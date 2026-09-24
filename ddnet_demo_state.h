/*
 * ddnet_demo_state.h - the complete game state of a DDNet 0.6 demo, tick by tick.
 *
 * A 0.6 demo does not contain every tick: the server snaps every other tick
 * (and packets get lost), and the characters it snaps are its dead-reckoning
 * cores, not the state at the snapshot tick. This header rebuilds every tick
 * from that, using a 1:1 port of DDNet's own physics internally, and hands out
 * plain per-tick state. Nothing about snapshots, reckoning or physics leaks out.
 *
 * Every character state carries a quality:
 *   RECORDED      the state the server itself sent for exactly this tick
 *   EXACT         rebuilt bit for bit from the server's own prediction
 *   VERIFIED      rebuilt by simulation and confirmed by the next recorded state
 *   APPROXIMATED  simulated, then corrected to meet the next recorded state
 * Fields other than the movement core (weapon, health, emote, ...) change at
 * snapshot resolution, like in DDNet's demo player. Events are placed on the
 * snapshot tick that carried them.
 *
 * Usage, in exactly one C file:
 *   #define DDNET_DEMO_IMPLEMENTATION        (unless done elsewhere)
 *   #include "ddnet_demo.h"
 *   #define DDNET_DEMO_STATE_IMPLEMENTATION
 *   #include "ddnet_demo_state.h"
 * and link ddnet_map_loader. Do not compile that file with -ffast-math.
 */
#ifndef DDNET_DEMO_STATE_H
#define DDNET_DEMO_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DD_STATE_MAX_CLIENTS 64
#define DD_STATE_TICK_SPEED 50

typedef enum {
  DD_QUALITY_NONE = 0, /* no character this tick */
  DD_QUALITY_RECORDED,
  DD_QUALITY_EXACT,
  DD_QUALITY_VERIFIED,
  DD_QUALITY_APPROXIMATED,
} dd_state_quality;

/* A character. Positions are world units, velocities units per tick. */
typedef struct {
  dd_state_quality quality;
  float x, y, vel_x, vel_y;
  float hook_x, hook_y, hook_dx, hook_dy;
  int hook_state, hook_tick, hooked_player;
  int direction, angle; /* angle: radians * 256, like the net object */
  int jumped, jumped_total, jumps;
  int weapon, ammo, health, armor, emote, attack_tick, player_flags;
  int freeze_start, freeze_end; /* ticks; freeze_end is -1 for deep freeze, 0 when not frozen */
  unsigned flags;               /* DD_CHARACTERFLAG_* from ddnet_demo.h */
  int ninja_activation_tick, tele_checkpoint;
  int target_x, target_y; /* the player's aim, relative to the tee */
  /* The aim as DDNet's demo player draws it, relative to the tee: the target
   * (or, without DDNet's extras, the angle) interpolated between the
   * snapshots around this tick. Aim only changes at snapshots, so this is
   * what to show; target_x/y and angle are the recorded values. Unlike DDNet,
   * a snapshot whose aim stalled for one late input is put back in line with
   * its neighbours first. */
  float aim_x, aim_y;
  bool has_ddnet_info;    /* flags, freeze, jumps, target, ... were sent */
  int strong_weak_id;
} dd_state_character;

typedef struct {
  bool connected;
  bool local; /* the player who recorded the demo */
  int team;   /* -1 spectators, 0 game */
  int ddrace_team;
  int score, latency;
  char name[17], clan[13], skin[25];
  int country, use_custom_color, color_body, color_feet;
  unsigned ddnet_flags;
  int auth_level;
  /* Paused with /spec: the character is out of the game and sends no state;
   * DDNet's client draws it at spec_x/y with the x_spec skin. */
  bool spec_char;
  int spec_x, spec_y;
} dd_state_player;

/* Projectiles are described by where and when they started, like on the net:
 * the position at a tick follows from DDNet's CalcPos with the tuning.
 * `owner`, for projectiles, lasers and events alike, is the client it belongs
 * to, or -1 for the world's own (map turrets, door lasers) and anything that
 * cannot be told. Where the demo does not say, it is the tee that stood where
 * a shot started, the grenade that burst where an explosion is (else the tee
 * next to it: a rocket jump), or the tee nearest to an event. */
typedef struct {
  float x, y, vel_x, vel_y;
  int type, start_tick, owner, switch_number, tune_zone;
  bool explosive, freeze;
  int bouncing; /* 1 horizontal, 2 vertical */
} dd_state_projectile;

typedef struct {
  float from_x, from_y, to_x, to_y;
  int start_tick, owner, type, subtype, switch_number, flags;
} dd_state_laser;

typedef struct {
  float x, y;
  int type, subtype, switch_number, flags;
} dd_state_pickup;

typedef enum {
  DD_STATE_EVENT_EXPLOSION = 0,
  DD_STATE_EVENT_SPAWN,
  DD_STATE_EVENT_HAMMERHIT,
  DD_STATE_EVENT_DEATH,
  DD_STATE_EVENT_SOUND_GLOBAL,
  DD_STATE_EVENT_SOUND_WORLD,
  DD_STATE_EVENT_DAMAGE_IND,
  DD_STATE_EVENT_FINISH,
  DD_STATE_EVENT_BIRTHDAY,
  DD_STATE_EVENT_MAP_SOUND_WORLD,
} dd_state_event_type;

typedef struct {
  dd_state_event_type type;
  float x, y;
  int owner;     /* see dd_state_projectile */
  int client_id; /* DEATH */
  int sound_id;  /* SOUND_* */
  int angle;     /* DAMAGE_IND */
} dd_state_event;

typedef enum {
  DD_STATE_MSG_OTHER = 0,
  DD_STATE_MSG_MOTD,
  DD_STATE_MSG_BROADCAST,
  DD_STATE_MSG_CHAT,
  DD_STATE_MSG_KILLMSG,
  DD_STATE_MSG_SOUND_GLOBAL,
  DD_STATE_MSG_TUNE_PARAMS,
  DD_STATE_MSG_WEAPON_PICKUP,
  DD_STATE_MSG_EMOTICON,
  DD_STATE_MSG_VOTE_SET,
  DD_STATE_MSG_VOTE_STATUS,
  DD_STATE_MSG_DDRACE_TIME,
  DD_STATE_MSG_RECORD,
  DD_STATE_MSG_TEAMS_STATE,
  DD_STATE_MSG_KILLMSG_TEAM,
  DD_STATE_MSG_RACE_FINISH,
  DD_STATE_MSG_MAP_SOUND_GLOBAL,
} dd_state_message_type;

typedef struct {
  dd_state_message_type type;
  int tick;
  /* decoded fields, by type:
   *   CHAT: team, client_id, text          KILLMSG: killer, victim, weapon, mode_special
   *   BROADCAST, MOTD: text                EMOTICON: client_id, value (emoticon)
   *   SOUND_GLOBAL, MAP_SOUND_GLOBAL: value (sound id)   WEAPON_PICKUP: weapon
   *   VOTE_SET: value (timeout), text (description), text2 (reason)
   *   VOTE_STATUS: yes, no, pass, total    DDRACE_TIME: time, check, finish
   *   RECORD: time (server best), time2 (player best)
   *   KILLMSG_TEAM: team, client_id (first)
   *   RACE_FINISH: client_id, time, diff, record_personal, record_server */
  int team, client_id, killer, victim, weapon, mode_special, value;
  int yes, no, pass, total;
  int time, time2, check, diff;
  bool finish, record_personal, record_server;
  const char *text, *text2;
  /* the message as it was recorded, including its id */
  const uint8_t *raw;
  int raw_size;
} dd_state_message;

typedef struct {
  int tick;
  int local_client_id; /* -1 if unknown */
  dd_state_character characters[DD_STATE_MAX_CLIENTS];
  dd_state_player players[DD_STATE_MAX_CLIENTS];
  /* entities as of the last snapshot at or before this tick */
  const dd_state_projectile *projectiles;
  int num_projectiles;
  const dd_state_laser *lasers;
  int num_lasers;
  const dd_state_pickup *pickups;
  int num_pickups;
  /* events of this tick, and messages that arrived at this tick */
  const dd_state_event *events;
  int num_events;
  const dd_state_message *messages;
  int num_messages;
  /* game info as of the last snapshot */
  bool has_game_info;
  int game_flags, game_state_flags, round_start_tick, warmup_timer;
  bool has_switch_state;
  int highest_switch_number;
  unsigned switch_status[8]; /* bit n: switch n is on, for the recording player's team */
} dd_state_tick;

typedef struct {
  long character_ticks;
  long recorded, exact, verified, approximated;
} dd_state_stats;

typedef struct dd_demo_state dd_demo_state;

/* Called now and then while a demo loads, with the share done so far (0..1).
 * Returning false cancels the load. */
typedef bool (*dd_state_progress_fn)(void *user, float progress);

typedef struct {
  dd_state_progress_fn progress; /* may be NULL */
  void *progress_user;
  /* A file to keep the reconstruction in, or NULL. When it holds the
   * reconstruction of this very demo it is used instead of rebuilding,
   * otherwise it is (re)written after the rebuild. Loading from it gives the
   * same state as rebuilding. */
  const char *cache_path;
} dd_state_load_options;

/* Reads and reconstructs a whole demo. Returns NULL and fills `error` on failure. */
dd_demo_state *dd_demo_state_load(const char *path, char *error, size_t error_size);
/* The same, with options; `options` may be NULL. A cancelled load returns
 * NULL with the error "cancelled". */
dd_demo_state *dd_demo_state_load_ex(const char *path, const dd_state_load_options *options, char *error, size_t error_size);
void dd_demo_state_free(dd_demo_state *state);

int dd_demo_state_first_tick(const dd_demo_state *state);
int dd_demo_state_last_tick(const dd_demo_state *state);

/* The state at `tick`. Pointers in `out` stay valid until the next call on
 * this state or dd_demo_state_free. Returns false outside the demo. */
bool dd_demo_state_get(dd_demo_state *state, int tick, dd_state_tick *out);

/* dd_demo_state_get without the players and characters, which are most of
 * its cost: entities, events, messages and game info only. */
bool dd_demo_state_entities(dd_demo_state *state, int tick, dd_state_tick *out);

/* Only the characters at `tick`, as in dd_demo_state_get; cheap enough to ask
 * for neighbouring ticks every frame. Returns false outside the demo. */
bool dd_demo_state_characters(dd_demo_state *state, int tick, dd_state_character out[DD_STATE_MAX_CLIENTS]);

/* Only one player's info at `tick`, as in dd_demo_state_get; cheap enough to
 * ask for every tick. Returns false outside the demo or for a bad client id. */
bool dd_demo_state_player(const dd_demo_state *state, int tick, int client_id, dd_state_player *out);

/* The tuning in effect at `tick` in tune zone `zone` (0 outside any zone), as
 * dd_state_tuning_count() values in DDNet's order, with DDNet's names. On a
 * map without tune zones this is the tuning the server sent; with tune zones
 * it is the tuning the map's settings give each zone. */
int dd_state_tuning_count(void);
const char *dd_state_tuning_name(int index);
bool dd_demo_state_tuning(const dd_demo_state *state, int tick, int zone, float *out);

/* Every message of the demo, in tick order, as dd_demo_state_get hands them
 * out tick by tick. Valid until dd_demo_state_free. */
const dd_state_message *dd_demo_state_messages(const dd_demo_state *state, int *count);

/* The map embedded in the demo. */
const uint8_t *dd_demo_state_map(const dd_demo_state *state, size_t *size);
const char *dd_demo_state_map_name(const dd_demo_state *state);
uint32_t dd_demo_state_map_crc(const dd_demo_state *state);
/* false when the demo has no SHA256 for its map */
bool dd_demo_state_map_sha256(const dd_demo_state *state, uint8_t out[32]);

void dd_demo_state_stats(const dd_demo_state *state, dd_state_stats *out);

#ifdef __cplusplus
}
#endif

#endif /* DDNET_DEMO_STATE_H */

#ifdef DDNET_DEMO_STATE_IMPLEMENTATION
#ifndef DDNET_DEMO_STATE_IMPLEMENTED
#define DDNET_DEMO_STATE_IMPLEMENTED
#include "internal/dd_reconstruct.h"
#endif
#endif
