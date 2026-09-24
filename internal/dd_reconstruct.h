/*
 * internal/dd_reconstruct.h - implementation of ddnet_demo_state.h.
 *
 * INTERNAL: included only by ddnet_demo_state.h's implementation.
 *
 * How ticks are rebuilt. For each character the demo gives anchors: at a
 * snapshot tick T the server sent its reckoning core from tick R <= T. The
 * server keeps a reckoning core only while its input-free prediction matches
 * the real core, so:
 *   - ticks R..T of an anchor are exact: the prediction from R is the truth.
 *   - between two anchors A and B (R_B > T_A + 1) lie ticks the demo does not
 *     describe. They are exact if B's re-sync was the only one since A, which
 *     is checked by stepping A's prediction one tick with input and comparing
 *     with B. The step runs first in the bare character physics, then in the
 *     full server world with every other character replayed; then a search
 *     for two re-syncs; and when nothing explains the gap, a simulation that
 *     is corrected to meet B.
 * Exact ticks are computed on request from the anchors (the prediction is a
 * few hundred float operations per tick); only solved gaps are stored.
 */
#ifndef DD_RECONSTRUCT_INTERNAL_H
#define DD_RECONSTRUCT_INTERNAL_H

#ifndef DDNET_DEMO_H
#error "include ddnet_demo.h before ddnet_demo_state.h's implementation"
#endif

#include "dd_physics.h"
#include "dd_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#endif

#define DD_RC_MAX_EVOLVE 3000 /* a reckoning older than this is not trusted */

/* ======================================================================== */
/* small utilities */

#define DD_RC_GROW(ptr, count, capacity, type)                                                                                               \
  (((count) < (capacity)) ||                                                                                                                 \
   ((capacity) = (capacity) ? (capacity) * 2 : 64, (ptr) = (type *)dd_rc_realloc((ptr), sizeof(type) * (size_t)(capacity)), (ptr) != NULL))

static void *dd_rc_realloc(void *ptr, size_t size) {
  void *grown = realloc(ptr, size);
  if (!grown) free(ptr);
  return grown;
}

/* CVariableInt::Unpack */
static const uint8_t *dd_rc_unpack_int(const uint8_t *src, const uint8_t *end, int *out) {
  if (!src || src >= end) return NULL;
  const int sign = (*src >> 6) & 1;
  int value = *src & 0x3F;
  static const int masks[] = {0x7F, 0x7F, 0x7F, 0x0F};
  static const int shifts[] = {6, 6 + 7, 6 + 7 + 7, 6 + 7 + 7 + 7};
  for (int i = 0; i < 4; i++) {
    if (!(*src & 0x80)) break;
    src++;
    if (src >= end) return NULL;
    value |= (*src & masks[i]) << shifts[i];
  }
  src++;
  value ^= -sign;
  *out = value;
  return src;
}

static const uint8_t *dd_rc_unpack_string(const uint8_t *src, const uint8_t *end, const char **out) {
  if (!src || src >= end) return NULL;
  const uint8_t *start = src;
  while (src < end && *src) src++;
  if (src >= end) return NULL;
  *out = (const char *)start;
  return src + 1;
}

/* IntsToStr */
static void dd_rc_ints_to_str(const int *ints, int num, char *out, size_t out_size) {
  size_t k = 0;
  for (int i = 0; i < num && k + 4 < out_size; i++) {
    out[k++] = (char)(((ints[i] >> 24) & 0xff) - 128);
    out[k++] = (char)(((ints[i] >> 16) & 0xff) - 128);
    out[k++] = (char)(((ints[i] >> 8) & 0xff) - 128);
    out[k++] = (char)((ints[i] & 0xff) - 128);
  }
  out[k < out_size ? k : out_size - 1] = 0;
  if (k > 0) out[k - 1] = 0; /* the last byte of the format is always zero */
}

#define DD_RC_OWNER_UNKNOWN (-2) /* while reading: the demo does not say, attributed after solving */

/* ---- extended object, event and message UUIDs (CalculateUuid of their names) ---- */
enum {
  DD_RC_EX_NONE = 0,
  DD_RC_EX_DDNETCHARACTER,
  DD_RC_EX_DDNETPLAYER,
  DD_RC_EX_GAMEINFOEX,
  DD_RC_EX_DDRACEPROJECTILE,
  DD_RC_EX_DDNETLASER,
  DD_RC_EX_DDNETPROJECTILE,
  DD_RC_EX_DDNETPICKUP,
  DD_RC_EX_SWITCHSTATE,
  DD_RC_EX_ENTITYEX,
  DD_RC_EX_SPECCHAR,
  DD_RC_EX_EVENT_BIRTHDAY,
  DD_RC_EX_EVENT_FINISH,
  DD_RC_EX_EVENT_MAPSOUNDWORLD,
  DD_RC_EX_MSG_TEAMSSTATE,
  DD_RC_EX_MSG_DDRACETIME,
  DD_RC_EX_MSG_RECORD,
  DD_RC_EX_MSG_KILLMSGTEAM,
  DD_RC_EX_MSG_RACEFINISH,
  DD_RC_EX_MSG_MAPSOUNDGLOBAL,
};

static const struct {
  int id;
  uint8_t uuid[16];
} dd_rc_uuids[] = {
    {DD_RC_EX_DDNETCHARACTER, {0x76, 0xce, 0x45, 0x5b, 0xf9, 0xeb, 0x3a, 0x48, 0xad, 0xd7, 0xe0, 0x4b, 0x94, 0x1d, 0x04, 0x5c}},
    {DD_RC_EX_DDNETPLAYER, {0x22, 0xca, 0x93, 0x8d, 0x13, 0x80, 0x3e, 0x2b, 0x9e, 0x7b, 0xd2, 0x55, 0x8e, 0xa6, 0xbe, 0x11}},
    {DD_RC_EX_GAMEINFOEX, {0x93, 0x3d, 0xea, 0x6a, 0xda, 0x79, 0x30, 0xea, 0xa9, 0x8f, 0x8a, 0xf0, 0x36, 0x89, 0xa9, 0x45}},
    {DD_RC_EX_DDRACEPROJECTILE, {0x0e, 0x6d, 0xb8, 0x5c, 0x2b, 0x61, 0x38, 0x6f, 0xbb, 0xf2, 0xd0, 0xd0, 0x47, 0x1b, 0x92, 0x72}},
    {DD_RC_EX_DDNETLASER, {0x29, 0xde, 0x68, 0xa2, 0x69, 0x28, 0x31, 0xb8, 0x83, 0x60, 0xa2, 0x30, 0x7e, 0x0d, 0x84, 0x4f}},
    {DD_RC_EX_DDNETPROJECTILE, {0x65, 0x50, 0xfb, 0xce, 0xf3, 0x17, 0x3b, 0x31, 0x8f, 0xfe, 0xd2, 0xb3, 0x7f, 0x3a, 0xb4, 0x0e}},
    {DD_RC_EX_DDNETPICKUP, {0xea, 0x5e, 0x4a, 0x51, 0x58, 0xfb, 0x36, 0x84, 0x96, 0xe4, 0xe0, 0xd2, 0x67, 0xf4, 0xca, 0x65}},
    {DD_RC_EX_SWITCHSTATE, {0xec, 0x15, 0xe6, 0x69, 0xce, 0x11, 0x33, 0x67, 0xae, 0x8e, 0xb9, 0x0e, 0x5b, 0x27, 0xb9, 0xd5}},
    {DD_RC_EX_SPECCHAR, {0x4b, 0x80, 0x1c, 0x74, 0xe2, 0x4c, 0x3c, 0xe0, 0xb9, 0x2c, 0xb7, 0x54, 0xd0, 0x2c, 0xfc, 0x8a}},
    {DD_RC_EX_ENTITYEX, {0x2d, 0xe9, 0xae, 0xc3, 0x32, 0xe4, 0x39, 0x86, 0x8f, 0x7e, 0xe7, 0x45, 0x9d, 0xa7, 0xf5, 0x35}},
    {DD_RC_EX_EVENT_BIRTHDAY, {0x1f, 0xd3, 0x57, 0x46, 0x62, 0x63, 0x35, 0x8c, 0xb4, 0xd6, 0x6e, 0xf6, 0x0e, 0x0e, 0xfa, 0xaa}},
    {DD_RC_EX_EVENT_FINISH, {0x68, 0xbf, 0x89, 0x39, 0xef, 0x55, 0x38, 0x78, 0x90, 0x82, 0x13, 0x52, 0x7e, 0xb0, 0xa5, 0x97}},
    {DD_RC_EX_EVENT_MAPSOUNDWORLD, {0x54, 0xec, 0xad, 0x2e, 0xbf, 0xad, 0x3b, 0xe5, 0x89, 0x03, 0x62, 0x1b, 0xa0, 0x52, 0x45, 0x8e}},
    {DD_RC_EX_MSG_TEAMSSTATE, {0xa0, 0x91, 0x96, 0x1a, 0x95, 0xe8, 0x37, 0x44, 0xbb, 0x60, 0x5e, 0xac, 0x9b, 0xd5, 0x63, 0xc6}},
    {DD_RC_EX_MSG_DDRACETIME, {0x5d, 0xde, 0x8b, 0x3c, 0x6f, 0x6f, 0x37, 0xac, 0xa7, 0x2a, 0xbb, 0x34, 0x1f, 0xe7, 0x6d, 0xe5}},
    {DD_RC_EX_MSG_RECORD, {0x80, 0x4f, 0x14, 0x9f, 0x9b, 0x53, 0x3b, 0x0a, 0x89, 0x7f, 0x59, 0x66, 0x3a, 0x1c, 0x4e, 0xb9}},
    {DD_RC_EX_MSG_KILLMSGTEAM, {0xee, 0x61, 0x0b, 0x6f, 0x90, 0x9f, 0x31, 0x1f, 0x93, 0xf7, 0x11, 0xa9, 0x5f, 0x55, 0xa0, 0x86}},
    {DD_RC_EX_MSG_RACEFINISH, {0xc9, 0x15, 0xba, 0x68, 0x0a, 0x49, 0x33, 0x24, 0x91, 0x5f, 0x62, 0x20, 0xce, 0xcf, 0x33, 0x33}},
    {DD_RC_EX_MSG_MAPSOUNDGLOBAL, {0x66, 0x9c, 0x97, 0x41, 0x69, 0x5a, 0x36, 0x9b, 0x85, 0x6f, 0x0b, 0x7f, 0xa6, 0x7f, 0x07, 0xcb}},
};

static int dd_rc_uuid_id(const uint8_t uuid[16]) {
  for (size_t i = 0; i < sizeof(dd_rc_uuids) / sizeof(dd_rc_uuids[0]); ++i)
    if (!memcmp(uuid, dd_rc_uuids[i].uuid, 16)) return dd_rc_uuids[i].id;
  return DD_RC_EX_NONE;
}

/* ======================================================================== */
/* the reconstructed demo */

typedef struct {
  int snap_index, snap_tick;
  dd_netobj_character chr;
  bool has_ddnet;
  dd_netobj_ddnet_character ddnet;
} dd_rc_anchor;

typedef struct {
  dd_rc_anchor *anchors;
  int count, capacity;
} dd_rc_track;

/* A solved stretch between two anchors. fill < 0: the earlier anchor's
 * prediction holds; otherwise explicit cores fills[fill .. fill + t1 - t0]. */
typedef struct {
  int t0, t1;
  int quality;
  int fill;
} dd_rc_gap;

typedef struct {
  dd_rc_gap *gaps;
  int count, capacity;
} dd_rc_gap_list;

typedef struct {
  int tick;
  int proj0, num_proj, laser0, num_laser, pickup0, num_pickup, event0, num_event;
  int local_client_id;
  bool has_game_info;
  dd_netobj_game_info game_info;
  bool has_switch_state;
  dd_netobj_switch_state switch_state;
} dd_rc_snap;

typedef struct {
  int tick;
  dd_state_player player;
} dd_rc_player_entry;

typedef struct {
  dd_rc_player_entry *entries;
  int count, capacity;
} dd_rc_player_log;

typedef struct {
  int tick;
  dd_tuning tuning;
} dd_rc_tune_entry;

typedef struct {
  int tick;
  int team[DD_STATE_MAX_CLIENTS];
  int num_ddrace_teams;
} dd_rc_teams_entry;

typedef struct {
  bool valid;
  int key, tick;
  dd_character_core core;
} dd_rc_evolve_cache;

/* string offsets of a message until the pools stop growing */
typedef struct {
  int text, text2, raw, raw_size;
} dd_rc_msg_offsets;

struct dd_demo_state {
  dd_demo_info info;
  char map_name[64];
  uint8_t *map_bytes; /* the demo's copy; the loader owns its own */
  size_t map_size;
  map_data_t map;
  bool have_map;
  dd_collision col;
  dd_server base;
  bool have_world;

  dd_rc_snap *snaps;
  int num_snaps, cap_snaps;
  dd_rc_track tracks[DD_STATE_MAX_CLIENTS];
  dd_rc_gap_list gaps[DD_STATE_MAX_CLIENTS];
  dd_netobj_character_core *fills;
  int num_fills, cap_fills;

  dd_state_projectile *projectiles;
  int num_projectiles, cap_projectiles;
  dd_state_laser *lasers;
  int num_lasers, cap_lasers;
  /* the snapshot item id of each projectile and laser: one entity keeps it across snapshots */
  int *projectile_ids, *laser_ids;
  int cap_projectile_ids, cap_laser_ids;
  dd_state_pickup *pickups;
  int num_pickups, cap_pickups;
  dd_state_event *events;
  int num_events, cap_events;
  dd_rc_player_log players[DD_STATE_MAX_CLIENTS];
  dd_state_message *messages;
  dd_rc_msg_offsets *message_offsets;
  int num_messages, cap_messages, cap_message_offsets;
  char *text_pool;
  int text_size, text_capacity;
  uint8_t *raw_pool;
  int raw_size, raw_capacity;
  dd_rc_tune_entry *tunes;
  int num_tunes, cap_tunes;
  dd_rc_teams_entry *teams;
  int num_teams, cap_teams;

  int first_tick, last_tick;
  dd_state_stats stats;

  /* per character: the last prediction step computed, to continue from */
  dd_rc_evolve_cache evolve_cache[DD_STATE_MAX_CLIENTS];
  dd_world_core evolve_world; /* empty, like the server's prediction world */
  dd_teams_core evolve_teams;
  bool snap_tuning_jitter; /* while solving: snap jittered tune params to defaults */

  /* while loading */
  dd_state_progress_fn progress;
  void *progress_user;
  float progress_reported;
  bool cancelled;
};

/* Reports progress, at most every 0.2%; false once the caller cancelled. */
static bool dd_rc_report(dd_demo_state *st, float progress) {
  if (st->cancelled) return false;
  if (!st->progress) return true;
  if (progress < 1.0f && progress - st->progress_reported < 0.002f) return true;
  st->progress_reported = progress;
  if (!st->progress(st->progress_user, progress)) st->cancelled = true;
  return !st->cancelled;
}

/* ======================================================================== */
/* reading the demo */

static int dd_rc_text(dd_demo_state *st, const char *text) {
  if (!text) return -1;
  const int len = (int)strlen(text) + 1;
  while (st->text_size + len > st->text_capacity) {
    st->text_capacity = st->text_capacity ? st->text_capacity * 2 : 4096;
    st->text_pool = (char *)dd_rc_realloc(st->text_pool, (size_t)st->text_capacity);
    if (!st->text_pool) return -1;
  }
  memcpy(st->text_pool + st->text_size, text, (size_t)len);
  st->text_size += len;
  return st->text_size - len;
}

static int dd_rc_raw(dd_demo_state *st, const uint8_t *data, int size) {
  while (st->raw_size + size > st->raw_capacity) {
    st->raw_capacity = st->raw_capacity ? st->raw_capacity * 2 : 8192;
    st->raw_pool = (uint8_t *)dd_rc_realloc(st->raw_pool, (size_t)st->raw_capacity);
    if (!st->raw_pool) return -1;
  }
  memcpy(st->raw_pool + st->raw_size, data, (size_t)size);
  st->raw_size += size;
  return st->raw_size - size;
}

static bool dd_rc_read_message(dd_demo_state *st, int tick, const uint8_t *data, int size) {
  const uint8_t *p = data, *end = data + size;
  int msg;
  p = dd_rc_unpack_int(p, end, &msg);
  if (!p) return true;
  if (msg & 1) return true; /* system message */
  msg >>= 1;
  int ex = DD_RC_EX_NONE;
  if (msg == DD_NETMSGTYPE_EX) {
    if (p + 16 > end) return true;
    ex = dd_rc_uuid_id(p);
    p += 16;
  }

  if (!DD_RC_GROW(st->messages, st->num_messages, st->cap_messages, dd_state_message)) return false;
  if (!DD_RC_GROW(st->message_offsets, st->num_messages, st->cap_message_offsets, dd_rc_msg_offsets)) return false;
  dd_state_message *m = &st->messages[st->num_messages];
  dd_rc_msg_offsets *o = &st->message_offsets[st->num_messages];
  memset(m, 0, sizeof(*m));
  o->text = o->text2 = -1;
  m->tick = tick;
  m->type = DD_STATE_MSG_OTHER;
  const char *text = NULL, *text2 = NULL;
  int v[8] = {0};

  if (ex == DD_RC_EX_NONE) {
    switch (msg) {
    case DD_NETMSGTYPE_SV_MOTD:
    case DD_NETMSGTYPE_SV_BROADCAST:
      if (dd_rc_unpack_string(p, end, &text)) m->type = msg == DD_NETMSGTYPE_SV_MOTD ? DD_STATE_MSG_MOTD : DD_STATE_MSG_BROADCAST;
      break;
    case DD_NETMSGTYPE_SV_CHAT:
      if ((p = dd_rc_unpack_int(p, end, &v[0])) && (p = dd_rc_unpack_int(p, end, &v[1])) && dd_rc_unpack_string(p, end, &text)) {
        m->type = DD_STATE_MSG_CHAT;
        m->team = v[0];
        m->client_id = v[1];
      }
      break;
    case DD_NETMSGTYPE_SV_KILLMSG:
      if ((p = dd_rc_unpack_int(p, end, &v[0])) && (p = dd_rc_unpack_int(p, end, &v[1])) && (p = dd_rc_unpack_int(p, end, &v[2])) &&
          dd_rc_unpack_int(p, end, &v[3])) {
        m->type = DD_STATE_MSG_KILLMSG;
        m->killer = v[0];
        m->victim = v[1];
        m->weapon = v[2];
        m->mode_special = v[3];
      }
      break;
    case DD_NETMSGTYPE_SV_SOUNDGLOBAL:
      if (dd_rc_unpack_int(p, end, &v[0])) {
        m->type = DD_STATE_MSG_SOUND_GLOBAL;
        m->value = v[0];
      }
      break;
    case DD_NETMSGTYPE_SV_TUNEPARAMS: {
      m->type = DD_STATE_MSG_TUNE_PARAMS;
      if (!DD_RC_GROW(st->tunes, st->num_tunes, st->cap_tunes, dd_rc_tune_entry)) return false;
      dd_rc_tune_entry *t = &st->tunes[st->num_tunes];
      t->tick = tick;
      dd_tuning_default(&t->tuning);
      int *params = (int *)&t->tuning;
      for (int i = 0; i < DD_TUNING_NUM; ++i) {
        int value;
        if (!(p = dd_rc_unpack_int(p, end, &value))) break;
        params[i] = value;
      }
      st->num_tunes++;
    } break;
    case DD_NETMSGTYPE_SV_WEAPONPICKUP:
      if (dd_rc_unpack_int(p, end, &v[0])) {
        m->type = DD_STATE_MSG_WEAPON_PICKUP;
        m->weapon = v[0];
      }
      break;
    case DD_NETMSGTYPE_SV_EMOTICON:
      if ((p = dd_rc_unpack_int(p, end, &v[0])) && dd_rc_unpack_int(p, end, &v[1])) {
        m->type = DD_STATE_MSG_EMOTICON;
        m->client_id = v[0];
        m->value = v[1];
      }
      break;
    case DD_NETMSGTYPE_SV_VOTESET:
      if ((p = dd_rc_unpack_int(p, end, &v[0])) && (p = dd_rc_unpack_string(p, end, &text)) && dd_rc_unpack_string(p, end, &text2)) {
        m->type = DD_STATE_MSG_VOTE_SET;
        m->value = v[0];
      }
      break;
    case DD_NETMSGTYPE_SV_VOTESTATUS:
      if ((p = dd_rc_unpack_int(p, end, &v[0])) && (p = dd_rc_unpack_int(p, end, &v[1])) && (p = dd_rc_unpack_int(p, end, &v[2])) &&
          dd_rc_unpack_int(p, end, &v[3])) {
        m->type = DD_STATE_MSG_VOTE_STATUS;
        m->yes = v[0];
        m->no = v[1];
        m->pass = v[2];
        m->total = v[3];
      }
      break;
    case 27: /* Sv_DDRaceTimeLegacy */
      if ((p = dd_rc_unpack_int(p, end, &v[0])) && (p = dd_rc_unpack_int(p, end, &v[1])) && dd_rc_unpack_int(p, end, &v[2])) {
        m->type = DD_STATE_MSG_DDRACE_TIME;
        m->time = v[0];
        m->check = v[1];
        m->finish = v[2] != 0;
      }
      break;
    case 28: /* Sv_RecordLegacy */
      if ((p = dd_rc_unpack_int(p, end, &v[0])) && dd_rc_unpack_int(p, end, &v[1])) {
        m->type = DD_STATE_MSG_RECORD;
        m->time = v[0];
        m->time2 = v[1];
      }
      break;
    case 30: ex = DD_RC_EX_MSG_TEAMSSTATE; break; /* Sv_TeamsStateLegacy */
    default: break;
    }
  }

  switch (ex) {
  case DD_RC_EX_MSG_TEAMSSTATE: {
    m->type = DD_STATE_MSG_TEAMS_STATE;
    if (!DD_RC_GROW(st->teams, st->num_teams, st->cap_teams, dd_rc_teams_entry)) return false;
    dd_rc_teams_entry *t = &st->teams[st->num_teams];
    memset(t, 0, sizeof(*t));
    t->tick = tick;
    int i;
    for (i = 0; i < DD_STATE_MAX_CLIENTS; i++) {
      int team;
      const uint8_t *q = dd_rc_unpack_int(p, end, &team);
      if (q && team >= DD_PHYS_TEAM_FLOCK && team < DD_PHYS_NUM_DDRACE_TEAMS) {
        t->team[i] = team;
        p = q;
      } else {
        t->team[i] = 0;
        break;
      }
    }
    /* like the client: 16-player servers have a smaller super team */
    t->num_ddrace_teams = i <= 16 ? 16 + 1 : DD_PHYS_NUM_DDRACE_TEAMS;
    st->num_teams++;
  } break;
  case DD_RC_EX_MSG_DDRACETIME:
    if ((p = dd_rc_unpack_int(p, end, &v[0])) && (p = dd_rc_unpack_int(p, end, &v[1])) && dd_rc_unpack_int(p, end, &v[2])) {
      m->type = DD_STATE_MSG_DDRACE_TIME;
      m->time = v[0];
      m->check = v[1];
      m->finish = v[2] != 0;
    }
    break;
  case DD_RC_EX_MSG_RECORD:
    if ((p = dd_rc_unpack_int(p, end, &v[0])) && dd_rc_unpack_int(p, end, &v[1])) {
      m->type = DD_STATE_MSG_RECORD;
      m->time = v[0];
      m->time2 = v[1];
    }
    break;
  case DD_RC_EX_MSG_KILLMSGTEAM:
    if ((p = dd_rc_unpack_int(p, end, &v[0])) && dd_rc_unpack_int(p, end, &v[1])) {
      m->type = DD_STATE_MSG_KILLMSG_TEAM;
      m->team = v[0];
      m->client_id = v[1];
    }
    break;
  case DD_RC_EX_MSG_RACEFINISH:
    if ((p = dd_rc_unpack_int(p, end, &v[0])) && (p = dd_rc_unpack_int(p, end, &v[1])) && (p = dd_rc_unpack_int(p, end, &v[2])) &&
        (p = dd_rc_unpack_int(p, end, &v[3]))) {
      m->type = DD_STATE_MSG_RACE_FINISH;
      m->client_id = v[0];
      m->time = v[1];
      m->diff = v[2];
      m->record_personal = v[3] != 0;
      m->record_server = dd_rc_unpack_int(p, end, &v[4]) ? v[4] != 0 : false;
    }
    break;
  case DD_RC_EX_MSG_MAPSOUNDGLOBAL:
    if (dd_rc_unpack_int(p, end, &v[0])) {
      m->type = DD_STATE_MSG_MAP_SOUND_GLOBAL;
      m->value = v[0];
    }
    break;
  default: break;
  }

  o->text = dd_rc_text(st, text);
  o->text2 = dd_rc_text(st, text2);
  o->raw = dd_rc_raw(st, data, size);
  o->raw_size = size;
  if (o->raw < 0) return false;
  st->num_messages++;
  return true;
}

/* Resolves the extended item types of one snapshot: each EX item maps a raw
 * type number to a UUID. */
typedef struct {
  int raw[32], id[32];
  int count;
} dd_rc_ex_map;

static void dd_rc_build_ex_map(const dd_snapshot *snap, dd_rc_ex_map *map) {
  map->count = 0;
  for (int i = 0; i < snap->num_items && map->count < 32; ++i) {
    const dd_snap_item *item = dd_snap_get_item(snap, i);
    if (dd_snap_item_type(item) != DD_NETOBJTYPE_EX) continue;
    const int *data = dd_snap_item_data(item);
    if (dd_snap_get_item_size(snap, i) < 16) continue;
    uint8_t uuid[16];
    for (int k = 0; k < 4; ++k) {
      uuid[k * 4 + 0] = (uint8_t)((unsigned)data[k] >> 24);
      uuid[k * 4 + 1] = (uint8_t)((unsigned)data[k] >> 16);
      uuid[k * 4 + 2] = (uint8_t)((unsigned)data[k] >> 8);
      uuid[k * 4 + 3] = (uint8_t)(unsigned)data[k];
    }
    map->raw[map->count] = dd_snap_item_id(item);
    map->id[map->count] = dd_rc_uuid_id(uuid);
    map->count++;
  }
}

/* Vanilla types map to themselves; extended ones to DD_RC_EX_* + 1000. */
static int dd_rc_item_kind(const dd_rc_ex_map *map, int type) {
  if (type < DD_NUM_VANILLA_NETOBJTYPES) return type;
  for (int i = 0; i < map->count; ++i)
    if (map->raw[i] == type) return 1000 + map->id[i];
  return -1;
}

static bool dd_rc_item_fits(const dd_snapshot *snap, int index, size_t size) { return dd_snap_get_item_size(snap, index) >= (int)size; }

static bool dd_rc_read_snapshot(dd_demo_state *st, int tick, const dd_snapshot *snap) {
  if (!DD_RC_GROW(st->snaps, st->num_snaps, st->cap_snaps, dd_rc_snap)) return false;
  const int snap_index = st->num_snaps;
  dd_rc_snap *s = &st->snaps[snap_index];
  memset(s, 0, sizeof(*s));
  s->tick = tick;
  s->local_client_id = -1;
  s->proj0 = st->num_projectiles;
  s->laser0 = st->num_lasers;
  s->pickup0 = st->num_pickups;
  s->event0 = st->num_events;

  dd_rc_ex_map ex;
  dd_rc_build_ex_map(snap, &ex);

  const dd_netobj_character *chars[DD_STATE_MAX_CLIENTS] = {0};
  const dd_netobj_ddnet_character *ddnet[DD_STATE_MAX_CLIENTS] = {0};
  const dd_netobj_player_info *pinfo[DD_STATE_MAX_CLIENTS] = {0};
  const dd_netobj_client_info *cinfo[DD_STATE_MAX_CLIENTS] = {0};
  const dd_netobj_ddnet_player *dplayer[DD_STATE_MAX_CLIENTS] = {0};
  const dd_netobj_spec_char *spec[DD_STATE_MAX_CLIENTS] = {0};

  for (int i = 0; i < snap->num_items; ++i) {
    const dd_snap_item *item = dd_snap_get_item(snap, i);
    const int kind = dd_rc_item_kind(&ex, dd_snap_item_type(item));
    const int id = dd_snap_item_id(item);
    const void *data = dd_snap_item_data(item);
    const bool client = id >= 0 && id < DD_STATE_MAX_CLIENTS;
    switch (kind) {
    case DD_NETOBJTYPE_CHARACTER:
      if (client && dd_rc_item_fits(snap, i, sizeof(dd_netobj_character))) chars[id] = (const dd_netobj_character *)data;
      break;
    case 1000 + DD_RC_EX_DDNETCHARACTER:
      /* older servers send fewer fields; the rest stays at the defaults */
      if (client && dd_rc_item_fits(snap, i, sizeof(int) * 5)) ddnet[id] = (const dd_netobj_ddnet_character *)data;
      break;
    case DD_NETOBJTYPE_PLAYERINFO:
      if (dd_rc_item_fits(snap, i, sizeof(dd_netobj_player_info))) {
        const dd_netobj_player_info *pi = (const dd_netobj_player_info *)data;
        if (pi->m_ClientId >= 0 && pi->m_ClientId < DD_STATE_MAX_CLIENTS) {
          pinfo[pi->m_ClientId] = pi;
          if (pi->m_Local) s->local_client_id = pi->m_ClientId;
        }
      }
      break;
    case DD_NETOBJTYPE_CLIENTINFO:
      if (client && dd_rc_item_fits(snap, i, sizeof(dd_netobj_client_info))) cinfo[id] = (const dd_netobj_client_info *)data;
      break;
    case 1000 + DD_RC_EX_DDNETPLAYER:
      if (client && dd_rc_item_fits(snap, i, sizeof(dd_netobj_ddnet_player))) dplayer[id] = (const dd_netobj_ddnet_player *)data;
      break;
    case 1000 + DD_RC_EX_SPECCHAR:
      if (client && dd_rc_item_fits(snap, i, sizeof(dd_netobj_spec_char))) spec[id] = (const dd_netobj_spec_char *)data;
      break;
    case DD_NETOBJTYPE_GAMEINFO:
      if (dd_rc_item_fits(snap, i, sizeof(dd_netobj_game_info))) {
        s->has_game_info = true;
        s->game_info = *(const dd_netobj_game_info *)data;
      }
      break;
    case 1000 + DD_RC_EX_SWITCHSTATE:
      if (dd_rc_item_fits(snap, i, sizeof(int) * 9)) {
        s->has_switch_state = true;
        memset(&s->switch_state, 0, sizeof(s->switch_state));
        memcpy(&s->switch_state, data, (size_t)dd_mini(dd_snap_get_item_size(snap, i), (int)sizeof(s->switch_state)));
      }
      break;
    case DD_NETOBJTYPE_PROJECTILE:
    case 1000 + DD_RC_EX_DDRACEPROJECTILE:
    case 1000 + DD_RC_EX_DDNETPROJECTILE: {
      if (!DD_RC_GROW(st->projectiles, st->num_projectiles, st->cap_projectiles, dd_state_projectile)) return false;
      dd_state_projectile *p = &st->projectiles[st->num_projectiles];
      memset(p, 0, sizeof(*p));
      p->owner = -1;
      const int *d = (const int *)data;
      /* ExtractProjectileInfo */
      if (kind == 1000 + DD_RC_EX_DDNETPROJECTILE) {
        if (!dd_rc_item_fits(snap, i, sizeof(dd_netobj_ddnet_projectile))) break;
        const dd_netobj_ddnet_projectile *pr = (const dd_netobj_ddnet_projectile *)data;
        p->x = pr->m_X / 100.0f;
        p->y = pr->m_Y / 100.0f;
        if (pr->m_Owner < 0) {
          p->vel_x = pr->m_VelX / 1e6f;
          p->vel_y = pr->m_VelY / 1e6f;
        } else {
          p->vel_x = (float)pr->m_VelX;
          p->vel_y = (float)pr->m_VelY;
        }
        if (pr->m_Flags & (1 << 4)) {
          dd_vec2 n = dd_v2_normalize(dd_v2(p->vel_x, p->vel_y));
          p->vel_x = n.x;
          p->vel_y = n.y;
        }
        p->type = pr->m_Type;
        p->start_tick = pr->m_StartTick;
        p->owner = pr->m_Owner;
        p->switch_number = pr->m_SwitchNumber;
        p->tune_zone = pr->m_TuneZone;
        p->bouncing = ((pr->m_Flags & 1) ? 1 : 0) | ((pr->m_Flags & 2) ? 2 : 0);
        p->explosive = (pr->m_Flags & 4) != 0;
        p->freeze = (pr->m_Flags & 8) != 0;
      } else if (kind == 1000 + DD_RC_EX_DDRACEPROJECTILE || (d[3] >= 0 && (d[3] & (1 << 9)))) {
        if (!dd_rc_item_fits(snap, i, sizeof(dd_netobj_ddrace_projectile))) break;
        const dd_netobj_ddrace_projectile *pr = (const dd_netobj_ddrace_projectile *)data;
        p->x = pr->m_X / 100.0f;
        p->y = pr->m_Y / 100.0f;
        float angle = pr->m_Angle / 1000000.0f;
        p->vel_x = sinf(-angle);
        p->vel_y = cosf(-angle);
        p->type = pr->m_Type;
        p->start_tick = pr->m_StartTick;
        p->owner = pr->m_Data & 255;
        if ((pr->m_Data & (1 << 8)) || p->owner < 0 || p->owner >= DD_STATE_MAX_CLIENTS) p->owner = -1;
        p->bouncing = (pr->m_Data >> 10) & 3;
        p->explosive = (pr->m_Data & (1 << 12)) != 0;
        p->freeze = (pr->m_Data & (1 << 13)) != 0;
      } else {
        if (!dd_rc_item_fits(snap, i, sizeof(dd_netobj_projectile))) break;
        const dd_netobj_projectile *pr = (const dd_netobj_projectile *)data;
        p->x = (float)pr->m_X;
        p->y = (float)pr->m_Y;
        p->vel_x = pr->m_VelX / 100.0f;
        p->vel_y = pr->m_VelY / 100.0f;
        p->type = pr->m_Type;
        p->start_tick = pr->m_StartTick;
        p->owner = DD_RC_OWNER_UNKNOWN;
      }
      if (!DD_RC_GROW(st->projectile_ids, st->num_projectiles, st->cap_projectile_ids, int)) return false;
      st->projectile_ids[st->num_projectiles] = id;
      st->num_projectiles++;
      s->num_proj++;
    } break;
    case DD_NETOBJTYPE_LASER:
    case 1000 + DD_RC_EX_DDNETLASER: {
      if (!DD_RC_GROW(st->lasers, st->num_lasers, st->cap_lasers, dd_state_laser)) return false;
      dd_state_laser *l = &st->lasers[st->num_lasers];
      memset(l, 0, sizeof(*l));
      l->owner = -1;
      if (kind == DD_NETOBJTYPE_LASER) {
        if (!dd_rc_item_fits(snap, i, sizeof(dd_netobj_laser))) break;
        const dd_netobj_laser *la = (const dd_netobj_laser *)data;
        l->to_x = (float)la->m_X;
        l->to_y = (float)la->m_Y;
        l->from_x = (float)la->m_FromX;
        l->from_y = (float)la->m_FromY;
        l->start_tick = la->m_StartTick;
        l->owner = DD_RC_OWNER_UNKNOWN;
      } else {
        if (!dd_rc_item_fits(snap, i, sizeof(int) * 7)) break;
        dd_netobj_ddnet_laser la;
        memset(&la, 0, sizeof(la));
        memcpy(&la, data, (size_t)dd_mini(dd_snap_get_item_size(snap, i), (int)sizeof(la)));
        l->to_x = (float)la.m_ToX;
        l->to_y = (float)la.m_ToY;
        l->from_x = (float)la.m_FromX;
        l->from_y = (float)la.m_FromY;
        l->start_tick = la.m_StartTick;
        l->owner = la.m_Owner;
        l->type = la.m_Type;
        l->switch_number = la.m_SwitchNumber;
        l->subtype = la.m_Subtype;
        l->flags = la.m_Flags;
      }
      if (!DD_RC_GROW(st->laser_ids, st->num_lasers, st->cap_laser_ids, int)) return false;
      st->laser_ids[st->num_lasers] = id;
      st->num_lasers++;
      s->num_laser++;
    } break;
    case DD_NETOBJTYPE_PICKUP:
    case 1000 + DD_RC_EX_DDNETPICKUP: {
      if (!DD_RC_GROW(st->pickups, st->num_pickups, st->cap_pickups, dd_state_pickup)) return false;
      dd_state_pickup *pk = &st->pickups[st->num_pickups];
      memset(pk, 0, sizeof(*pk));
      if (kind == DD_NETOBJTYPE_PICKUP) {
        if (!dd_rc_item_fits(snap, i, sizeof(dd_netobj_pickup))) break;
        const dd_netobj_pickup *pu = (const dd_netobj_pickup *)data;
        pk->x = (float)pu->m_X;
        pk->y = (float)pu->m_Y;
        pk->type = pu->m_Type;
        pk->subtype = pu->m_Subtype;
      } else {
        if (!dd_rc_item_fits(snap, i, sizeof(dd_netobj_ddnet_pickup))) break;
        const dd_netobj_ddnet_pickup *pu = (const dd_netobj_ddnet_pickup *)data;
        pk->x = (float)pu->m_X;
        pk->y = (float)pu->m_Y;
        pk->type = pu->m_Type;
        pk->subtype = pu->m_Subtype;
        pk->switch_number = pu->m_SwitchNumber;
        pk->flags = pu->m_Flags;
      }
      st->num_pickups++;
      s->num_pickup++;
    } break;
    case DD_NETEVENTTYPE_EXPLOSION:
    case DD_NETEVENTTYPE_SPAWN:
    case DD_NETEVENTTYPE_HAMMERHIT:
    case DD_NETEVENTTYPE_DEATH:
    case DD_NETEVENTTYPE_SOUNDGLOBAL:
    case DD_NETEVENTTYPE_SOUNDWORLD:
    case DD_NETEVENTTYPE_DAMAGEIND:
    case 1000 + DD_RC_EX_EVENT_FINISH:
    case 1000 + DD_RC_EX_EVENT_BIRTHDAY:
    case 1000 + DD_RC_EX_EVENT_MAPSOUNDWORLD: {
      if (!dd_rc_item_fits(snap, i, sizeof(dd_netevent_common))) break;
      if (!DD_RC_GROW(st->events, st->num_events, st->cap_events, dd_state_event)) return false;
      dd_state_event *e = &st->events[st->num_events];
      memset(e, 0, sizeof(*e));
      const int *d = (const int *)data;
      const int size = dd_snap_get_item_size(snap, i);
      e->x = (float)d[0];
      e->y = (float)d[1];
      e->owner = DD_RC_OWNER_UNKNOWN;
      switch (kind) {
      case DD_NETEVENTTYPE_EXPLOSION: e->type = DD_STATE_EVENT_EXPLOSION; break;
      case DD_NETEVENTTYPE_SPAWN: e->type = DD_STATE_EVENT_SPAWN; break;
      case DD_NETEVENTTYPE_HAMMERHIT: e->type = DD_STATE_EVENT_HAMMERHIT; break;
      case DD_NETEVENTTYPE_DEATH:
        e->type = DD_STATE_EVENT_DEATH;
        e->client_id = size >= 12 ? d[2] : -1;
        break;
      case DD_NETEVENTTYPE_SOUNDGLOBAL:
        e->type = DD_STATE_EVENT_SOUND_GLOBAL;
        e->sound_id = size >= 12 ? d[2] : -1;
        break;
      case DD_NETEVENTTYPE_SOUNDWORLD:
        e->type = DD_STATE_EVENT_SOUND_WORLD;
        e->sound_id = size >= 12 ? d[2] : -1;
        break;
      case DD_NETEVENTTYPE_DAMAGEIND:
        e->type = DD_STATE_EVENT_DAMAGE_IND;
        e->angle = size >= 12 ? d[2] : 0;
        break;
      case 1000 + DD_RC_EX_EVENT_FINISH: e->type = DD_STATE_EVENT_FINISH; break;
      case 1000 + DD_RC_EX_EVENT_BIRTHDAY: e->type = DD_STATE_EVENT_BIRTHDAY; break;
      default:
        e->type = DD_STATE_EVENT_MAP_SOUND_WORLD;
        e->sound_id = size >= 12 ? d[2] : -1;
        break;
      }
      st->num_events++;
      s->num_event++;
    } break;
    default: break;
    }
  }

  /* characters become anchors */
  for (int cid = 0; cid < DD_STATE_MAX_CLIENTS; ++cid) {
    if (!chars[cid]) continue;
    dd_rc_track *t = &st->tracks[cid];
    if (!DD_RC_GROW(t->anchors, t->count, t->capacity, dd_rc_anchor)) return false;
    dd_rc_anchor *a = &t->anchors[t->count++];
    memset(a, 0, sizeof(*a));
    a->snap_index = snap_index;
    a->snap_tick = tick;
    a->chr = *chars[cid];
    if (ddnet[cid]) {
      a->has_ddnet = true;
      /* the DDNet defaults for fields an older server did not send */
      a->ddnet.m_Jumps = 2;
      a->ddnet.m_TeleCheckpoint = -1;
      a->ddnet.m_JumpedTotal = -1;
      a->ddnet.m_NinjaActivationTick = -1;
      a->ddnet.m_FreezeStart = -1;
      a->ddnet.m_TuneZoneOverride = -1;
      int size = 0;
      for (int i = 0; i < snap->num_items; ++i) {
        const dd_snap_item *item = dd_snap_get_item(snap, i);
        if ((const void *)dd_snap_item_data(item) == (const void *)ddnet[cid]) size = dd_snap_get_item_size(snap, i);
      }
      memcpy(&a->ddnet, ddnet[cid], (size_t)dd_mini(size, (int)sizeof(a->ddnet)));
    }
  }

  /* players, logged when they change */
  for (int cid = 0; cid < DD_STATE_MAX_CLIENTS; ++cid) {
    dd_state_player p;
    memset(&p, 0, sizeof(p));
    if (pinfo[cid]) {
      p.connected = true;
      p.local = pinfo[cid]->m_Local != 0;
      p.team = pinfo[cid]->m_Team;
      p.score = pinfo[cid]->m_Score;
      p.latency = pinfo[cid]->m_Latency;
    }
    if (cinfo[cid]) {
      p.connected = true;
      dd_rc_ints_to_str(cinfo[cid]->m_aName, 4, p.name, sizeof(p.name));
      dd_rc_ints_to_str(cinfo[cid]->m_aClan, 3, p.clan, sizeof(p.clan));
      dd_rc_ints_to_str(cinfo[cid]->m_aSkin, 6, p.skin, sizeof(p.skin));
      p.country = cinfo[cid]->m_Country;
      p.use_custom_color = cinfo[cid]->m_UseCustomColor;
      p.color_body = cinfo[cid]->m_ColorBody;
      p.color_feet = cinfo[cid]->m_ColorFeet;
    }
    if (dplayer[cid]) {
      p.ddnet_flags = (unsigned)dplayer[cid]->m_Flags;
      p.auth_level = dplayer[cid]->m_AuthLevel;
    }
    if (spec[cid]) {
      p.spec_char = true;
      p.spec_x = spec[cid]->m_X;
      p.spec_y = spec[cid]->m_Y;
    }
    dd_rc_player_log *log = &st->players[cid];
    if (log->count > 0 && !memcmp(&log->entries[log->count - 1].player, &p, sizeof(p))) continue;
    if (log->count == 0 && !p.connected) continue;
    if (!DD_RC_GROW(log->entries, log->count, log->capacity, dd_rc_player_entry)) return false;
    log->entries[log->count].tick = tick;
    log->entries[log->count].player = p;
    log->count++;
  }

  st->num_snaps++;
  return true;
}

static bool dd_rc_read_demo(dd_demo_state *st, FILE *f, char *error, size_t error_size) {
  dd_demo_reader *dr = demo_r_create();
  if (!dr) {
    snprintf(error, error_size, "out of memory");
    return false;
  }
  if (!demo_r_open(dr, f)) {
    snprintf(error, error_size, "not a DDNet demo");
    demo_r_destroy(&dr);
    return false;
  }
  st->info = *demo_r_get_info(dr);
  memcpy(st->map_name, st->info.header.map_name, sizeof(st->info.header.map_name) < sizeof(st->map_name) ? sizeof(st->info.header.map_name) : sizeof(st->map_name));
  st->map_name[sizeof(st->map_name) - 1] = 0;
  st->map_size = st->info.map_size;
  if (st->map_size > 0) {
    st->map_bytes = (uint8_t *)malloc(st->map_size);
    if (!st->map_bytes || !demo_r_read_map(dr, st->map_bytes, (uint32_t)st->map_size)) {
      snprintf(error, error_size, "cannot read the embedded map");
      demo_r_destroy(&dr);
      return false;
    }
  }

  static uint8_t unpacked[DD_SNAPSHOT_MAX_SIZE];
  dd_demo_chunk chunk;
  int tick = 0;
  bool ok = true;
  while (ok && demo_r_next_chunk(dr, &chunk)) {
    switch (chunk.type) {
    case DD_CHUNK_TICK_MARKER: tick = chunk.tick; break;
    case DD_CHUNK_SNAP: ok = dd_rc_read_snapshot(st, chunk.tick, (const dd_snapshot *)chunk.data); break;
    case DD_CHUNK_SNAP_DELTA:
      if (demo_r_unpack_delta(dr, chunk.data, unpacked) >= 0) ok = dd_rc_read_snapshot(st, chunk.tick, (const dd_snapshot *)unpacked);
      break;
    case DD_CHUNK_MSG: ok = dd_rc_read_message(st, tick, chunk.data, chunk.size); break;
    default: break;
    }
  }
  demo_r_destroy(&dr);
  if (!ok) {
    snprintf(error, error_size, "out of memory");
    return false;
  }
  if (st->num_snaps == 0) {
    snprintf(error, error_size, "the demo has no snapshots");
    return false;
  }
  st->first_tick = st->snaps[0].tick;
  st->last_tick = st->snaps[st->num_snaps - 1].tick;
  for (int i = 0; i < st->num_messages; ++i) st->last_tick = dd_maxi(st->last_tick, st->messages[i].tick);

  /* pools are final: point the messages into them */
  for (int i = 0; i < st->num_messages; ++i) {
    const dd_rc_msg_offsets *o = &st->message_offsets[i];
    st->messages[i].text = o->text >= 0 ? st->text_pool + o->text : NULL;
    st->messages[i].text2 = o->text2 >= 0 ? st->text_pool + o->text2 : NULL;
    st->messages[i].raw = st->raw_pool + o->raw;
    st->messages[i].raw_size = o->raw_size;
  }
  return true;
}

/* ======================================================================== */
/* per-character state at a tick */

/* The reckoning core of an anchor; an unreckoned one (tick 0) is the live core
 * at the snapshot tick. */
static dd_netobj_character dd_rc_anchor_base(const dd_rc_anchor *a) {
  dd_netobj_character c = a->chr;
  if (c.core.m_Tick <= 0 || c.core.m_Tick > a->snap_tick) c.core.m_Tick = a->snap_tick;
  return c;
}

static bool dd_rc_evolve(const dd_demo_state *st, const dd_netobj_character *from, int tick, dd_netobj_character_core *out) {
  if (tick < from->core.m_Tick || tick - from->core.m_Tick > DD_RC_MAX_EVOLVE) return false;
  dd_netobj_character c = *from;
  if (st->have_map) dd_evolve_character(&st->col, &c, tick);
  *out = c.core;
  return true;
}

/* dd_evolve_character, continuing from the character's last computed step
 * when it follows the same anchor. The full core is kept, so the result is
 * identical to evolving from the anchor. */
static bool dd_rc_evolve_cached(const dd_demo_state *cst, int cid, int key, const dd_netobj_character *from, int tick,
                                dd_netobj_character_core *out) {
  dd_demo_state *st = (dd_demo_state *)cst;
  if (tick < from->core.m_Tick || tick - from->core.m_Tick > DD_RC_MAX_EVOLVE) return false;
  if (cid < 0 || cid >= DD_STATE_MAX_CLIENTS) return dd_rc_evolve(cst, from, tick, out);
  dd_rc_evolve_cache *cache = &st->evolve_cache[cid];
  if (!cache->valid || cache->key != key || cache->tick > tick) {
    dd_core_init(&cache->core, &st->evolve_world, &st->col, &st->evolve_teams);
    dd_core_read(&cache->core, &from->core);
    cache->tick = from->core.m_Tick;
    cache->key = key;
    cache->valid = true;
  }
  while (cache->tick < tick) {
    cache->tick++;
    dd_core_tick(&cache->core, false, true);
    dd_core_move(&cache->core);
    dd_core_quantize(&cache->core);
  }
  dd_core_write(&cache->core, out);
  out->m_Tick = tick;
  return true;
}

static const dd_rc_gap *dd_rc_find_gap(const dd_demo_state *st, int cid, int tick) {
  const dd_rc_gap_list *list = &st->gaps[cid];
  int lo = 0, hi = list->count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (list->gaps[mid].t1 < tick)
      lo = mid + 1;
    else if (list->gaps[mid].t0 > tick)
      hi = mid - 1;
    else
      return &list->gaps[mid];
  }
  return NULL;
}

/* the last anchor at or before `tick`, or -1 */
static int dd_rc_anchor_at(const dd_rc_track *t, int tick) {
  int lo = 0, hi = t->count - 1, found = -1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (t->anchors[mid].snap_tick <= tick) {
      found = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return found;
}

static bool dd_rc_consecutive(const dd_rc_track *t, int k) {
  return k + 1 < t->count && t->anchors[k + 1].snap_index == t->anchors[k].snap_index + 1;
}

/* The movement core of `cid` at `tick`. `extras` receives the anchor whose
 * non-core fields apply. Returns the quality, or NONE. */
static int dd_rc_core_at(const dd_demo_state *st, int cid, int tick, dd_netobj_character_core *out, const dd_rc_anchor **extras, bool evolve) {
  const dd_rc_track *t = &st->tracks[cid];
  if (t->count == 0) return DD_QUALITY_NONE;
  int k = dd_rc_anchor_at(t, tick);
  if (k < 0) {
    /* before the first anchor: from its re-sync on it is exact */
    const dd_rc_anchor *first = &t->anchors[0];
    const int r = first->chr.core.m_Tick;
    if (r > 0 && r <= tick && tick < first->snap_tick) {
      if (extras) *extras = first;
      if (evolve) {
        dd_netobj_character base = dd_rc_anchor_base(first);
        if (!dd_rc_evolve_cached(st, cid, 0, &base, tick, out)) return DD_QUALITY_NONE;
      }
      return tick == r ? DD_QUALITY_RECORDED : DD_QUALITY_EXACT;
    }
    return DD_QUALITY_NONE;
  }
  const dd_rc_anchor *a = &t->anchors[k];
  if (extras) *extras = a;
  const int ra = a->chr.core.m_Tick > 0 && a->chr.core.m_Tick <= a->snap_tick ? a->chr.core.m_Tick : a->snap_tick;

  if (tick == a->snap_tick) {
    if (evolve) {
      dd_netobj_character base = dd_rc_anchor_base(a);
      if (!dd_rc_evolve_cached(st, cid, k, &base, tick, out)) return DD_QUALITY_NONE;
    }
    return ra == tick ? DD_QUALITY_RECORDED : DD_QUALITY_EXACT;
  }

  if (dd_rc_consecutive(t, k)) {
    const dd_rc_anchor *b = &t->anchors[k + 1];
    const int rb = b->chr.core.m_Tick;
    if (rb > 0 && rb <= tick) {
      if (extras) *extras = a;
      if (evolve) {
        dd_netobj_character base = dd_rc_anchor_base(b);
        if (!dd_rc_evolve_cached(st, cid, k + 1, &base, tick, out)) return DD_QUALITY_NONE;
      }
      return rb == tick ? DD_QUALITY_RECORDED : DD_QUALITY_EXACT;
    }
    if (rb == a->chr.core.m_Tick && rb > 0) {
      if (evolve) {
        dd_netobj_character base = dd_rc_anchor_base(a);
        if (!dd_rc_evolve_cached(st, cid, k, &base, tick, out)) return DD_QUALITY_NONE;
      }
      return DD_QUALITY_EXACT;
    }
    const dd_rc_gap *g = dd_rc_find_gap(st, cid, tick);
    if (g && g->fill >= 0) {
      if (evolve) *out = st->fills[g->fill + (tick - g->t0)];
      return g->quality;
    }
    if (evolve) {
      dd_netobj_character base = dd_rc_anchor_base(a);
      if (!dd_rc_evolve_cached(st, cid, k, &base, tick, out)) return DD_QUALITY_NONE;
    }
    return g ? g->quality : DD_QUALITY_APPROXIMATED;
  }

  /* the character is gone by the next snapshot (or the demo ends) */
  const int next_snap_tick = a->snap_index + 1 < st->num_snaps ? st->snaps[a->snap_index + 1].tick : st->last_tick + 1;
  if (tick >= next_snap_tick) return DD_QUALITY_NONE;
  if (evolve) {
    dd_netobj_character base = dd_rc_anchor_base(a);
    if (!dd_rc_evolve_cached(st, cid, k, &base, tick, out)) return DD_QUALITY_NONE;
  }
  return DD_QUALITY_APPROXIMATED;
}

static const dd_rc_teams_entry *dd_rc_teams_at(const dd_demo_state *st, int tick) {
  const dd_rc_teams_entry *found = NULL;
  for (int i = 0; i < st->num_teams && st->teams[i].tick <= tick; ++i) found = &st->teams[i];
  return found;
}

static const dd_rc_snap *dd_rc_snap_at(const dd_demo_state *st, int tick) {
  int lo = 0, hi = st->num_snaps - 1, found = -1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (st->snaps[mid].tick <= tick) {
      found = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return found >= 0 ? &st->snaps[found] : NULL;
}

/* ======================================================================== */
/* gap solving */

typedef struct {
  int forced, calls;
} dd_rc_random;

static int dd_rc_random_cb(void *user, int below_this) {
  dd_rc_random *r = (dd_rc_random *)user;
  r->calls++;
  return r->forced < below_this ? r->forced : 0;
}

static bool dd_rc_core_equal(const dd_netobj_character_core *a, const dd_netobj_character_core *b) {
  return memcmp((const int *)a + 1, (const int *)b + 1, sizeof(*a) - sizeof(int)) == 0;
}

/* Aim candidates for a tick whose recorded state is `after`. */
static int dd_rc_targets(const dd_netobj_character_core *after, const dd_rc_anchor *anchor, int targets[4][2]) {
  int n = 0;
  if (anchor && anchor->has_ddnet && (anchor->ddnet.m_TargetX || anchor->ddnet.m_TargetY)) {
    targets[n][0] = anchor->ddnet.m_TargetX;
    targets[n][1] = anchor->ddnet.m_TargetY;
    n++;
  }
  if (after->m_HookDx || after->m_HookDy) {
    targets[n][0] = after->m_HookDx;
    targets[n][1] = after->m_HookDy;
    n++;
  }
  const float angle = after->m_Angle / 256.0f;
  targets[n][0] = (int)lroundf(cosf(angle) * 1024.0f);
  targets[n][1] = (int)lroundf(sinf(angle) * 1024.0f);
  n++;
  return n;
}

/* One tick of the bare character physics with input, as the core-only check. */
static bool dd_rc_core_step_input(const dd_demo_state *st, const dd_netobj_character_core *before, const dd_netobj_character_core *after,
                                  const dd_rc_anchor *anchor, dd_player_input *matched) {
  int targets[4][2];
  const int num_targets = dd_rc_targets(after, anchor, targets);
  for (int t = 0; t < num_targets; ++t) {
    for (int jump = 0; jump <= 1; ++jump) {
      for (int hook = 0; hook <= 1; ++hook) {
        dd_world_core world;
        memset(&world, 0, sizeof(world));
        dd_teams_core teams;
        dd_teams_reset(&teams);
        dd_character_core core;
        dd_core_init(&core, &world, &st->col, &teams);
        dd_core_read(&core, before);
        core.input.direction = after->m_Direction;
        core.input.target_x = targets[t][0];
        core.input.target_y = targets[t][1];
        core.input.jump = jump;
        core.input.hook = hook;
        dd_core_tick(&core, true, true);
        dd_core_move(&core);
        dd_core_quantize(&core);
        dd_netobj_character_core out;
        memset(&out, 0, sizeof(out));
        dd_core_write(&core, &out);
        out.m_Angle = after->m_Angle;
        if (dd_rc_core_equal(&out, after)) {
          if (matched) *matched = core.input;
          return true;
        }
      }
    }
  }
  return false;
}

static bool dd_rc_core_step(const dd_demo_state *st, const dd_netobj_character_core *before, const dd_netobj_character_core *after,
                            const dd_rc_anchor *anchor) {
  return dd_rc_core_step_input(st, before, after, anchor, NULL);
}

static bool dd_rc_full_auto(int weapon, bool jetpack) {
  return weapon == DD_WEAPON_ID_GRENADE || weapon == DD_WEAPON_ID_SHOTGUN || weapon == DD_WEAPON_ID_LASER || (jetpack && weapon == DD_WEAPON_ID_GUN);
}

/* Sets the fire button in `in` (for the tick `tick`) from a recorded attack
 * tick: a press fires on arrival, between ticks, so it shows as tick - 1; a
 * held automatic weapon fires during the tick itself. `held` is the fire
 * counter the character had before. */
static void dd_rc_apply_fire(dd_player_input *in, int held, const dd_rc_anchor *anchor, int tick) {
  in->fire = held;
  if (!anchor) return;
  const int attack = anchor->chr.m_AttackTick;
  const bool jetpack = anchor->has_ddnet && (anchor->ddnet.m_Flags & DD_CHARACTERFLAG_JETPACK);
  if (attack == tick - 1) {
    in->fire = (held & 1) ? held + 2 : held + 1; /* a fresh press */
  } else if (attack == tick && dd_rc_full_auto(anchor->chr.m_Weapon, jetpack)) {
    in->fire = (held & 1) ? held : held + 1; /* held down */
  }
}

/* The input a character most likely held at `tick`, from its states. */
static dd_player_input dd_rc_derive_input(const dd_netobj_character_core *now, const dd_rc_anchor *anchor, int tick) {
  dd_player_input in;
  memset(&in, 0, sizeof(in));
  in.direction = now->m_Direction;
  in.hook = now->m_HookState != DD_HOOK_IDLE;
  in.jump = (now->m_Jumped & 1) != 0;
  if (anchor && anchor->has_ddnet && (anchor->ddnet.m_TargetX || anchor->ddnet.m_TargetY)) {
    in.target_x = anchor->ddnet.m_TargetX;
    in.target_y = anchor->ddnet.m_TargetY;
  } else {
    const float angle = now->m_Angle / 256.0f;
    in.target_x = (int)lroundf(cosf(angle) * 256.0f);
    in.target_y = (int)lroundf(sinf(angle) * 256.0f);
  }
  dd_rc_apply_fire(&in, 0, anchor, tick);
  return in;
}

/* Puts a character into a server world as it was at w->tick (the tick before
 * the one to simulate), with `input` for the next tick. */
static dd_character *dd_rc_place_character(const dd_demo_state *st, dd_server *w, int cid, const dd_netobj_character_core *core,
                                           const dd_netobj_character_core *prev_core, const dd_rc_anchor *anchor, const dd_player_input *input) {
  dd_character *c = dd_server_spawn_character(w, cid, dd_v2((float)core->m_X, (float)core->m_Y));
  if (!c) return NULL;
  dd_core_read(&c->core, core);
  const int tick = w->tick;
  if (anchor) {
    if (anchor->has_ddnet) {
      dd_core_read_ddnet(&c->core, &anchor->ddnet);
      const int freeze_end = anchor->ddnet.m_FreezeEnd;
      c->freeze_time = freeze_end > tick ? freeze_end - tick : 0;
      c->tele_checkpoint = anchor->ddnet.m_TeleCheckpoint > 0 ? anchor->ddnet.m_TeleCheckpoint : 0;
      dd_chr_set_solo(w, c, c->core.solo);
      for (int wpn = 0; wpn < DD_PHYS_NUM_WEAPONS; ++wpn)
        if (c->core.weapons[wpn].got) c->core.weapons[wpn].ammo = -1;
    }
    c->core.active_weapon = anchor->chr.m_Weapon >= 0 && anchor->chr.m_Weapon < DD_PHYS_NUM_WEAPONS ? anchor->chr.m_Weapon : DD_WEAPON_ID_GUN;
    if (!anchor->has_ddnet) {
      /* without DDNet's flags only the weapon in hand is known to be owned */
      c->core.weapons[c->core.active_weapon].got = true;
      c->core.weapons[c->core.active_weapon].ammo = -1;
    }
    c->health = anchor->chr.m_Health;
    c->armor = anchor->chr.m_Armor;
    c->attack_tick = anchor->chr.m_AttackTick;
    const float delay = dd_tuning_fire_delay(&w->tuning[c->tune_zone], c->core.active_weapon) * DD_SERVER_TICK_SPEED;
    const int remaining = (int)delay - (tick - c->attack_tick);
    c->reload_timer = remaining > 0 ? remaining : 0;
  }
  /* Older servers do not send JumpedTotal. A tee whose air jump is used up
   * (bit 2) has at least jumps - 1 of them behind it; DDRacePostCoreTick
   * would otherwise give the air jump back. */
  if (!anchor || !anchor->has_ddnet || anchor->ddnet.m_JumpedTotal == -1)
    c->core.jumped_total = (c->core.jumped & 2) ? dd_maxi(c->core.jumps - 1, 0) : 0;
  c->prev_pos = prev_core ? dd_v2((float)prev_core->m_X, (float)prev_core->m_Y) : c->core.pos;
  c->tune_zone = dd_col_is_tune(&st->col, dd_col_get_map_index(&st->col, c->core.pos));
  c->core.tuning = w->tuning[c->tune_zone];
  w->players[cid].tune_zone = c->tune_zone;
  /* the input so far: no new fire presses unless asked for */
  dd_player_input held = *input;
  held.fire = 0;
  c->input = c->prev_input = c->saved_input = c->latest_input = c->latest_prev_input = held;
  c->num_inputs = 2;
  return c;
}

/* Some servers jitter the tuning they send (an anti-bot measure) while their
 * physics keeps the real values. Parameters within 1% of DDNet's default are
 * taken to be the default. */
static void dd_rc_unjitter(dd_tuning *t) {
  dd_tuning def;
  dd_tuning_default(&def);
  int *v = (int *)t;
  const int *d = (const int *)&def;
  for (int k = 0; k < DD_TUNING_NUM; ++k) {
    const int tolerance = dd_absi(d[k]) / 100;
    if (v[k] != d[k] && dd_absi(v[k] - d[k]) <= tolerance) v[k] = d[k];
  }
}

/* Builds the world at tick-1 with every known character, `target` last with
 * `target_input`. Returns false when the target cannot be placed. */
static bool dd_rc_seed_world(const dd_demo_state *st, dd_server *w, int tick, int target, const dd_netobj_character_core *target_core,
                             const dd_netobj_character_core *target_prev, const dd_rc_anchor *target_anchor, const dd_player_input *target_input) {
  if (!dd_server_copy(w, &st->base)) return false;
  w->tick = tick - 1;
  w->num_events = 0;

  const dd_rc_teams_entry *teams = dd_rc_teams_at(st, tick - 1);
  if (teams) {
    for (int i = 0; i < DD_STATE_MAX_CLIENTS; ++i) w->teams.team[i] = teams->team[i];
    w->teams.num_ddrace_teams = teams->num_ddrace_teams;
  }
  /* tune params describe the recording player's zone; without a tune layer
   * that is the global tuning */
  if (!st->col.tune_type) {
    for (int i = st->num_tunes - 1; i >= 0; --i)
      if (st->tunes[i].tick <= tick - 1) {
        w->tuning[0] = st->tunes[i].tuning;
        break;
      }
    if (st->snap_tuning_jitter) dd_rc_unjitter(&w->tuning[0]);
  }
  const dd_rc_snap *snap = dd_rc_snap_at(st, tick - 1);
  if (snap && snap->has_switch_state) {
    for (int n = 0; n < w->core.num_switchers && n <= snap->switch_state.m_HighestSwitchNumber && n < 256; ++n) {
      const bool on = (snap->switch_state.m_aStatus[n / 32] >> (n % 32)) & 1;
      for (int team = 0; team < DD_PHYS_NUM_DDRACE_TEAMS; ++team) w->core.switchers[n].status[team] = on;
    }
  }

  /* projectiles as the last snapshot described them */
  if (snap) {
    for (int i = 0; i < snap->num_proj; ++i) {
      const dd_state_projectile *p = &st->projectiles[snap->proj0 + i];
      const int zone = p->tune_zone > 0 && p->tune_zone < DD_NUM_TUNEZONES ? p->tune_zone : 0;
      const float lifetime = p->type == DD_WEAPON_ID_GRENADE ? dd_tune(w->tuning[zone].grenade_lifetime) : dd_tune(w->tuning[zone].gun_lifetime);
      int span = p->owner < 0 ? -2 : (int)(DD_SERVER_TICK_SPEED * lifetime) - (tick - 1 - p->start_tick);
      if (p->owner >= 0 && span < 0) continue;
      const bool explosive = p->explosive || (p->owner >= 0 && p->type == DD_WEAPON_ID_GRENADE);
      int e = dd_projectile_new(w, p->type, p->owner, dd_v2(p->x, p->y), dd_v2(p->vel_x, p->vel_y), span, p->freeze, explosive,
                                dd_v2(p->vel_x, p->vel_y), p->switch_number ? DD_LAYER_SWITCH : DD_LAYER_GAME, p->switch_number);
      if (e < 0) continue;
      w->ents[e].u.projectile.start_tick = p->start_tick;
      w->ents[e].u.projectile.bouncing = p->bouncing;
      if (p->tune_zone) w->ents[e].u.projectile.tune_zone = p->tune_zone;
    }
  }

  /* characters, in the server's list order: ascending strong/weak id,
   * which head insertion gets by placing them in descending order */
  int order[DD_STATE_MAX_CLIENTS], ids[DD_STATE_MAX_CLIENTS], count = 0;
  for (int cid = 0; cid < DD_STATE_MAX_CLIENTS; ++cid) {
    dd_netobj_character_core core;
    const dd_rc_anchor *anchor = NULL;
    if (cid != target && dd_rc_core_at(st, cid, tick - 1, &core, &anchor, true) == DD_QUALITY_NONE) continue;
    if (cid == target) anchor = target_anchor;
    order[count] = cid;
    ids[count] = anchor && anchor->has_ddnet ? anchor->ddnet.m_StrongWeakId : cid;
    count++;
  }
  for (int i = 1; i < count; ++i)
    for (int j = i; j > 0 && ids[j] > ids[j - 1]; --j) {
      int ti = ids[j];
      ids[j] = ids[j - 1];
      ids[j - 1] = ti;
      int tc = order[j];
      order[j] = order[j - 1];
      order[j - 1] = tc;
    }
  bool placed_target = false;
  for (int i = 0; i < count; ++i) {
    const int cid = order[i];
    if (cid == target) {
      if (!dd_rc_place_character(st, w, cid, target_core, target_prev, target_anchor, target_input)) return false;
      placed_target = true;
      continue;
    }
    dd_netobj_character_core core, prev, next;
    const dd_rc_anchor *anchor = NULL, *next_anchor = NULL;
    if (dd_rc_core_at(st, cid, tick - 1, &core, &anchor, true) == DD_QUALITY_NONE) continue;
    const bool have_prev = dd_rc_core_at(st, cid, tick - 2, &prev, NULL, true) != DD_QUALITY_NONE;
    const bool have_next = dd_rc_core_at(st, cid, tick, &next, &next_anchor, true) != DD_QUALITY_NONE;
    dd_player_input input = dd_rc_derive_input(have_next ? &next : &core, have_next ? next_anchor : anchor, tick);
    dd_rc_place_character(st, w, cid, &core, have_prev ? &prev : NULL, anchor, &input);
  }
  return placed_target || target < 0;
}

#ifdef DD_RC_DIAGNOSTICS
static int dd_rc_diag_dump = 12;
static int dd_rc_diag_approx_dump = 16;
static int dd_rc_diag_joint_dump = 20;
/* Counters for tests: how each gap was settled, and whether the full server
 * world agrees with the bare physics where the latter already explains a gap. */
static struct {
  long core_ok, server_ok, two_resyncs, joint_ok, approximated;
  long cross_checked, cross_agreed;
} dd_rc_diag;
#endif

/* A world at tick-1 with everyone placed, reused for each candidate input. */
typedef struct {
  dd_server world;
  bool has_input[DD_STATE_MAX_CLIENTS];
  dd_player_input input[DD_STATE_MAX_CLIENTS]; /* everyone else's input for the tick */
} dd_rc_step_template;

/* `seed` is the anchor whose non-core fields describe the target at tick-1. */
static bool dd_rc_prepare_step(const dd_demo_state *st, dd_rc_step_template *tpl, int cid, int tick, const dd_netobj_character_core *before,
                               const dd_netobj_character_core *before_prev, const dd_rc_anchor *seed) {
  /* what the target held on the tick before: the base for press edges */
  dd_player_input held = dd_rc_derive_input(before, seed, tick - 1);
  held.fire = 0;
  if (!dd_rc_seed_world(st, &tpl->world, tick, cid, before, before_prev, seed, &held)) return false;
  for (int other = 0; other < DD_STATE_MAX_CLIENTS; ++other) {
    tpl->has_input[other] = false;
    if (other == cid || !dd_chr(&tpl->world, other)) continue;
    dd_netobj_character_core next;
    const dd_rc_anchor *next_anchor = NULL;
    tpl->input[other] = dd_chr(&tpl->world, other)->input;
    if (dd_rc_core_at(st, other, tick, &next, &next_anchor, true) != DD_QUALITY_NONE)
      tpl->input[other] = dd_rc_derive_input(&next, next_anchor, tick);
    tpl->has_input[other] = true;
  }
  return true;
}

/* One server tick from tick-1 to tick for `cid` with each candidate input;
 * true when one reproduces `after` exactly. */
static bool dd_rc_server_step_once(const dd_demo_state *st, dd_rc_step_template *tpl, dd_server *w, int cid, int tick,
                                   const dd_netobj_character_core *before, const dd_netobj_character_core *before_prev,
                                   const dd_rc_anchor *seed, const dd_netobj_character_core *after, const dd_rc_anchor *anchor) {
  if (!st->have_world) return false;
  if (!dd_rc_prepare_step(st, tpl, cid, tick, before, before_prev, seed)) return false;
  int targets[4][2];
  const int num_targets = dd_rc_targets(after, anchor, targets);
  const bool fired = anchor && (anchor->chr.m_AttackTick == tick || anchor->chr.m_AttackTick == tick - 1);
  if (!dd_server_copy(w, &tpl->world)) return false; /* the candidates restore from here */
  for (int t = 0; t < num_targets; ++t) {
    for (int jump = 0; jump <= 1; ++jump) {
      for (int hook = 0; hook <= 1; ++hook) {
        dd_rc_random random = {0, 0};
        for (random.forced = 0; random.forced < 8; ++random.forced) {
          dd_player_input input;
          memset(&input, 0, sizeof(input));
          input.direction = after->m_Direction;
          input.target_x = targets[t][0];
          input.target_y = targets[t][1];
          input.jump = jump;
          input.hook = hook;
          if (fired) dd_rc_apply_fire(&input, 0, anchor, tick);
          if (!dd_server_restore(w, &tpl->world)) return false;
          w->core.random = dd_rc_random_cb;
          w->core.random_user = &random;
          random.calls = 0;
          for (int other = 0; other < DD_STATE_MAX_CLIENTS; ++other)
            if (tpl->has_input[other]) dd_server_set_input(w, other, &tpl->input[other]);
          dd_server_set_input(w, cid, &input);
          dd_server_tick(w);
          dd_character *c = dd_chr(w, cid);
          if (c) {
            dd_netobj_character_core out;
            memset(&out, 0, sizeof(out));
            dd_core_write(&c->core, &out);
            out.m_Angle = after->m_Angle;
            /* the snapped direction is the input's, which frozen tees zero */
            out.m_Direction = c->input.direction;
            if (dd_rc_core_equal(&out, after)) return true;
          }
          if (random.calls == 0) break; /* no random choice to vary */
        }
      }
    }
  }
  return false;
}

/* The step with the recorded tuning, then with jitter taken out of it. */
static bool dd_rc_server_step(dd_demo_state *st, dd_rc_step_template *tpl, dd_server *w, int cid, int tick,
                              const dd_netobj_character_core *before, const dd_netobj_character_core *before_prev, const dd_rc_anchor *seed,
                              const dd_netobj_character_core *after, const dd_rc_anchor *anchor) {
  st->snap_tuning_jitter = false;
  if (dd_rc_server_step_once(st, tpl, w, cid, tick, before, before_prev, seed, after, anchor)) return true;
  bool jittered = false;
  for (int i = st->num_tunes - 1; i >= 0; --i)
    if (st->tunes[i].tick <= tick - 1) {
      dd_tuning t = st->tunes[i].tuning;
      dd_rc_unjitter(&t);
      jittered = memcmp(&t, &st->tunes[i].tuning, sizeof(t)) != 0;
      break;
    }
  if (!jittered || st->col.tune_type) return false;
  st->snap_tuning_jitter = true;
  const bool ok = dd_rc_server_step_once(st, tpl, w, cid, tick, before, before_prev, seed, after, anchor);
  st->snap_tuning_jitter = false;
  return ok;
}

/* ---- joint solving of interacting characters ---- */

#define DD_RC_JOINT_MAX 8        /* characters solved together */
#define DD_RC_JOINT_MAX_TICKS 16 /* longest stretch solved tick by tick */
#define DD_RC_JOINT_MAX_CANDIDATES 96

typedef struct {
  int cid, k;       /* the character and its anchor A */
  int rb;           /* B's re-sync tick: the first known tick after the gap */
  const dd_rc_anchor *a, *b;
  int num_candidates[DD_RC_JOINT_MAX_TICKS];
  dd_player_input candidates[DD_RC_JOINT_MAX_TICKS][DD_RC_JOINT_MAX_CANDIDATES];
  int choice[DD_RC_JOINT_MAX_TICKS], best_choice[DD_RC_JOINT_MAX_TICKS];
  long score;
  dd_netobj_character_core sim[DD_RC_JOINT_MAX_TICKS + 1]; /* ticks T_a+1 .. rb */
} dd_rc_joint_member;

static void dd_rc_joint_add_candidate(dd_rc_joint_member *m, int slot, const dd_player_input *in) {
  for (int i = 0; i < m->num_candidates[slot]; ++i)
    if (!memcmp(&m->candidates[slot][i], in, sizeof(*in))) return;
  if (m->num_candidates[slot] < DD_RC_JOINT_MAX_CANDIDATES) m->candidates[slot][m->num_candidates[slot]++] = *in;
}

/* Inputs worth trying at each tick of the gap, from the states on both sides. */
static void dd_rc_joint_candidates(dd_rc_joint_member *m, int ta) {
  const dd_netobj_character_core *ac = &m->a->chr.core, *bc = &m->b->chr.core;
  const dd_player_input from_a = dd_rc_derive_input(ac, m->a, ta), from_b = dd_rc_derive_input(bc, m->b, m->rb);
  /* aims: B's, A's, B's hook direction (the aim when the hook left, to 1/256)
   * and halfway between A and B */
  int aims[4][2], num_aims = 0;
  aims[num_aims][0] = from_b.target_x;
  aims[num_aims++][1] = from_b.target_y;
  aims[num_aims][0] = from_a.target_x;
  aims[num_aims++][1] = from_a.target_y;
  if (bc->m_HookState != DD_HOOK_IDLE && (bc->m_HookDx || bc->m_HookDy)) {
    aims[num_aims][0] = bc->m_HookDx;
    aims[num_aims++][1] = bc->m_HookDy;
  }
  aims[num_aims][0] = (from_a.target_x + from_b.target_x) / 2;
  aims[num_aims++][1] = (from_a.target_y + from_b.target_y) / 2;
  const int ticks = m->rb - ta;
  for (int slot = 0; slot < ticks; ++slot) {
    m->num_candidates[slot] = 0;
    const bool last = slot == ticks - 1;
    const int dirs[2] = {from_b.direction, from_a.direction};
    const int hooks[2] = {from_b.hook, from_a.hook};
    const int jumps[2] = {from_b.jump, from_a.jump};
    for (int t = 0; t < num_aims; ++t)
      for (int d = 0; d < (last ? 1 : 2); ++d)
        for (int h = 0; h < 2; ++h)
          for (int j = 0; j < 2; ++j) {
            dd_player_input in;
            memset(&in, 0, sizeof(in));
            in.direction = dirs[d];
            in.hook = last ? (h == 0 ? from_b.hook : !from_b.hook) : hooks[h];
            in.jump = last ? (j == 0 ? from_b.jump : !from_b.jump) : jumps[j];
            in.target_x = aims[t][0];
            in.target_y = aims[t][1];
            if (!in.target_x && !in.target_y) in.target_y = -1;
            dd_rc_joint_add_candidate(m, slot, &in);
          }
    m->choice[slot] = m->best_choice[slot] = 0;
  }
}

static long dd_rc_core_score(const dd_netobj_character_core *out, const dd_netobj_character_core *want) {
  const int *o = (const int *)out, *e = (const int *)want;
  long score = 0;
  for (int f = 1; f < 15; ++f) {
    if (f == 5) continue; /* the angle follows the aim, approximated here */
    score += f <= 4 ? dd_absi(o[f] - e[f]) : (o[f] != e[f]) * 64;
  }
  return score;
}

/* Runs the stretch from T_a with every member's current choice; returns the
 * sum of the members' scores and fills their simulated states. */
/* Everyone's known state and likely input over a stretch, looked up once
 * instead of on every candidate run. Index: tick - (ta - 1). */
typedef struct {
  bool known;
  dd_netobj_character_core core;
  bool has_input;
  dd_player_input input;
} dd_rc_known;

static dd_rc_known dd_rc_joint_table[DD_RC_JOINT_MAX_TICKS + 3][DD_STATE_MAX_CLIENTS];

static void dd_rc_joint_fill_table(const dd_demo_state *st, int ta, int end) {
  for (int tick = ta - 1; tick <= end && tick - (ta - 1) < DD_RC_JOINT_MAX_TICKS + 3; ++tick) {
    for (int cid = 0; cid < DD_STATE_MAX_CLIENTS; ++cid) {
      dd_rc_known *k = &dd_rc_joint_table[tick - (ta - 1)][cid];
      const dd_rc_anchor *anchor = NULL;
      k->known = dd_rc_core_at(st, cid, tick, &k->core, &anchor, true) != DD_QUALITY_NONE;
      k->has_input = k->known;
      if (k->known) k->input = dd_rc_derive_input(&k->core, anchor, tick);
    }
  }
}

/* The world part way through a stretch, and the member inputs that led
 * there: a run whose inputs agree up to that tick resumes from it. */
typedef struct {
  bool valid, primed; /* primed: `world` holds a copy of this stretch's start */
  int tick;           /* the last tick simulated into `world` */
  dd_server world;
  dd_player_input inputs[DD_RC_JOINT_MAX][DD_RC_JOINT_MAX_TICKS];
  long score[DD_RC_JOINT_MAX];
  dd_netobj_character_core sim[DD_RC_JOINT_MAX][DD_RC_JOINT_MAX_TICKS + 1];
} dd_rc_joint_checkpoint;

static const dd_player_input *dd_rc_joint_input(const dd_rc_joint_member *m, int slot, bool use_best) {
  return &m->candidates[slot][use_best ? m->best_choice[slot] : m->choice[slot]];
}

static bool dd_rc_joint_resumable(const dd_rc_joint_checkpoint *cp, const dd_rc_joint_member *members, int count, int ta, bool use_best) {
  if (!cp->valid) return false;
  for (int i = 0; i < count; ++i)
    for (int tick = ta + 1; tick <= cp->tick && tick <= members[i].rb; ++tick)
      if (memcmp(dd_rc_joint_input(&members[i], tick - ta - 1, use_best), &cp->inputs[i][tick - ta - 1], sizeof(dd_player_input))) return false;
  return true;
}

/* `keep` > 0 saves the world after tick ta + keep into `cp`, for the runs
 * after this one that only change inputs from slot `keep` on. */
static long dd_rc_joint_run(const dd_demo_state *st, const dd_server *start, dd_server *w, int ta, dd_rc_joint_member *members, int count,
                            bool use_best, dd_rc_joint_checkpoint *cp, int keep) {
  (void)st;
  int end = ta;
  for (int i = 0; i < count; ++i) end = dd_maxi(end, members[i].rb);
  int first = ta + 1;
  if (dd_rc_joint_resumable(cp, members, count, ta, use_best)) {
    if (!dd_server_restore(w, &cp->world)) return -1;
    first = cp->tick + 1;
    for (int i = 0; i < count; ++i) {
      members[i].score = cp->score[i];
      memcpy(members[i].sim, cp->sim[i], sizeof(members[i].sim[0]) * (size_t)(cp->tick - ta));
    }
  } else {
    if (!dd_server_restore(w, start)) return -1;
    for (int i = 0; i < count; ++i) members[i].score = 0;
  }
  if (keep <= 0 || ta + keep >= end) keep = 0;
  if (keep && first <= ta + keep) cp->valid = false; /* about to be replaced */
  for (int tick = first; tick <= end; ++tick) {
    /* everyone else, and members past their re-sync, follow their known states */
    if (tick > ta + 1) {
      for (int cid = 0; cid < DD_STATE_MAX_CLIENTS; ++cid) {
        bool free_member = false;
        for (int i = 0; i < count; ++i)
          if (members[i].cid == cid && tick <= members[i].rb) free_member = true;
        dd_character *c = dd_chr(w, cid);
        if (!c || free_member) continue;
        const dd_rc_known *known = &dd_rc_joint_table[tick - 1 - (ta - 1)][cid];
        const dd_rc_known *prev = &dd_rc_joint_table[tick - 2 - (ta - 1)][cid];
        if (!known->known) continue;
        dd_core_read(&c->core, &known->core);
        dd_chr_set_pos(w, c, c->core.pos);
        if (prev->known) c->prev_pos = dd_v2((float)prev->core.m_X, (float)prev->core.m_Y);
      }
    }
    for (int cid = 0; cid < DD_STATE_MAX_CLIENTS; ++cid) {
      dd_character *c = dd_chr(w, cid);
      if (!c) continue;
      dd_player_input in = c->input;
      int mi = -1;
      for (int i = 0; i < count; ++i)
        if (members[i].cid == cid && tick <= members[i].rb) mi = i;
      if (mi >= 0) {
        const int slot = tick - ta - 1;
        const int choice = use_best ? members[mi].best_choice[slot] : members[mi].choice[slot];
        in = members[mi].candidates[slot][choice];
        dd_rc_apply_fire(&in, c->latest_input.fire, members[mi].b, tick);
      } else {
        const dd_rc_known *next = &dd_rc_joint_table[tick - (ta - 1)][cid];
        if (next->has_input) in = next->input;
      }
      dd_server_set_input(w, cid, &in);
    }
    dd_server_tick(w);
    for (int i = 0; i < count; ++i) {
      dd_rc_joint_member *m = &members[i];
      if (tick > m->rb) continue;
      dd_character *c = dd_chr(w, m->cid);
      dd_netobj_character_core *out = &m->sim[tick - ta - 1];
      if (!c) {
        m->score += 1 << 20;
        *out = m->b->chr.core;
        continue;
      }
      memset(out, 0, sizeof(*out));
      dd_core_write(&c->core, out);
      out->m_Direction = c->input.direction;
      out->m_Tick = tick;
      if (tick == m->rb) {
        dd_netobj_character_core want = m->b->chr.core;
        out->m_Angle = want.m_Angle;
        m->score += dd_rc_core_score(out, &want);
      }
    }
    if (keep && tick == ta + keep) {
      if (!(cp->primed ? dd_server_restore(&cp->world, w) : dd_server_copy(&cp->world, w))) return -1;
      cp->primed = true;
      cp->valid = true;
      cp->tick = tick;
      for (int i = 0; i < count; ++i) {
        cp->score[i] = members[i].score;
        memcpy(cp->sim[i], members[i].sim, sizeof(members[i].sim[0]) * (size_t)keep);
        for (int t = ta + 1; t <= tick && t <= members[i].rb; ++t) cp->inputs[i][t - ta - 1] = *dd_rc_joint_input(&members[i], t - ta - 1, use_best);
      }
    }
  }
  long total = 0;
  for (int i = 0; i < count; ++i) total += members[i].score;
  return total;
}

/* Solves characters whose gaps after the same snapshot were not explained
 * one by one: they are simulated together from T_a. Returns true when every
 * member reproduced its re-sync exactly; members' best runs are left in sim. */
static bool dd_rc_joint_solve(const dd_demo_state *st, dd_server *start, dd_server *w, int ta, dd_rc_joint_member *members, int count,
                              dd_rc_joint_checkpoint *cp) {
  if (!st->have_world || count <= 0) return false;
  cp->valid = cp->primed = false;
  if (!dd_rc_seed_world(st, start, ta + 1, -1, NULL, NULL, NULL, NULL)) return false;
  int end = ta;
  for (int i = 0; i < count; ++i) {
    dd_rc_joint_candidates(&members[i], ta);
    end = dd_maxi(end, members[i].rb);
  }
  dd_rc_joint_fill_table(st, ta, end);
  /* Tees far from every member cannot reach them within a stretch (hooks
   * reach 380 units, explosions 135); leaving them out keeps each run cheap. */
  for (int cid = 0; cid < DD_STATE_MAX_CLIENTS; ++cid) {
    dd_character *c = dd_chr(start, cid);
    if (!c) continue;
    bool near = false;
    for (int i = 0; i < count && !near; ++i) {
      if (members[i].cid == cid) near = true;
      const dd_character *mc = dd_chr(start, members[i].cid);
      if (mc && dd_v2_distance(mc->core.pos, c->core.pos) < 1500.0f) near = true;
    }
    if (near) continue;
    c->alive = false;
    dd_ent_free(start, c->ent);
    dd_world_set_character(&start->core, cid, NULL);
  }

  if (!dd_server_copy(w, start)) return false; /* the runs restore from here */
  long best = dd_rc_joint_run(st, start, w, ta, members, count, false, cp, 0);
  if (best < 0) return false;
  int budget = 2000; /* runs per stretch */
  for (int round = 0; round < 3 && best > 0; ++round) {
    bool improved = false;
    for (int i = 0; i < count && best > 0; ++i) {
      dd_rc_joint_member *m = &members[i];
      const int ticks = m->rb - ta;
      /* every combination of this member's candidates, the others fixed.
       * Combination c sets slot s to digit s of c (slot 0 the lowest), and
       * they are judged in that order: the first strictly better one wins
       * and a perfect one ends the search. They are simulated in a different
       * order though, slot 0 outermost, so that runs sharing their first
       * ticks resume from a checkpoint. */
      int combos = 1;
      for (int slot = 0; slot < ticks && combos <= 256; ++slot) combos *= m->num_candidates[slot];
      if (combos > 256) combos = 256;
      const int limit = dd_mini(combos, budget);
      if (limit <= 0) continue;
      int vary = 0, weight[DD_RC_JOINT_MAX_TICKS];
      for (int span = 1; vary < ticks && span < limit; span *= m->num_candidates[vary], ++vary) weight[vary] = span;
      int kept[DD_RC_JOINT_MAX_TICKS];
      memcpy(kept, m->choice, sizeof(kept));
      long scores[256];
      int first_perfect = limit;
      int digit[DD_RC_JOINT_MAX_TICKS] = {0};
      for (int slot = vary; slot < ticks; ++slot) m->choice[slot] = 0;
      if (vary == 0) scores[0] = dd_rc_joint_run(st, start, w, ta, members, count, false, cp, 0); /* the one combination: all 0 */
      while (vary > 0) {
        int combo = 0;
        for (int slot = 0; slot < vary; ++slot) combo += digit[slot] * weight[slot];
        if (combo < first_perfect) {
          for (int slot = 0; slot < vary; ++slot) m->choice[slot] = digit[slot];
          scores[combo] = dd_rc_joint_run(st, start, w, ta, members, count, false, cp, vary - 1);
          if (scores[combo] == 0) first_perfect = combo;
        }
        int slot = vary - 1; /* next digits, the last varying slot fastest */
        while (slot >= 0 && ++digit[slot] == m->num_candidates[slot]) digit[slot--] = 0;
        if (slot < 0) break;
      }
      int saved = -1;
      for (int combo = 0; combo < limit && best > 0; ++combo, --budget)
        if (scores[combo] >= 0 && scores[combo] < best) {
          best = scores[combo];
          saved = combo;
          improved = true;
        }
      memcpy(m->choice, kept, sizeof(kept));
      if (saved >= 0)
        for (int slot = 0; slot < ticks; ++slot) m->choice[slot] = slot < vary ? saved / weight[slot] % m->num_candidates[slot] : 0;
    }
    if (!improved) break;
  }
  /* Close but not exact: the aim of some tick is off by a fraction of a
   * degree. Rotate each tick's aim in small steps, as a long vector so the
   * direction is fine-grained, and keep what gets closer. */
  best = dd_rc_joint_run(st, start, w, ta, members, count, false, cp, 0); /* members' scores for the kept choices */
  for (int i = 0; i < count && best > 0; ++i) {
    dd_rc_joint_member *m = &members[i];
    if (m->score == 0 || m->score > 4096) continue;
    const int ticks = m->rb - ta;
    for (int slot = 0; slot < ticks && best > 0; ++slot) {
      const dd_player_input base = m->candidates[slot][m->choice[slot]];
      const float angle = atan2f((float)base.target_y, (float)base.target_x);
      const int saved = m->choice[slot];
      int kept = saved;
      for (int step = -24; step <= 24 && best > 0 && budget > 0; ++step, --budget) {
        if (!step || m->num_candidates[slot] >= DD_RC_JOINT_MAX_CANDIDATES) continue;
        dd_player_input in = base;
        const float a = angle + (float)step / 2048.0f;
        in.target_x = (int)lroundf(cosf(a) * 100000.0f);
        in.target_y = (int)lroundf(sinf(a) * 100000.0f);
        m->candidates[slot][m->num_candidates[slot]] = in;
        m->choice[slot] = m->num_candidates[slot];
        long score = dd_rc_joint_run(st, start, w, ta, members, count, false, cp, slot);
        if (score >= 0 && score < best) {
          best = score;
          kept = m->num_candidates[slot]++;
        }
      }
      m->choice[slot] = kept;
    }
  }
  for (int i = 0; i < count; ++i) memcpy(members[i].best_choice, members[i].choice, sizeof(members[i].choice));
  dd_rc_joint_run(st, start, w, ta, members, count, true, cp, 0);
  return best == 0;
}

static int dd_rc_add_fills(dd_demo_state *st, const dd_netobj_character_core *cores, int count) {
  while (st->num_fills + count > st->cap_fills) {
    st->cap_fills = st->cap_fills ? st->cap_fills * 2 : 1024;
    st->fills = (dd_netobj_character_core *)dd_rc_realloc(st->fills, sizeof(dd_netobj_character_core) * (size_t)st->cap_fills);
    if (!st->fills) return -1;
  }
  memcpy(st->fills + st->num_fills, cores, sizeof(dd_netobj_character_core) * (size_t)count);
  st->num_fills += count;
  return st->num_fills - count;
}

static bool dd_rc_add_gap(dd_demo_state *st, int cid, int t0, int t1, int quality, int fill) {
  dd_rc_gap_list *list = &st->gaps[cid];
  if (!DD_RC_GROW(list->gaps, list->count, list->capacity, dd_rc_gap)) return false;
  dd_rc_gap *g = &list->gaps[list->count++];
  g->t0 = t0;
  g->t1 = t1;
  g->quality = quality;
  g->fill = fill;
  return true;
}

/* Two re-syncs in the gap: A's prediction until d-1, an input step at d, the
 * new prediction until R_B-1, and a final input step at R_B. */
static bool dd_rc_two_resyncs(dd_demo_state *st, const dd_rc_anchor *a, const dd_rc_anchor *b, int g0, int rb, dd_netobj_character_core *out_cores) {
  dd_netobj_character base_a = dd_rc_anchor_base(a);
  int targets[4][2];
  const int num_targets = dd_rc_targets(&b->chr.core, b, targets);
  for (int d = g0; d < rb; ++d) {
    dd_netobj_character_core before;
    if (!dd_rc_evolve(st, &base_a, d - 1, &before)) return false;
    for (int dir = -1; dir <= 1; ++dir) {
      for (int t = 0; t < num_targets; ++t) {
        for (int jump = 0; jump <= 1; ++jump) {
          for (int hook = 0; hook <= 1; ++hook) {
            dd_world_core world;
            memset(&world, 0, sizeof(world));
            dd_teams_core teams;
            dd_teams_reset(&teams);
            dd_character_core core;
            dd_core_init(&core, &world, &st->col, &teams);
            dd_core_read(&core, &before);
            core.input.direction = dir;
            core.input.target_x = targets[t][0];
            core.input.target_y = targets[t][1];
            core.input.jump = jump;
            core.input.hook = hook;
            dd_core_tick(&core, true, true);
            dd_core_move(&core);
            dd_core_quantize(&core);
            dd_netobj_character mid;
            memset(&mid, 0, sizeof(mid));
            dd_core_write(&core, &mid.core);
            mid.core.m_Tick = d;
            dd_netobj_character_core pre_b;
            if (!dd_rc_evolve(st, &mid, rb - 1, &pre_b)) continue;
            if (!dd_rc_core_step(st, &pre_b, &b->chr.core, b)) continue;
            /* found: fill g0..rb-1 */
            for (int tick = g0; tick < rb; ++tick) {
              if (tick < d) {
                dd_rc_evolve(st, &base_a, tick, &out_cores[tick - g0]);
              } else {
                dd_rc_evolve(st, &mid, tick, &out_cores[tick - g0]);
              }
            }
            return true;
          }
        }
      }
    }
  }
  return false;
}

/* When nothing explains a gap: simulate from A with the most likely input and
 * spread the remaining error to meet B linearly. */
static void dd_rc_approximate(dd_demo_state *st, dd_server *w, int cid, const dd_rc_anchor *a, const dd_rc_anchor *b, int g0, int rb,
                              dd_netobj_character_core *out_cores) {
  dd_netobj_character base_a = dd_rc_anchor_base(a);
  const int n = rb - g0;
  bool simulated = false;
  if (st->have_world) {
    dd_netobj_character_core before, before_prev;
    if (dd_rc_evolve(st, &base_a, g0 - 1, &before)) {
      const bool have_prev = dd_rc_evolve(st, &base_a, g0 - 2, &before_prev);
      dd_player_input input = dd_rc_derive_input(&b->chr.core, b, rb);
      if (dd_rc_seed_world(st, w, g0, cid, &before, have_prev ? &before_prev : NULL, a, &input)) {
        simulated = true;
        for (int tick = g0; tick <= rb && simulated; ++tick) {
          if (tick > g0) {
            /* replay everyone else to their known state */
            for (int other = 0; other < DD_STATE_MAX_CLIENTS; ++other) {
              if (other == cid) continue;
              dd_character *c = dd_chr(w, other);
              dd_netobj_character_core known;
              if (!c || dd_rc_core_at(st, other, tick - 1, &known, NULL, true) == DD_QUALITY_NONE) continue;
              dd_core_read(&c->core, &known);
              dd_chr_set_pos(w, c, c->core.pos);
            }
          }
          for (int other = 0; other < DD_STATE_MAX_CLIENTS; ++other) {
            dd_character *c = dd_chr(w, other);
            if (!c) continue;
            dd_player_input in = input;
            if (other != cid) {
              dd_netobj_character_core next;
              const dd_rc_anchor *next_anchor = NULL;
              if (dd_rc_core_at(st, other, tick, &next, &next_anchor, true) == DD_QUALITY_NONE) continue;
              in = dd_rc_derive_input(&next, next_anchor, tick);
            }
            dd_server_set_input(w, other, &in);
          }
          dd_server_tick(w);
          dd_character *c = dd_chr(w, cid);
          if (!c) {
            simulated = false;
            break;
          }
          if (tick < rb) {
            memset(&out_cores[tick - g0], 0, sizeof(out_cores[0]));
            dd_core_write(&c->core, &out_cores[tick - g0]);
          } else {
            dd_netobj_character_core end;
            memset(&end, 0, sizeof(end));
            dd_core_write(&c->core, &end);
            /* spread the error */
            const int ex = b->chr.core.m_X - end.m_X, ey = b->chr.core.m_Y - end.m_Y;
            const int evx = b->chr.core.m_VelX - end.m_VelX, evy = b->chr.core.m_VelY - end.m_VelY;
            for (int i = 0; i < n; ++i) {
              const float f = (float)(i + 1) / (float)(n + 1);
              out_cores[i].m_X += (int)lroundf(ex * f);
              out_cores[i].m_Y += (int)lroundf(ey * f);
              out_cores[i].m_VelX += (int)lroundf(evx * f);
              out_cores[i].m_VelY += (int)lroundf(evy * f);
            }
          }
        }
      }
    }
  }
  if (!simulated) {
    /* A's prediction, blended into B's re-sync state */
    dd_netobj_character_core end;
    if (!dd_rc_evolve(st, &base_a, rb, &end)) end = b->chr.core;
    for (int i = 0; i < n; ++i) {
      if (!dd_rc_evolve(st, &base_a, g0 + i, &out_cores[i])) out_cores[i] = end;
      const float f = (float)(i + 1) / (float)(n + 1);
      out_cores[i].m_X += (int)lroundf((b->chr.core.m_X - end.m_X) * f);
      out_cores[i].m_Y += (int)lroundf((b->chr.core.m_Y - end.m_Y) * f);
    }
  }
  for (int i = 0; i < n; ++i) out_cores[i].m_Tick = g0 + i;
}

typedef struct {
  int cid, k, rb;
} dd_rc_task;

static int dd_rc_task_cmp(const void *pa, const void *pb) {
  const dd_rc_task *a = (const dd_rc_task *)pa, *b = (const dd_rc_task *)pb;
  if (a->rb != b->rb) return a->rb < b->rb ? -1 : 1;
  return a->cid - b->cid;
}

static bool dd_rc_solve_gaps(dd_demo_state *st) {
  dd_rc_task *tasks = NULL;
  int num_tasks = 0, cap_tasks = 0;
  for (int cid = 0; cid < DD_STATE_MAX_CLIENTS; ++cid) {
    const dd_rc_track *t = &st->tracks[cid];
    for (int k = 0; k + 1 < t->count; ++k) {
      if (!dd_rc_consecutive(t, k)) continue;
      const dd_rc_anchor *a = &t->anchors[k], *b = &t->anchors[k + 1];
      const int rb = b->chr.core.m_Tick;
      if (rb <= 0 || rb == a->chr.core.m_Tick || rb <= a->snap_tick + 1 || rb > b->snap_tick) continue;
      if (!DD_RC_GROW(tasks, num_tasks, cap_tasks, dd_rc_task)) return false;
      tasks[num_tasks].cid = cid;
      tasks[num_tasks].k = k;
      tasks[num_tasks].rb = rb;
      num_tasks++;
    }
  }
  qsort(tasks, (size_t)num_tasks, sizeof(dd_rc_task), dd_rc_task_cmp);

  dd_server w;
  memset(&w, 0, sizeof(w));
  dd_rc_step_template *tpl = (dd_rc_step_template *)calloc(1, sizeof(dd_rc_step_template));
  if (!tpl) {
    free(tasks);
    return false;
  }
  dd_netobj_character_core *cores = NULL;
  int cap_cores = 0;
  bool ok = true;
  dd_rc_task *pending = NULL;
  int num_pending = 0, cap_pending = 0;

  for (int i = 0; i < num_tasks && ok; ++i) {
    /* the joint solver below takes most of the time */
    if (!dd_rc_report(st, 0.05f + 0.25f * (float)i / (float)num_tasks)) {
      ok = false;
      break;
    }
    const int cid = tasks[i].cid;
    const dd_rc_anchor *a = &st->tracks[cid].anchors[tasks[i].k], *b = &st->tracks[cid].anchors[tasks[i].k + 1];
    const int rb = tasks[i].rb, g0 = a->snap_tick + 1, n = rb - g0;
    dd_netobj_character base_a = dd_rc_anchor_base(a);
    dd_netobj_character_core before, before_prev;
    if (cap_cores < n) {
      cap_cores = n * 2;
      cores = (dd_netobj_character_core *)dd_rc_realloc(cores, sizeof(dd_netobj_character_core) * (size_t)cap_cores);
      if (!cores) {
        ok = false;
        break;
      }
    }
    if (!dd_rc_evolve(st, &base_a, rb - 1, &before)) {
      /* A's re-sync is too old to predict from: nothing can be verified, only bridged to B */
      dd_rc_approximate(st, &w, cid, a, b, g0, rb, cores);
      const int fill = dd_rc_add_fills(st, cores, n);
      ok = fill >= 0 && dd_rc_add_gap(st, cid, g0, rb - 1, DD_QUALITY_APPROXIMATED, fill);
      continue;
    }
    const bool have_prev = dd_rc_evolve(st, &base_a, rb - 2, &before_prev);

    /* 1. A's prediction held until R_B - 1: the bare physics, then the full world */
    const bool core_ok = dd_rc_core_step(st, &before, &b->chr.core, b);
#ifdef DD_RC_DIAGNOSTICS
    if (core_ok) {
      dd_rc_diag.core_ok++;
      dd_rc_diag.cross_checked++;
      if (dd_rc_server_step(st, tpl, &w, cid, rb, &before, have_prev ? &before_prev : NULL, a, &b->chr.core, b)) {
        dd_rc_diag.cross_agreed++;
      } else if (dd_rc_diag_dump > 0) {
        dd_rc_diag_dump--;
        dd_player_input in;
        dd_rc_core_step_input(st, &before, &b->chr.core, b, &in);
        dd_rc_prepare_step(st, tpl, cid, rb, &before, have_prev ? &before_prev : NULL, a);
        dd_server_copy(&w, &tpl->world);
        for (int other = 0; other < DD_STATE_MAX_CLIENTS; ++other)
          if (tpl->has_input[other]) dd_server_set_input(&w, other, &tpl->input[other]);
        dd_server_set_input(&w, cid, &in);
        dd_character *pre = dd_chr(&w, cid);
        const int zone = pre ? pre->tune_zone : -1;
        {
          dd_tuning def;
          dd_tuning_default(&def);
          const int *tw = (const int *)&w.tuning[zone < 0 ? 0 : zone], *td = (const int *)&def;
          for (int k = 0; k < DD_TUNING_NUM; ++k)
            if (tw[k] != td[k]) fprintf(stderr, "  diag tuning %s = %d (default %d)\n", dd_tuning_names[k], tw[k], td[k]);
        }
        const int idx = dd_col_get_pure_map_index(&st->col, (float)before.m_X, (float)before.m_Y);
        dd_server_tick(&w);
        dd_character *c = dd_chr(&w, cid);
        fprintf(stderr, "  diag cid %d tick %d zone %d tile %d front %d switch %d tele %d speedup %d freeze_time %d | in dir %d jump %d hook %d t(%d,%d)\n",
                cid, rb, zone, st->col.tiles[idx], st->col.front ? st->col.front[idx] : -1, st->col.switch_type ? st->col.switch_type[idx] : -1,
                st->col.tele_type ? st->col.tele_type[idx] : -1, st->col.speedup_force ? st->col.speedup_force[idx] : -1, c ? c->freeze_time : -1,
                in.direction, in.jump, in.hook, in.target_x, in.target_y);
        if (c) {
          dd_netobj_character_core out;
          memset(&out, 0, sizeof(out));
          dd_core_write(&c->core, &out);
          out.m_Direction = c->input.direction;
          static const char *const names[] = {"tick", "x", "y", "vx", "vy", "angle", "dir", "jumped", "hooked", "hstate", "htick", "hx", "hy", "hdx", "hdy"};
          const int *o = (const int *)&out, *e = (const int *)&b->chr.core, *p0 = (const int *)&before;
          fprintf(stderr, "       ");
          for (int f = 1; f < 15; ++f)
            if (o[f] != e[f] && f != 5) fprintf(stderr, " %s before %d server %d demo %d;", names[f], p0[f], o[f], e[f]);
          fprintf(stderr, "\n");
        } else {
          fprintf(stderr, "       character died in the server world\n");
        }
      }
    }
#endif
    if (core_ok || dd_rc_server_step(st, tpl, &w, cid, rb, &before, have_prev ? &before_prev : NULL, a, &b->chr.core, b)) {
#ifdef DD_RC_DIAGNOSTICS
      if (!core_ok) dd_rc_diag.server_ok++;
#endif
      ok = dd_rc_add_gap(st, cid, g0, rb - 1, DD_QUALITY_VERIFIED, -1);
      continue;
    }
    /* 2. two re-syncs */
    if (n <= 8 && dd_rc_two_resyncs(st, a, b, g0, rb, cores)) {
#ifdef DD_RC_DIAGNOSTICS
      dd_rc_diag.two_resyncs++;
#endif
      int fill = dd_rc_add_fills(st, cores, n);
      ok = fill >= 0 && dd_rc_add_gap(st, cid, g0, rb - 1, DD_QUALITY_VERIFIED, fill);
      continue;
    }
    /* 3. short stretches go to the joint solver below */
    if (rb - a->snap_tick <= DD_RC_JOINT_MAX_TICKS && st->have_world) {
      if (!DD_RC_GROW(pending, num_pending, cap_pending, dd_rc_task)) {
        ok = false;
        break;
      }
      pending[num_pending++] = tasks[i];
      continue;
    }
    /* 4. simulate and correct */
#ifdef DD_RC_DIAGNOSTICS
    dd_rc_diag.approximated++;
    if (dd_rc_diag_approx_dump > 0 && st->have_world) {
      dd_rc_diag_approx_dump--;
      /* the closest single-step candidate, to see what is missing */
      dd_rc_prepare_step(st, tpl, cid, rb, &before, have_prev ? &before_prev : NULL, a);
      int targets[4][2];
      const int num_targets = dd_rc_targets(&b->chr.core, b, targets);
      long best_score = -1;
      dd_netobj_character_core best;
      dd_player_input best_in;
      memset(&best, 0, sizeof(best));
      memset(&best_in, 0, sizeof(best_in));
      for (int tt = 0; tt < num_targets; ++tt)
        for (int jump = 0; jump <= 1; ++jump)
          for (int hook = 0; hook <= 1; ++hook) {
            dd_player_input in;
            memset(&in, 0, sizeof(in));
            in.direction = b->chr.core.m_Direction;
            in.target_x = targets[tt][0];
            in.target_y = targets[tt][1];
            in.jump = jump;
            in.hook = hook;
            dd_server_copy(&w, &tpl->world);
            for (int other = 0; other < DD_STATE_MAX_CLIENTS; ++other)
              if (tpl->has_input[other]) dd_server_set_input(&w, other, &tpl->input[other]);
            dd_server_set_input(&w, cid, &in);
            dd_server_tick(&w);
            dd_character *c = dd_chr(&w, cid);
            if (!c) continue;
            dd_netobj_character_core out;
            memset(&out, 0, sizeof(out));
            dd_core_write(&c->core, &out);
            out.m_Direction = c->input.direction;
            long score = 0;
            const int *o = (const int *)&out, *e = (const int *)&b->chr.core;
            for (int f = 1; f < 15; ++f)
              if (f != 5) score += f <= 4 ? dd_absi(o[f] - e[f]) : (o[f] != e[f]) * 1000;
            if (best_score < 0 || score < best_score) {
              best_score = score;
              best = out;
              best_in = in;
            }
          }
      int nearest = -1, nearest_dist = 1 << 30;
      for (int other = 0; other < DD_STATE_MAX_CLIENTS; ++other) {
        dd_character *oc = other == cid ? NULL : dd_chr(&tpl->world, other);
        if (!oc) continue;
        const int d = dd_absi((int)oc->core.pos.x - before.m_X) + dd_absi((int)oc->core.pos.y - before.m_Y);
        if (d < nearest_dist) {
          nearest_dist = d;
          nearest = other;
        }
      }
      const int idx = dd_col_get_pure_map_index(&st->col, (float)before.m_X, (float)before.m_Y);
      dd_character *pre = dd_chr(&tpl->world, cid);
      fprintf(stderr, "  approx cid %d ticks %d..%d (n %d) tile %d front %d freeze_time %d nearest tee %d at %d | best in dir %d jump %d hook %d\n",
              cid, g0, rb - 1, n, st->col.tiles[idx], st->col.front ? st->col.front[idx] : -1, pre ? pre->freeze_time : -1, nearest,
              nearest_dist, best_in.direction, best_in.jump, best_in.hook);
      static const char *const names[] = {"tick", "x", "y", "vx", "vy", "angle", "dir", "jumped", "hooked", "hstate", "htick", "hx", "hy", "hdx", "hdy"};
      const int *o = (const int *)&best, *e = (const int *)&b->chr.core, *p0 = (const int *)&before;
      fprintf(stderr, "       ");
      for (int f = 1; f < 15; ++f)
        if (o[f] != e[f] && f != 5) fprintf(stderr, " %s before %d best %d demo %d;", names[f], p0[f], o[f], e[f]);
      fprintf(stderr, "\n");
    }
#endif
    dd_rc_approximate(st, &w, cid, a, b, g0, rb, cores);
    int fill = dd_rc_add_fills(st, cores, n);
    ok = fill >= 0 && dd_rc_add_gap(st, cid, g0, rb - 1, DD_QUALITY_APPROXIMATED, fill);
  }

  /* the joint solver: pending gaps grouped by the snapshot they start after */
  if (ok && num_pending > 0) {
    dd_server start;
    memset(&start, 0, sizeof(start));
    dd_rc_joint_member *members = (dd_rc_joint_member *)calloc(DD_RC_JOINT_MAX, sizeof(dd_rc_joint_member));
    dd_rc_joint_checkpoint *cp = (dd_rc_joint_checkpoint *)calloc(1, sizeof(dd_rc_joint_checkpoint));
    if (!members || !cp) ok = false;
    for (int i = 0; i < num_pending && ok;) {
      if (!dd_rc_report(st, 0.3f + 0.7f * (float)i / (float)num_pending)) {
        ok = false;
        break;
      }
      const int snap_index = st->tracks[pending[i].cid].anchors[pending[i].k].snap_index;
      int j = i;
      int count = 0;
      /* more than DD_RC_JOINT_MAX after one snapshot are solved in several groups */
      while (j < num_pending && count < DD_RC_JOINT_MAX && st->tracks[pending[j].cid].anchors[pending[j].k].snap_index == snap_index) {
        dd_rc_joint_member *m = &members[count++];
        memset(m, 0, sizeof(*m));
        m->cid = pending[j].cid;
        m->k = pending[j].k;
        m->rb = pending[j].rb;
        m->a = &st->tracks[m->cid].anchors[m->k];
        m->b = &st->tracks[m->cid].anchors[m->k + 1];
        m->score = -1; /* no run yet: a solver that gives up early leaves no state to trust */
        j++;
      }
      const int ta = st->snaps[snap_index].tick;
      dd_rc_joint_solve(st, &start, &w, ta, members, count, cp);
      for (int x = 0; x < count && ok; ++x) {
        dd_rc_joint_member *m = &members[x];
        const int g0 = ta + 1, n = m->rb - g0;
        if (n <= 0) continue;
        if (m->score < 0) {
          /* the solver gave up before simulating anything */
          if (cap_cores < n) {
            cap_cores = n * 2;
            cores = (dd_netobj_character_core *)dd_rc_realloc(cores, sizeof(dd_netobj_character_core) * (size_t)cap_cores);
            if (!cores) {
              ok = false;
              break;
            }
          }
          dd_rc_approximate(st, &w, m->cid, m->a, m->b, g0, m->rb, cores);
          const int fill = dd_rc_add_fills(st, cores, n);
          ok = fill >= 0 && dd_rc_add_gap(st, m->cid, g0, m->rb - 1, DD_QUALITY_APPROXIMATED, fill);
        } else if (m->score == 0) {
#ifdef DD_RC_DIAGNOSTICS
          dd_rc_diag.joint_ok++;
#endif
          int fill = dd_rc_add_fills(st, m->sim, n);
          ok = fill >= 0 && dd_rc_add_gap(st, m->cid, g0, m->rb - 1, DD_QUALITY_VERIFIED, fill);
        } else {
#ifdef DD_RC_DIAGNOSTICS
          dd_rc_diag.approximated++;
          if (dd_rc_diag_joint_dump > 0) {
            dd_rc_diag_joint_dump--;
            static const char *const names[] = {"tick", "x", "y", "vx", "vy", "angle", "dir", "jumped", "hooked", "hstate", "htick", "hx", "hy", "hdx", "hdy"};
            const int *o = (const int *)&m->sim[n], *e = (const int *)&m->b->chr.core, *p0 = (const int *)&m->a->chr.core;
            const int idx = dd_col_get_pure_map_index(&st->col, (float)m->a->chr.core.m_X, (float)m->a->chr.core.m_Y);
            fprintf(stderr, "  joint cid %d ticks %d..%d (n %d, members %d) tile %d weapon %d attack %d ddnet %d freeze_end %d |", m->cid, g0,
                    m->rb - 1, n, count, st->col.tiles[idx], m->b->chr.m_Weapon, m->b->chr.m_AttackTick, m->b->has_ddnet,
                    m->b->has_ddnet ? m->b->ddnet.m_FreezeEnd : 0);
            for (int f = 1; f < 15; ++f)
              if (o[f] != e[f] && f != 5) fprintf(stderr, " %s a %d sim %d demo %d;", names[f], p0[f], o[f], e[f]);
            fprintf(stderr, "\n");
          }
#endif
          /* the closest run, corrected to meet B */
          const dd_netobj_character_core *end = &m->sim[n];
          const dd_netobj_character_core *want = &m->b->chr.core;
          for (int t = 0; t < n; ++t) {
            const float f = (float)(t + 1) / (float)(n + 1);
            m->sim[t].m_X += (int)lroundf((want->m_X - end->m_X) * f);
            m->sim[t].m_Y += (int)lroundf((want->m_Y - end->m_Y) * f);
            m->sim[t].m_VelX += (int)lroundf((want->m_VelX - end->m_VelX) * f);
            m->sim[t].m_VelY += (int)lroundf((want->m_VelY - end->m_VelY) * f);
          }
          int fill = dd_rc_add_fills(st, m->sim, n);
          ok = fill >= 0 && dd_rc_add_gap(st, m->cid, g0, m->rb - 1, DD_QUALITY_APPROXIMATED, fill);
        }
      }
      i = j;
    }
    free(members);
    if (cp) dd_server_free(&cp->world);
    free(cp);
    dd_server_free(&start);
  }
  free(pending);

  /* gap lists are appended in tick order per character, but tasks were
   * interleaved; sort each list by start tick */
  for (int cid = 0; cid < DD_STATE_MAX_CLIENTS && ok; ++cid) {
    dd_rc_gap_list *list = &st->gaps[cid];
    for (int x = 1; x < list->count; ++x)
      for (int y = x; y > 0 && list->gaps[y].t0 < list->gaps[y - 1].t0; --y) {
        dd_rc_gap tmp = list->gaps[y];
        list->gaps[y] = list->gaps[y - 1];
        list->gaps[y - 1] = tmp;
      }
  }
  free(cores);
  free(tasks);
  dd_server_free(&w);
  dd_server_free(&tpl->world);
  free(tpl);
  return ok;
}

static void dd_rc_count_stats(dd_demo_state *st) {
  memset(&st->stats, 0, sizeof(st->stats));
  for (int tick = st->first_tick; tick <= st->last_tick; ++tick) {
    for (int cid = 0; cid < DD_STATE_MAX_CLIENTS; ++cid) {
      if (st->tracks[cid].count == 0) continue;
      int q = dd_rc_core_at(st, cid, tick, NULL, NULL, false);
      if (q == DD_QUALITY_NONE) continue;
      st->stats.character_ticks++;
      if (q == DD_QUALITY_RECORDED) st->stats.recorded++;
      else if (q == DD_QUALITY_EXACT) st->stats.exact++;
      else if (q == DD_QUALITY_VERIFIED) st->stats.verified++;
      else st->stats.approximated++;
    }
  }
}


/* ======================================================================== */
/* who things belong to */

/* The tuning in effect at `tick` in tune zone `zone`. Without tune zones the
 * server's tune messages are the whole map's tuning, as when solving
 * (dd_rc_seed_world). */
static void dd_rc_tuning_at(const dd_demo_state *st, int tick, int zone, dd_tuning *out) {
  if (zone < 0 || zone >= DD_NUM_TUNEZONES) zone = 0;
  if (st->have_world)
    *out = st->base.tuning[zone];
  else
    dd_tuning_default(out);
  if (zone == 0 && !st->col.tune_type)
    for (int i = st->num_tunes - 1; i >= 0; --i)
      if (st->tunes[i].tick <= tick) {
        *out = st->tunes[i].tuning;
        break;
      }
}

/* DDNet's CalcPos: where a projectile is at `tick`. */
static dd_vec2 dd_rc_projectile_pos(const dd_demo_state *st, const dd_state_projectile *p, int tick) {
  dd_tuning t;
  dd_rc_tuning_at(st, tick, p->tune_zone, &t);
  float curvature = 0.f, speed = 0.f;
  if (p->type == DD_WEAPON_ID_GRENADE) {
    curvature = dd_tune(t.grenade_curvature);
    speed = dd_tune(t.grenade_speed);
  } else if (p->type == DD_WEAPON_ID_SHOTGUN) {
    curvature = dd_tune(t.shotgun_curvature);
    speed = dd_tune(t.shotgun_speed);
  } else if (p->type == DD_WEAPON_ID_GUN) {
    curvature = dd_tune(t.gun_curvature);
    speed = dd_tune(t.gun_speed);
  }
  const float time = (float)(tick - p->start_tick) / (float)DD_SERVER_TICK_SPEED * speed;
  return dd_v2(p->x + p->vel_x * time, p->y + p->vel_y * time + curvature / 10000.f * time * time);
}

/* The client whose tee is nearest to (x, y) at `tick`, within `radius`, or -1. */
static int dd_rc_nearest_tee(const dd_demo_state *st, int tick, dd_vec2 at, float radius) {
  int best = -1;
  float best_distance = radius;
  for (int cid = 0; cid < DD_STATE_MAX_CLIENTS; ++cid) {
    if (st->tracks[cid].count == 0) continue;
    dd_netobj_character_core core;
    if (dd_rc_core_at(st, cid, tick, &core, NULL, true) == DD_QUALITY_NONE) continue;
    const float distance = dd_v2_distance(dd_v2((float)core.m_X, (float)core.m_Y), at);
    if (distance < best_distance) {
      best_distance = distance;
      best = cid;
    }
  }
  return best;
}

/* The same, at a snapshot's tick or the one before: a snapshot carries both ticks' events. */
static int dd_rc_nearest_tee_around(const dd_demo_state *st, int tick, dd_vec2 at, float radius) {
  const int now = dd_rc_nearest_tee(st, tick, at, radius);
  return now >= 0 ? now : dd_rc_nearest_tee(st, tick - 1, at, radius);
}

/* The same entity in the previous snapshot: same item id, and for projectiles the same shot. */
static int dd_rc_previous_projectile(const dd_demo_state *st, const dd_rc_snap *prev, int id, const dd_state_projectile *p) {
  for (int k = 0; prev && k < prev->num_proj; ++k) {
    const dd_state_projectile *q = &st->projectiles[prev->proj0 + k];
    if (st->projectile_ids[prev->proj0 + k] == id && q->start_tick == p->start_tick && q->type == p->type) return prev->proj0 + k;
  }
  return -1;
}

static int dd_rc_previous_laser(const dd_demo_state *st, const dd_rc_snap *prev, int id) {
  for (int k = 0; prev && k < prev->num_laser; ++k)
    if (st->laser_ids[prev->laser0 + k] == id) return prev->laser0 + k;
  return -1;
}

/* Fills in the owners the demo did not send. Shots belong to the tee they
 * started at (DDNet fires them from 3/4 of a tee's radius off its centre),
 * and keep that owner for as long as the entity lives. Explosions belong to
 * the grenade that burst there, other events to the nearest tee, else to a
 * shot passing by. What cannot be told belongs to the world. */
static void dd_rc_attribute_owners(dd_demo_state *st) {
  for (int si = 0; si < st->num_snaps; ++si) {
    const dd_rc_snap *s = &st->snaps[si];
    const dd_rc_snap *prev = si > 0 ? &st->snaps[si - 1] : NULL;
    for (int k = 0; k < s->num_proj; ++k) {
      dd_state_projectile *p = &st->projectiles[s->proj0 + k];
      if (p->owner != DD_RC_OWNER_UNKNOWN) continue;
      const int before = dd_rc_previous_projectile(st, prev, st->projectile_ids[s->proj0 + k], p);
      if (before >= 0) {
        p->owner = st->projectiles[before].owner;
        continue;
      }
      int owner = dd_rc_nearest_tee(st, p->start_tick, dd_v2(p->x, p->y), 48.f);
      if (owner < 0) owner = dd_rc_nearest_tee(st, p->start_tick - 1, dd_v2(p->x, p->y), 48.f);
      p->owner = owner;
    }
    for (int k = 0; k < s->num_laser; ++k) {
      dd_state_laser *l = &st->lasers[s->laser0 + k];
      if (l->owner != DD_RC_OWNER_UNKNOWN) continue;
      /* a bounce starts a new segment of the same laser, far from its shooter */
      const int before = dd_rc_previous_laser(st, prev, st->laser_ids[s->laser0 + k]);
      if (before >= 0) {
        l->owner = st->lasers[before].owner;
        continue;
      }
      int owner = dd_rc_nearest_tee(st, l->start_tick, dd_v2(l->from_x, l->from_y), 48.f);
      if (owner < 0) owner = dd_rc_nearest_tee(st, l->start_tick - 1, dd_v2(l->from_x, l->from_y), 48.f);
      l->owner = owner;
    }
    for (int k = 0; k < s->num_event; ++k) {
      dd_state_event *e = &st->events[s->event0 + k];
      if (e->owner != DD_RC_OWNER_UNKNOWN) continue;
      const dd_vec2 at = dd_v2(e->x, e->y);
      int owner = -1;
      if (e->type == DD_STATE_EVENT_DEATH) {
        owner = e->client_id >= 0 && e->client_id < DD_STATE_MAX_CLIENTS ? e->client_id : -1;
      } else if (e->type == DD_STATE_EVENT_EXPLOSION) {
        /* a grenade of the snapshot before that is gone now, near enough to have burst here */
        float best = 96.f;
        for (int q = 0; prev && q < prev->num_proj; ++q) {
          const dd_state_projectile *g = &st->projectiles[prev->proj0 + q];
          if (g->type != DD_WEAPON_ID_GRENADE && !g->explosive) continue;
          bool still_there = false;
          for (int c = 0; c < s->num_proj && !still_there; ++c)
            still_there = st->projectile_ids[s->proj0 + c] == st->projectile_ids[prev->proj0 + q] &&
                          st->projectiles[s->proj0 + c].start_tick == g->start_tick;
          if (still_there) continue;
          for (int tick = prev->tick; tick <= s->tick; ++tick) {
            const float distance = dd_v2_distance(dd_rc_projectile_pos(st, g, tick), at);
            if (distance < best) {
              best = distance;
              owner = g->owner;
            }
          }
        }
        /* a grenade that burst before any snapshot showed it: a rocket jump, next to its shooter */
        if (owner < 0) owner = dd_rc_nearest_tee_around(st, s->tick, at, 96.f);
      } else {
        owner = dd_rc_nearest_tee_around(st, s->tick, at, 64.f);
        if (owner < 0) {
          float best = 64.f;
          for (int q = 0; q < s->num_proj; ++q) {
            const dd_state_projectile *p = &st->projectiles[s->proj0 + q];
            const float distance = dd_v2_distance(dd_rc_projectile_pos(st, p, s->tick), at);
            if (distance < best) {
              best = distance;
              owner = p->owner;
            }
          }
        }
      }
      e->owner = owner;
    }
  }
}

/* ======================================================================== */
/* public API */

/* ---- the reconstruction cache ----
 * What solving produces (every character's gaps and the cores filled into
 * them), tied to the demo's bytes. Bump the version whenever solving can
 * produce a different result, so stale caches are rebuilt. */
#define DD_RC_CACHE_MAGIC "DDRCACHE"
#define DD_RC_CACHE_VERSION 3u /* 3: closest points clamp to the segment, like DDNet */

static uint64_t dd_rc_file_hash(const char *path, bool *ok) {
  uint64_t h = 1469598103934665603ULL; /* FNV-1a */
  *ok = false;
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  uint8_t buffer[65536];
  size_t n;
  while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0)
    for (size_t i = 0; i < n; ++i) {
      h ^= buffer[i];
      h *= 1099511628211ULL;
    }
  *ok = !ferror(f);
  fclose(f);
  return h;
}

typedef struct {
  char magic[8];
  uint32_t version, core_size, gap_size, max_clients;
  uint64_t demo_hash;
  int32_t num_fills;
} dd_rc_cache_header;

static void dd_rc_cache_header_init(dd_rc_cache_header *h, uint64_t demo_hash, int num_fills) {
  memset(h, 0, sizeof(*h));
  memcpy(h->magic, DD_RC_CACHE_MAGIC, 8);
  h->version = DD_RC_CACHE_VERSION;
  h->core_size = (uint32_t)sizeof(dd_netobj_character_core);
  h->gap_size = (uint32_t)sizeof(dd_rc_gap);
  h->max_clients = DD_STATE_MAX_CLIENTS;
  h->demo_hash = demo_hash;
  h->num_fills = num_fills;
}

static bool dd_rc_cache_write(const dd_demo_state *st, const char *path, uint64_t demo_hash) {
  /* written next to the target and renamed, so a crash never leaves half a cache */
  char temp[4096];
  if (snprintf(temp, sizeof(temp), "%s.tmp", path) >= (int)sizeof(temp)) return false;
  FILE *f = fopen(temp, "wb");
  if (!f) return false;
  dd_rc_cache_header h;
  dd_rc_cache_header_init(&h, demo_hash, st->num_fills);
  bool ok = fwrite(&h, sizeof(h), 1, f) == 1;
  if (ok && st->num_fills) ok = fwrite(st->fills, sizeof(*st->fills), (size_t)st->num_fills, f) == (size_t)st->num_fills;
  for (int cid = 0; cid < DD_STATE_MAX_CLIENTS && ok; ++cid) {
    const int32_t count = st->gaps[cid].count;
    ok = fwrite(&count, sizeof(count), 1, f) == 1;
    if (ok && count) ok = fwrite(st->gaps[cid].gaps, sizeof(dd_rc_gap), (size_t)count, f) == (size_t)count;
  }
  ok = fclose(f) == 0 && ok;
  if (ok) {
    remove(path);
    ok = rename(temp, path) == 0;
  }
  if (!ok) remove(temp);
  return ok;
}

/* Fills the gaps from the cache; false (with nothing changed) when it is
 * missing, stale or broken. */
static bool dd_rc_cache_read(dd_demo_state *st, const char *path, uint64_t demo_hash) {
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  dd_rc_cache_header h, want;
  bool ok = fread(&h, sizeof(h), 1, f) == 1;
  if (ok) {
    dd_rc_cache_header_init(&want, demo_hash, h.num_fills);
    ok = memcmp(&h, &want, sizeof(h)) == 0 && h.num_fills >= 0;
  }
  dd_netobj_character_core *fills = NULL;
  dd_rc_gap_list gaps[DD_STATE_MAX_CLIENTS];
  memset(gaps, 0, sizeof(gaps));
  if (ok && h.num_fills) {
    fills = (dd_netobj_character_core *)malloc(sizeof(*fills) * (size_t)h.num_fills);
    ok = fills && fread(fills, sizeof(*fills), (size_t)h.num_fills, f) == (size_t)h.num_fills;
  }
  for (int cid = 0; cid < DD_STATE_MAX_CLIENTS && ok; ++cid) {
    int32_t count = 0;
    ok = fread(&count, sizeof(count), 1, f) == 1 && count >= 0 && count <= 100000000;
    if (!ok || !count) continue;
    gaps[cid].gaps = (dd_rc_gap *)malloc(sizeof(dd_rc_gap) * (size_t)count);
    ok = gaps[cid].gaps && fread(gaps[cid].gaps, sizeof(dd_rc_gap), (size_t)count, f) == (size_t)count;
    gaps[cid].count = gaps[cid].capacity = count;
    /* every filled gap must point inside the fills */
    for (int g = 0; g < count && ok; ++g) {
      const dd_rc_gap *gap = &gaps[cid].gaps[g];
      ok = gap->t0 <= gap->t1 && (gap->fill < 0 || (int64_t)gap->fill + (gap->t1 - gap->t0) < (int64_t)h.num_fills);
    }
  }
  if (ok) ok = fgetc(f) == EOF; /* nothing trailing */
  fclose(f);
  if (!ok) {
    free(fills);
    for (int cid = 0; cid < DD_STATE_MAX_CLIENTS; ++cid) free(gaps[cid].gaps);
    return false;
  }
  free(st->fills);
  st->fills = fills;
  st->num_fills = st->cap_fills = h.num_fills;
  for (int cid = 0; cid < DD_STATE_MAX_CLIENTS; ++cid) {
    free(st->gaps[cid].gaps);
    st->gaps[cid] = gaps[cid];
  }
  return true;
}

dd_demo_state *dd_demo_state_load(const char *path, char *error, size_t error_size) { return dd_demo_state_load_ex(path, NULL, error, error_size); }

dd_demo_state *dd_demo_state_load_ex(const char *path, const dd_state_load_options *options, char *error, size_t error_size) {
  char dummy[8];
  if (!error || !error_size) {
    error = dummy;
    error_size = sizeof(dummy);
  }
  error[0] = 0;
  FILE *f = fopen(path, "rb");
  if (!f) {
    snprintf(error, error_size, "cannot open %s", path);
    return NULL;
  }
  dd_demo_state *st = (dd_demo_state *)calloc(1, sizeof(dd_demo_state));
  if (!st) {
    fclose(f);
    snprintf(error, error_size, "out of memory");
    return NULL;
  }
  if (options) {
    st->progress = options->progress;
    st->progress_user = options->progress_user;
  }
  st->progress_reported = -1.0f;
  dd_rc_report(st, 0.0f);
  const bool read = dd_rc_read_demo(st, f, error, error_size);
  fclose(f);
  if (!read) {
    dd_demo_state_free(st);
    return NULL;
  }

  if (st->map_bytes && st->map_size) {
    /* the loader takes ownership of its buffer */
    uint8_t *copy = (uint8_t *)malloc(st->map_size);
    if (copy) {
      memcpy(copy, st->map_bytes, st->map_size);
      st->map = load_map_from_memory(copy, st->map_size);
      st->have_map = dd_collision_init(&st->col, &st->map);
      if (st->have_map) st->have_world = dd_server_init(&st->base, &st->col, st->map.settings, st->map.num_settings);
    }
  }
  if (!st->have_map) {
    snprintf(error, error_size, "the demo's map could not be loaded");
    dd_demo_state_free(st);
    return NULL;
  }
  if (!dd_rc_report(st, 0.05f)) {
    snprintf(error, error_size, "cancelled");
    dd_demo_state_free(st);
    return NULL;
  }

  const char *cache_path = options && options->cache_path && options->cache_path[0] ? options->cache_path : NULL;
  bool hashed = false;
  const uint64_t demo_hash = cache_path ? dd_rc_file_hash(path, &hashed) : 0;
  if (!(hashed && dd_rc_cache_read(st, cache_path, demo_hash))) {
    if (!dd_rc_solve_gaps(st)) {
      snprintf(error, error_size, st->cancelled ? "cancelled" : "out of memory");
      dd_demo_state_free(st);
      return NULL;
    }
    if (hashed) dd_rc_cache_write(st, cache_path, demo_hash); /* only a cache: failing to write it is not an error */
  }
  dd_rc_count_stats(st);
  dd_rc_attribute_owners(st);
  dd_rc_report(st, 1.0f);
  st->progress = NULL; /* the caller's callback is only valid during the load */
  return st;
}

void dd_demo_state_free(dd_demo_state *st) {
  if (!st) return;
  free(st->map_bytes);
  if (st->have_world) dd_server_free(&st->base);
  dd_collision_free(&st->col);
  free_map_data(&st->map);
  free(st->snaps);
  for (int i = 0; i < DD_STATE_MAX_CLIENTS; ++i) {
    free(st->tracks[i].anchors);
    free(st->gaps[i].gaps);
    free(st->players[i].entries);
  }
  free(st->fills);
  free(st->projectiles);
  free(st->lasers);
  free(st->projectile_ids);
  free(st->laser_ids);
  free(st->pickups);
  free(st->events);
  free(st->messages);
  free(st->message_offsets);
  free(st->text_pool);
  free(st->raw_pool);
  free(st->tunes);
  free(st->teams);
  free(st);
}

int dd_demo_state_first_tick(const dd_demo_state *st) { return st->first_tick; }
int dd_demo_state_last_tick(const dd_demo_state *st) { return st->last_tick; }

/* An anchor's aim as DDNet's client reads it: the target when the server sent
 * one, else the angle (in 1/256 radians). */
typedef struct {
  bool target;
  float x, y, angle;
} dd_rc_aim;

static dd_rc_aim dd_rc_anchor_aim_raw(const dd_rc_anchor *a) {
  dd_rc_aim aim = {a->has_ddnet, 0.f, 0.f, (float)a->chr.core.m_Angle};
  if (aim.target) {
    aim.x = (float)a->ddnet.m_TargetX;
    aim.y = (float)a->ddnet.m_TargetY;
  }
  return aim;
}

static bool dd_rc_aim_equal(dd_rc_aim a, dd_rc_aim b) {
  if (a.target != b.target) return false;
  return a.target ? a.x == b.x && a.y == b.y : a.angle == b.angle;
}

/* The aim of anchor k, with late inputs repaired. The server snaps the input
 * it applied on a tick, and when a player's input arrives late it applies the
 * previous one again, so a moving aim stands still for one snapshot and then
 * jumps ahead. A snapshot that repeats the aim before it exactly, while the
 * aim moved just before and moves on after, is taken to be such a late input
 * and put where the aim would have been between its neighbours. A stop that
 * lasts longer than one snapshot is left alone. */
static dd_rc_aim dd_rc_anchor_aim(const dd_rc_track *t, int k) {
  const dd_rc_aim aim = dd_rc_anchor_aim_raw(&t->anchors[k]);
  if (k < 2 || k + 1 >= t->count) return aim;
  const dd_rc_anchor *p2 = &t->anchors[k - 2], *p = &t->anchors[k - 1], *c = &t->anchors[k], *n = &t->anchors[k + 1];
  if (p2->snap_index + 1 != p->snap_index || p->snap_index + 1 != c->snap_index || c->snap_index + 1 != n->snap_index) return aim;
  const dd_rc_aim before2 = dd_rc_anchor_aim_raw(p2), before = dd_rc_anchor_aim_raw(p), after = dd_rc_anchor_aim_raw(n);
  if (before2.target != aim.target || before.target != aim.target || after.target != aim.target) return aim;
  if (!dd_rc_aim_equal(aim, before) || dd_rc_aim_equal(after, aim) || dd_rc_aim_equal(before, before2)) return aim;
  const float f = (float)(c->snap_tick - p->snap_tick) / (float)(n->snap_tick - p->snap_tick);
  dd_rc_aim repaired = aim;
  if (aim.target) {
    repaired.x = before.x + (after.x - before.x) * f;
    repaired.y = before.y + (after.y - before.y) * f;
  } else {
    const float pi256 = 256.f * 3.14159265358979f;
    float d = after.angle - before.angle;
    while (d > pi256) d -= 2.f * pi256;
    while (d < -pi256) d += 2.f * pi256;
    repaired.angle = before.angle + d * f;
  }
  return repaired;
}

/* CPlayers::GetPlayerTargetAngle in demo playback: the aim mixed between the
 * snapshot at or before `tick` and the next one, at `tick`'s share of the time
 * between them. The target is mixed when both carry one, else the angle, the
 * short way round. Late inputs are repaired first (dd_rc_anchor_aim), which
 * DDNet does not do. Returned as a vector: the target itself, or 256 long. */
static void dd_rc_display_aim(const dd_demo_state *st, int cid, int tick, float *out_x, float *out_y) {
  const dd_rc_track *t = &st->tracks[cid];
  int k = dd_rc_anchor_at(t, tick);
  if (k < 0) k = 0;
  const dd_rc_anchor *a = &t->anchors[k];
  const bool next = dd_rc_consecutive(t, k) && tick > a->snap_tick;
  const dd_rc_anchor *b = next ? &t->anchors[k + 1] : a;
  const float intra = next ? (float)(tick - a->snap_tick) / (float)(b->snap_tick - a->snap_tick) : 0.f;
  const dd_rc_aim aim_a = dd_rc_anchor_aim(t, k), aim_b = next ? dd_rc_anchor_aim(t, k + 1) : aim_a;
  if (aim_a.target && aim_b.target) {
    *out_x = aim_a.x + (aim_b.x - aim_a.x) * intra;
    *out_y = aim_a.y + (aim_b.y - aim_a.y) * intra;
    return;
  }
  if (aim_b.target) {
    /* DDNet without a previous target uses the current one as is */
    *out_x = aim_b.x;
    *out_y = aim_b.y;
    return;
  }
  const float pi256 = 256.f * 3.14159265358979f;
  float from = aim_a.target ? (float)a->chr.core.m_Angle : aim_a.angle, to = aim_b.angle;
  if (to > pi256 && from < 0) to -= 2.f * pi256;
  else if (to < 0 && from > pi256) to += 2.f * pi256;
  const float angle = (from + (to - from) * intra) / 256.f;
  *out_x = cosf(angle) * 256.f;
  *out_y = sinf(angle) * 256.f;
}

/* One character's state at `tick`; quality NONE when it is not there. */
/* The extras of a tick come from the last snapshot at or before it, but the
 * core does not: the server re-syncs it on the tick it changes, and that can
 * lie before the next snapshot. A tee frozen since before `tick` has its input
 * cleared, so its core shows no direction and no hook; when it shows either,
 * it was unfrozen since those extras were sent (a hammer, most often, fired
 * from a tee that ticks first), and the next snapshot has its freeze as it is. */
/* Whether HandleTiles unfroze the tee at `tick`: it walks the tiles the
 * previous tick's move crossed, from the position two ticks back to the one a
 * tick back, after the core ticked (CCharacter::DDRacePostCoreTick). */
static bool dd_rc_crossed_unfreeze(dd_demo_state *st, int cid, int tick) {
  if (!st->have_world) return false;
  dd_netobj_character_core from, to;
  if (dd_rc_core_at(st, cid, tick - 2, &from, NULL, true) == DD_QUALITY_NONE ||
      dd_rc_core_at(st, cid, tick - 1, &to, NULL, true) == DD_QUALITY_NONE)
    return false;
  static int indices[DD_MAX_MAP_INDICES];
  const dd_vec2 to_pos = dd_v2((float)to.m_X, (float)to.m_Y);
  int count = dd_col_get_map_indices(&st->col, dd_v2((float)from.m_X, (float)from.m_Y), to_pos, indices);
  if (count == 0) {
    indices[0] = dd_col_get_pure_map_index(&st->col, to_pos.x, to_pos.y);
    count = 1;
  }
  for (int i = 0; i < count; ++i)
    if (dd_col_get_tile_index(&st->col, indices[i]) == DD_TILE_UNFREEZE || dd_col_get_front_tile_index(&st->col, indices[i]) == DD_TILE_UNFREEZE)
      return true;
  return false;
}

/* The extras of a tick come from the last snapshot at or before it, while the
 * tee may have been unfrozen since; the next snapshot has its freeze as it is.
 * A tee frozen since before `tick` was unfrozen by then when
 *  - its core shows input, which freeze clears (no direction, no hook): the
 *    server re-synced the core on the tick it changed, which can lie before
 *    the next snapshot (a hammer, most often, from a tee that ticks first);
 *  - it crossed an unfreeze tile in the previous tick's move. */
static const dd_rc_anchor *dd_rc_freeze_extras(dd_demo_state *st, int cid, int tick, const dd_rc_anchor *extras,
                                               const dd_netobj_character_core *core) {
  const int end = extras->ddnet.m_FreezeEnd;
  if (!((end == -1 || end > tick) && extras->ddnet.m_FreezeStart < tick)) return extras;
  const dd_rc_track *t = &st->tracks[cid];
  const int k = (int)(extras - t->anchors);
  if (k < 0 || k + 1 >= t->count) return extras;
  const dd_rc_anchor *next = &t->anchors[k + 1];
  if (!next->has_ddnet || next->snap_tick <= tick || next->ddnet.m_FreezeEnd != 0) return extras;
  const bool input = core->m_Direction != 0 || (core->m_HookState != DD_HOOK_IDLE && core->m_HookState != DD_HOOK_RETRACTED);
  /* unfreeze tiles leave deep freeze alone */
  return input || (end != -1 && dd_rc_crossed_unfreeze(st, cid, tick)) ? next : extras;
}

static void dd_rc_fill_character(dd_demo_state *st, int cid, int tick, dd_state_character *c) {
  memset(c, 0, sizeof(*c));
  dd_netobj_character_core core;
  const dd_rc_anchor *extras = NULL;
  const int quality = dd_rc_core_at(st, cid, tick, &core, &extras, true);
  if (quality == DD_QUALITY_NONE) return;
  c->quality = (dd_state_quality)quality;
  c->x = (float)core.m_X;
  c->y = (float)core.m_Y;
  c->vel_x = core.m_VelX / 256.0f;
  c->vel_y = core.m_VelY / 256.0f;
  c->hook_x = (float)core.m_HookX;
  c->hook_y = (float)core.m_HookY;
  c->hook_dx = core.m_HookDx / 256.0f;
  c->hook_dy = core.m_HookDy / 256.0f;
  c->hook_state = core.m_HookState;
  c->hook_tick = core.m_HookTick;
  c->hooked_player = core.m_HookedPlayer;
  c->direction = core.m_Direction;
  c->angle = core.m_Angle;
  c->jumped = core.m_Jumped;
  c->jumps = 2;
  c->jumped_total = -1;
  c->freeze_end = 0;
  dd_rc_display_aim(st, cid, tick, &c->aim_x, &c->aim_y);
  if (!extras) return;
  c->weapon = extras->chr.m_Weapon;
  c->ammo = extras->chr.m_AmmoCount;
  c->health = extras->chr.m_Health;
  c->armor = extras->chr.m_Armor;
  c->emote = extras->chr.m_Emote;
  c->attack_tick = extras->chr.m_AttackTick;
  c->player_flags = extras->chr.m_PlayerFlags;
  if (!extras->has_ddnet) return;
  c->has_ddnet_info = true;
  c->flags = (unsigned)extras->ddnet.m_Flags;
  const dd_rc_anchor *freeze = dd_rc_freeze_extras(st, cid, tick, extras, &core);
  c->freeze_end = freeze->ddnet.m_FreezeEnd;
  c->freeze_start = freeze->ddnet.m_FreezeStart;
  c->jumps = extras->ddnet.m_Jumps;
  c->jumped_total = extras->ddnet.m_JumpedTotal;
  c->tele_checkpoint = extras->ddnet.m_TeleCheckpoint;
  c->ninja_activation_tick = extras->ddnet.m_NinjaActivationTick;
  c->target_x = extras->ddnet.m_TargetX;
  c->target_y = extras->ddnet.m_TargetY;
  c->strong_weak_id = extras->ddnet.m_StrongWeakId;
}

bool dd_demo_state_get(dd_demo_state *st, int tick, dd_state_tick *out) {
  memset(out, 0, sizeof(*out));
  if (tick < st->first_tick || tick > st->last_tick) return false;
  out->tick = tick;
  const dd_rc_snap *snap = dd_rc_snap_at(st, tick);
  out->local_client_id = snap ? snap->local_client_id : -1;
  if (snap) {
    out->projectiles = st->projectiles + snap->proj0;
    out->num_projectiles = snap->num_proj;
    out->lasers = st->lasers + snap->laser0;
    out->num_lasers = snap->num_laser;
    out->pickups = st->pickups + snap->pickup0;
    out->num_pickups = snap->num_pickup;
    if (snap->tick == tick) {
      out->events = st->events + snap->event0;
      out->num_events = snap->num_event;
    }
    out->has_game_info = snap->has_game_info;
    out->game_flags = snap->game_info.m_GameFlags;
    out->game_state_flags = snap->game_info.m_GameStateFlags;
    out->round_start_tick = snap->game_info.m_RoundStartTick;
    out->warmup_timer = snap->game_info.m_WarmupTimer;
    out->has_switch_state = snap->has_switch_state;
    out->highest_switch_number = snap->switch_state.m_HighestSwitchNumber;
    for (int i = 0; i < 8; ++i) out->switch_status[i] = (unsigned)snap->switch_state.m_aStatus[i];
  }
  /* messages of this tick */
  {
    int lo = 0, hi = st->num_messages;
    while (lo < hi) {
      int mid = (lo + hi) / 2;
      if (st->messages[mid].tick < tick)
        lo = mid + 1;
      else
        hi = mid;
    }
    int end = lo;
    while (end < st->num_messages && st->messages[end].tick == tick) end++;
    out->messages = st->messages + lo;
    out->num_messages = end - lo;
  }

  const dd_rc_teams_entry *teams = dd_rc_teams_at(st, tick);
  for (int cid = 0; cid < DD_STATE_MAX_CLIENTS; ++cid) {
    /* player info */
    const dd_rc_player_log *log = &st->players[cid];
    for (int i = log->count - 1; i >= 0; --i)
      if (log->entries[i].tick <= tick) {
        out->players[cid] = log->entries[i].player;
        break;
      }
    out->players[cid].ddrace_team = teams ? teams->team[cid] : 0;

    dd_rc_fill_character(st, cid, tick, &out->characters[cid]);
  }
  return true;
}

bool dd_demo_state_characters(dd_demo_state *st, int tick, dd_state_character out[DD_STATE_MAX_CLIENTS]) {
  if (tick < st->first_tick || tick > st->last_tick) {
    memset(out, 0, sizeof(dd_state_character) * DD_STATE_MAX_CLIENTS);
    return false;
  }
  for (int cid = 0; cid < DD_STATE_MAX_CLIENTS; ++cid) dd_rc_fill_character(st, cid, tick, &out[cid]);
  return true;
}

bool dd_demo_state_player(const dd_demo_state *st, int tick, int client_id, dd_state_player *out) {
  memset(out, 0, sizeof(*out));
  if (tick < st->first_tick || tick > st->last_tick || client_id < 0 || client_id >= DD_STATE_MAX_CLIENTS) return false;
  /* the last entry at or before the tick */
  const dd_rc_player_log *log = &st->players[client_id];
  int lo = 0, hi = log->count;
  while (lo < hi) {
    const int mid = (lo + hi) / 2;
    if (log->entries[mid].tick <= tick)
      lo = mid + 1;
    else
      hi = mid;
  }
  if (lo > 0) *out = log->entries[lo - 1].player;
  const dd_rc_teams_entry *teams = dd_rc_teams_at(st, tick);
  out->ddrace_team = teams ? teams->team[client_id] : 0;
  return true;
}

int dd_state_tuning_count(void) { return DD_TUNING_NUM; }
const char *dd_state_tuning_name(int index) { return index >= 0 && index < DD_TUNING_NUM ? dd_tuning_names[index] : NULL; }

bool dd_demo_state_tuning(const dd_demo_state *st, int tick, int zone, float *out) {
  if (zone < 0 || zone >= DD_NUM_TUNEZONES) return false;
  dd_tuning t;
  dd_rc_tuning_at(st, tick, zone, &t);
  const int *values = (const int *)&t;
  for (int i = 0; i < DD_TUNING_NUM; ++i) out[i] = dd_tune(values[i]);
  return true;
}

const uint8_t *dd_demo_state_map(const dd_demo_state *st, size_t *size) {
  if (size) *size = st->map_size;
  return st->map_bytes;
}

const dd_state_message *dd_demo_state_messages(const dd_demo_state *st, int *count) {
  if (count) *count = st->num_messages;
  return st->messages;
}

const char *dd_demo_state_map_name(const dd_demo_state *st) { return st->map_name; }
uint32_t dd_demo_state_map_crc(const dd_demo_state *st) { return st->info.map_crc; }

bool dd_demo_state_map_sha256(const dd_demo_state *st, uint8_t out[32]) {
  if (!st->info.has_sha256) return false;
  memcpy(out, st->info.map_sha256, 32);
  return true;
}

void dd_demo_state_stats(const dd_demo_state *st, dd_state_stats *out) { *out = st->stats; }

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif

#endif /* DD_RECONSTRUCT_INTERNAL_H */
