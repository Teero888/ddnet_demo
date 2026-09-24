/*
 * internal/dd_server.h - 1:1 port of DDNet's server-side gameplay: the game
 * world, CCharacter, the race controller's tiles, and every map entity
 * (game/server/gameworld.cpp, the entities directory, gamemodes/ddnet.cpp and the
 * physics parts of gamecontext.cpp).
 *
 * INTERNAL: not part of the library's API. Include dd_physics.h first.
 *
 * Only what changes the simulated world is ported. Chat, sounds, scores,
 * antibot, saving and snapping are left out; the few world events a demo
 * shows (explosions, hammer hits, deaths, spawns, damage indicators) are
 * recorded instead of being sent.
 *
 * Entities live in an index pool with DDNet's per-type linked lists (newest
 * first, which is DDNet's tick order), so a whole world copies with
 * dd_server_copy. The collision is shared read-only between copies.
 *
 * What a demo cannot carry is supplied by the caller: player input per tick
 * (dd_server_set_input) and the server's random choices (the world core's
 * random callback, used for teleporters with several outputs).
 */
#ifndef DD_SERVER_INTERNAL_H
#define DD_SERVER_INTERNAL_H

#ifndef DD_PHYSICS_INTERNAL_H
#error "include internal/dd_physics.h before internal/dd_server.h"
#endif

/* Scratch buffers too big for the stack are per thread: several demos may
 * load at once, each on its own thread. */
#ifndef DD_THREAD_LOCAL
#if defined(_MSC_VER)
#define DD_THREAD_LOCAL __declspec(thread)
#elif defined(__cplusplus)
#define DD_THREAD_LOCAL thread_local
#else
#define DD_THREAD_LOCAL _Thread_local
#endif
#endif

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#define DD_NUM_TUNEZONES 256
#define DD_INPUT_STATE_MASK 0x3f
/* (int)(TickSpeed * 0.15f), the 150ms step of movers, draggers, turrets and lights */
#define DD_MOVER_STEP ((int)(DD_SERVER_TICK_SPEED * 0.15f))

enum { DD_ENTTYPE_PROJECTILE = 0, DD_ENTTYPE_LASER, DD_ENTTYPE_PICKUP, DD_ENTTYPE_FLAG, DD_ENTTYPE_CHARACTER, DD_NUM_ENTTYPES };

enum {
  DD_ENT_CHARACTER = 0,
  DD_ENT_PROJECTILE,
  DD_ENT_LASER,
  DD_ENT_PICKUP,
  DD_ENT_DOOR,
  DD_ENT_DRAGGER,
  DD_ENT_DRAGGER_BEAM,
  DD_ENT_GUN,
  DD_ENT_LIGHT,
  DD_ENT_PLASMA,
};

/* ddnet_demo.h has DD_POWERUP_* up to ARMOR_LASER and DD_WEAPON_WORLD; DDNet's
 * next powerup, POWERUP_FREEZE, is only used by the server. */
#define DD_POWERUP_FREEZE 8

/* mapitems.h entity numbers */
enum {
  DD_ENTITY_SPAWN = 1,
  DD_ENTITY_SPAWN_RED = 2,
  DD_ENTITY_SPAWN_BLUE = 3,
  DD_ENTITY_ARMOR_1 = 6,
  DD_ENTITY_HEALTH_1 = 7,
  DD_ENTITY_WEAPON_SHOTGUN = 8,
  DD_ENTITY_WEAPON_GRENADE = 9,
  DD_ENTITY_POWERUP_NINJA = 10,
  DD_ENTITY_WEAPON_LASER = 11,
  DD_ENTITY_LASER_FAST_CCW = 12,
  DD_ENTITY_LASER_STOP = 15,
  DD_ENTITY_LASER_FAST_CW = 18,
  DD_ENTITY_LASER_SHORT = 19,
  DD_ENTITY_LASER_LONG = 21,
  DD_ENTITY_LASER_C_SLOW = 22,
  DD_ENTITY_LASER_C_FAST = 24,
  DD_ENTITY_LASER_O_SLOW = 25,
  DD_ENTITY_LASER_O_FAST = 27,
  DD_ENTITY_PLASMAE = 29,
  DD_ENTITY_PLASMAF = 30,
  DD_ENTITY_PLASMA = 31,
  DD_ENTITY_PLASMAU = 32,
  DD_ENTITY_CRAZY_SHOTGUN_EX = 33,
  DD_ENTITY_CRAZY_SHOTGUN = 34,
  DD_ENTITY_ARMOR_SHOTGUN = 35,
  DD_ENTITY_ARMOR_GRENADE = 36,
  DD_ENTITY_ARMOR_NINJA = 37,
  DD_ENTITY_ARMOR_LASER = 38,
  DD_ENTITY_DRAGGER_WEAK = 42,
  DD_ENTITY_DRAGGER_STRONG = 44,
  DD_ENTITY_DRAGGER_WEAK_NW = 45,
  DD_ENTITY_DRAGGER_STRONG_NW = 47,
  DD_ENTITY_DOOR = 49,
};

/* content.py: ninja */
#define DD_NINJA_DURATION 15000
#define DD_NINJA_MOVETIME 200
#define DD_NINJA_VELOCITY 50
#define DD_NINJA_DAMAGE 9
#define DD_HAMMER_DAMAGE 3

enum { DD_RACE_NONE = 0, DD_RACE_STARTED, DD_RACE_CHEATED, DD_RACE_FINISHED };
enum { DD_SV_TEAM_OFF = 0, DD_SV_TEAM_ON, DD_SV_TEAM_MANDATORY, DD_SV_TEAM_FORCED_SOLO };

/* ---- server settings a map (or server) can change ---- */
typedef struct {
  int sv_old_teleport_weapons, sv_old_teleport_hook, sv_teleport_hold_hook, sv_teleport_lose_weapons, sv_deepfly,
      sv_destroy_bullets_on_death, sv_destroy_lasers_on_death, sv_hit, sv_endless_drag, sv_freeze_delay, sv_team, sv_old_laser,
      sv_no_weak_hook, sv_reset_pickups, sv_plasma_range, sv_plasma_per_sec, sv_dragger_range, sv_solo_server, sv_endless_super_hook,
      sv_min_team_size;
  bool bug_grenade_double_explosion; /* map "bug grenade-doubleexplosion@ddnet.tw" */
} dd_server_config;

static void dd_server_config_default(dd_server_config *c) {
  memset(c, 0, sizeof(*c));
  c->sv_deepfly = 1;
  c->sv_destroy_bullets_on_death = 1;
  c->sv_hit = 1;
  c->sv_freeze_delay = 3;
  c->sv_team = DD_SV_TEAM_ON;
  c->sv_plasma_range = 700;
  c->sv_plasma_per_sec = 3;
  c->sv_dragger_range = 700;
  c->sv_min_team_size = 2;
}

/* tuning.h script names, in declaration order */
static const char *const dd_tuning_names[DD_TUNING_NUM] = {
    "ground_control_speed", "ground_control_accel", "ground_friction", "ground_jump_impulse", "air_jump_impulse", "air_control_speed",
    "air_control_accel", "air_friction", "hook_length", "hook_fire_speed", "hook_drag_accel", "hook_drag_speed", "gravity",
    "velramp_start", "velramp_range", "velramp_curvature", "gun_curvature", "gun_speed", "gun_lifetime", "shotgun_curvature",
    "shotgun_speed", "shotgun_speeddiff", "shotgun_lifetime", "grenade_curvature", "grenade_speed", "grenade_lifetime", "laser_reach",
    "laser_bounce_delay", "laser_bounce_num", "laser_bounce_cost", "laser_damage", "player_collision", "player_hooking",
    "jetpack_strength", "shotgun_strength", "explosion_strength", "hammer_strength", "hook_duration", "hammer_fire_delay",
    "gun_fire_delay", "shotgun_fire_delay", "grenade_fire_delay", "laser_fire_delay", "ninja_fire_delay", "hammer_hit_fire_delay",
    "ground_elasticity_x", "ground_elasticity_y"};

static bool dd_tuning_set(dd_tuning *t, const char *name, float value) {
  for (int i = 0; i < DD_TUNING_NUM; ++i) {
    const char *a = dd_tuning_names[i], *b = name;
    while (*a && *b && (*a | 32) == (*b | 32)) ++a, ++b; /* str_comp_nocase */
    if (!*a && !*b) {
      ((int *)t)[i] = dd_tune_param(value);
      return true;
    }
  }
  return false;
}

/* ---- world events a demo shows (instead of CGameContext::Create*) ---- */
enum {
  DD_EVENT_EXPLOSION = 0,
  DD_EVENT_HAMMERHIT,
  DD_EVENT_DEATH,
  DD_EVENT_SPAWN,
  DD_EVENT_DAMAGEIND,
};

typedef struct {
  int type;
  dd_vec2 pos;
  int client_id; /* DEATH */
  float angle;   /* DAMAGEIND */
  int amount;    /* DAMAGEIND */
} dd_world_event;

#define DD_MAX_WORLD_EVENTS 512

/* ---- entities ---- */
typedef struct {
  bool owner_alive;
  int owner_id, ddrace_team;
  bool solo, no_hit_others, no_hit_self;
} dd_interactions;

typedef struct {
  int kind, objtype;
  bool used, marked_for_destroy;
  int prev, next; /* neighbours in the type list, -1 at the ends */
  dd_vec2 pos;
  float proximity_radius;
  union {
    int character; /* client id */
    struct {
      int type, owner, life_span, start_tick, layer, number, bouncing, tune_zone, ddrace_team;
      bool explosive, freeze, belongs_to_practice_team, is_solo;
      dd_vec2 direction, init_dir;
    } projectile;
    struct {
      dd_vec2 from, dir, tele_pos, prev_pos;
      bool was_tele, zero_energy_bounce_in_last_tick, teleport_cancelled, is_blue_teleport, belongs_to_practice_team;
      float energy;
      int bounces, eval_tick, owner, type, tune_zone, number, layer;
      dd_interactions interact;
    } laser;
    struct {
      int type, subtype, layer, number, flags;
      dd_vec2 core;
    } pickup;
    struct {
      int number, length;
      dd_vec2 direction, to;
    } door;
    struct {
      dd_vec2 core;
      float strength;
      bool ignore_walls;
      int layer, number, eval_tick;
      int target_id_in_team[DD_PHYS_MAX_CLIENTS];
      int beam[DD_PHYS_MAX_CLIENTS]; /* entity index or -1 */
    } dragger;
    struct {
      int dragger; /* entity index */
      float strength;
      bool ignore_walls, active;
      int for_client_id, layer, number, eval_tick;
    } beam;
    struct {
      dd_vec2 core;
      bool freeze, explosive;
      int layer, number, eval_tick;
      int last_fire_team[DD_PHYS_MAX_CLIENTS], last_fire_solo[DD_PHYS_MAX_CLIENTS];
    } gun;
    struct {
      dd_vec2 to, core;
      float rotation, angular_speed;
      int layer, number, eval_tick, tick, curve_length, length_l, speed, length;
    } light;
    struct {
      dd_vec2 core;
      int freeze; /* int in DDNet */
      bool explosive;
      int for_client_id, eval_tick, life_time;
    } plasma;
  } u;
} dd_entity;

/* ---- characters ---- */
typedef struct {
  bool alive;
  int ent; /* entity index */
  dd_character_core core;
  dd_vec2 prev_pos;

  dd_player_input input, prev_input, latest_input, latest_prev_input, latest_prev_prev_input, saved_input;
  int num_inputs;

  int health, armor;
  int attack_tick, damage_taken_tick;
  int reload_timer, queued_weapon, last_weapon;
  int freeze_time;
  bool frozen_last_tick;
  int pain_sound_timer;
  int emote_type, emote_stop;
  int last_action, last_move;
  int tune_zone, tune_zone_old;
  int tele_checkpoint;
  int start_time, ddrace_state;
  bool last_refill_jumps, last_penalty, last_bonus;
  bool tele_gun_teleport, is_blue_tele_gun_teleport;
  dd_vec2 tele_gun_pos;
  int move_restrictions;
  int tile_index, tile_findex;
  int num_objects_hit;
  int hit_objects[DD_PHYS_MAX_CLIENTS];
  int strong_weak_id;
  int spawn_tick, weapon_change_tick;
  bool ninja_jetpack; /* the player's /ninjajetpack setting */
} dd_character;

typedef struct {
  int tune_zone; /* CPlayer::m_TuneZone, from the view position */
  int die_tick;
} dd_player;

typedef struct {
  const dd_collision *col; /* shared, read-only after dd_server_init */
  dd_server_config cfg;
  dd_tuning tuning[DD_NUM_TUNEZONES];
  dd_world_core core;
  dd_teams_core teams;
  bool team_practice[DD_PHYS_NUM_DDRACE_TEAMS];
  int tick;

  dd_character chars[DD_PHYS_MAX_CLIENTS];
  dd_player players[DD_PHYS_MAX_CLIENTS];

  dd_entity *ents;
  int ent_capacity;
  int first[DD_NUM_ENTTYPES];
  int next_traverse; /* m_pNextTraverseEntity */

  dd_vec2 *spawn_points[3];
  int num_spawn_points[3];

  dd_world_event events[DD_MAX_WORLD_EVENTS];
  int num_events;

  /* for dd_server_restore: character slots and entities in use at some
   * point since the last dd_server_copy this world descends from */
  uint64_t chars_touched;
  int ent_high;
} dd_server;

/* ======================================================================== */
/* entity pool and lists (CGameWorld) */

static void dd_server_event(dd_server *s, int type, dd_vec2 pos, int client_id, float angle, int amount) {
  if (s->num_events >= DD_MAX_WORLD_EVENTS) return;
  dd_world_event *e = &s->events[s->num_events++];
  e->type = type;
  e->pos = pos;
  e->client_id = client_id;
  e->angle = angle;
  e->amount = amount;
}

static int dd_ent_alloc(dd_server *s, int kind, int objtype, dd_vec2 pos, float proximity_radius) {
  int index = -1;
  for (int i = 0; i < s->ent_capacity; ++i)
    if (!s->ents[i].used) {
      index = i;
      break;
    }
  if (index < 0) {
    int capacity = s->ent_capacity ? s->ent_capacity * 2 : 64;
    dd_entity *grown = (dd_entity *)realloc(s->ents, sizeof(dd_entity) * (size_t)capacity);
    if (!grown) return -1;
    memset(grown + s->ent_capacity, 0, sizeof(dd_entity) * (size_t)(capacity - s->ent_capacity));
    index = s->ent_capacity;
    s->ents = grown;
    s->ent_capacity = capacity;
  }
  if (index >= s->ent_high) s->ent_high = index + 1;
  dd_entity *e = &s->ents[index];
  memset(e, 0, sizeof(*e));
  e->used = true;
  e->kind = kind;
  e->objtype = objtype;
  e->pos = pos;
  e->proximity_radius = proximity_radius;
  e->prev = e->next = -1;
  return index;
}

/* CGameWorld::InsertEntity: at the head of its type list. */
static void dd_ent_insert(dd_server *s, int index) {
  dd_entity *e = &s->ents[index];
  const int head = s->first[e->objtype];
  if (head >= 0) s->ents[head].prev = index;
  e->next = head;
  e->prev = -1;
  s->first[e->objtype] = index;
}

/* CGameWorld::RemoveEntity */
static void dd_ent_remove(dd_server *s, int index) {
  dd_entity *e = &s->ents[index];
  if (e->next < 0 && e->prev < 0 && s->first[e->objtype] != index) return;
  if (e->prev >= 0)
    s->ents[e->prev].next = e->next;
  else
    s->first[e->objtype] = e->next;
  if (e->next >= 0) s->ents[e->next].prev = e->prev;
  if (s->next_traverse == index) s->next_traverse = e->next;
  e->next = e->prev = -1;
}

static void dd_ent_free(dd_server *s, int index) {
  dd_ent_remove(s, index);
  s->ents[index].used = false;
}

/* ======================================================================== */
/* characters: small helpers */

static inline dd_character *dd_chr(dd_server *s, int cid) {
  if (cid < 0 || cid >= DD_PHYS_MAX_CLIENTS || !s->chars[cid].alive) return NULL;
  return &s->chars[cid];
}
static inline dd_vec2 dd_chr_pos(const dd_server *s, const dd_character *c) { return s->ents[c->ent].pos; }
static inline void dd_chr_set_pos(dd_server *s, dd_character *c, dd_vec2 pos) { s->ents[c->ent].pos = pos; }
static inline int dd_chr_cid(const dd_character *c) { return c->core.id; }
static inline int dd_chr_team(const dd_server *s, const dd_character *c) { return dd_teams_team(&s->teams, dd_chr_cid(c)); }
static inline bool dd_chr_can_collide(const dd_server *s, const dd_character *c, int cid) {
  return dd_teams_can_collide(&s->teams, dd_chr_cid(c), cid);
}
static inline const dd_tuning *dd_server_tuning(const dd_server *s, int zone) { return &s->tuning[zone]; }
static inline float dd_ent_proximity(const dd_server *s, int index) { return s->ents[index].proximity_radius; }

static bool dd_game_layer_clipped(const dd_server *s, dd_vec2 check_pos) {
  return dd_round_to_int(check_pos.x) / 32 < -200 || dd_round_to_int(check_pos.x) / 32 > s->col->width + 200 ||
         dd_round_to_int(check_pos.y) / 32 < -200 || dd_round_to_int(check_pos.y) / 32 > s->col->height + 200;
}

/* CountInput: presses of a counted button between two input states */
static int dd_count_presses(int prev, int cur) {
  int presses = 0;
  prev &= DD_INPUT_STATE_MASK;
  cur &= DD_INPUT_STATE_MASK;
  int i = prev;
  while (i != cur) {
    i = (i + 1) & DD_INPUT_STATE_MASK;
    if (i & 1) presses++;
  }
  return presses;
}

/* CGameWorld::FindEntities */
static int dd_find_entities(const dd_server *s, dd_vec2 pos, float radius, int *out, int max, int type) {
  if (type < 0 || type >= DD_NUM_ENTTYPES) return 0;
  int num = 0;
  for (int e = s->first[type]; e >= 0; e = s->ents[e].next) {
    if (dd_v2_distance(s->ents[e].pos, pos) < radius + s->ents[e].proximity_radius) {
      if (out) out[num] = e;
      num++;
      if (num == max) break;
    }
  }
  return num;
}

/* CGameWorld::IntersectCharacter; returns the client id or -1 */
static int dd_intersect_character(const dd_server *s, dd_vec2 pos0, dd_vec2 pos1, float radius, dd_vec2 *new_pos, int not_this_cid,
                                  int collide_with, int this_only_cid) {
  float closest_len = dd_v2_distance(pos0, pos1) * 100.0f;
  int closest = -1;
  for (int e = s->first[DD_ENTTYPE_CHARACTER]; e >= 0; e = s->ents[e].next) {
    const int cid = s->ents[e].u.character;
    if (cid == not_this_cid) continue;
    if (this_only_cid >= 0 && cid != this_only_cid) continue;
    if (collide_with != -1 && !dd_teams_can_collide(&s->teams, cid, collide_with)) continue;
    dd_vec2 intersect_pos;
    if (dd_closest_point_on_line(pos0, pos1, s->ents[e].pos, &intersect_pos)) {
      float len = dd_v2_distance(s->ents[e].pos, intersect_pos);
      if (len < s->ents[e].proximity_radius + radius) {
        len = dd_v2_distance(pos0, intersect_pos);
        if (len < closest_len) {
          *new_pos = intersect_pos;
          closest_len = len;
          closest = cid;
        }
      }
    }
  }
  return closest;
}

/* CGameWorld::IntersectedCharacters; returns the count */
static int dd_intersected_characters(const dd_server *s, dd_vec2 pos0, dd_vec2 pos1, float radius, int *out) {
  int count = 0;
  for (int e = s->first[DD_ENTTYPE_CHARACTER]; e >= 0; e = s->ents[e].next) {
    dd_vec2 intersect_pos;
    if (dd_closest_point_on_line(pos0, pos1, s->ents[e].pos, &intersect_pos)) {
      float len = dd_v2_distance(s->ents[e].pos, intersect_pos);
      if (len < s->ents[e].proximity_radius + radius) out[count++] = s->ents[e].u.character;
    }
  }
  return count;
}

static void dd_chr_release_hook(dd_character *c);

/* CGameWorld::ReleaseHooked */
static void dd_release_hooked(dd_server *s, int client_id) {
  for (int e = s->first[DD_ENTTYPE_CHARACTER]; e >= 0; e = s->ents[e].next) {
    dd_character *c = &s->chars[s->ents[e].u.character];
    if (c->core.hooked_player == client_id && !c->core.is_super) dd_chr_release_hook(c);
  }
}

/* CEntity::GetNearestAirPos */
static bool dd_get_nearest_air_pos(const dd_server *s, dd_vec2 pos, dd_vec2 prev_pos, dd_vec2 *out_pos) {
  const dd_vec2 size = dd_v2(dd_physical_size, dd_physical_size);
  for (int k = 0; k < 16 && dd_col_check_point(s->col, pos.x, pos.y); k++) pos = dd_v2_sub(pos, dd_v2_normalize(dd_v2_sub(prev_pos, pos)));
  dd_vec2 pos_in_block = dd_v2((float)(dd_round_to_int(pos.x) % 32), (float)(dd_round_to_int(pos.y) % 32));
  dd_vec2 block_center = dd_v2_add(dd_v2_sub(dd_v2((float)dd_round_to_int(pos.x), (float)dd_round_to_int(pos.y)), pos_in_block), dd_v2(16.0f, 16.0f));
  *out_pos = dd_v2(block_center.x + (pos_in_block.x < 16 ? -2.0f : 1.0f), pos.y);
  if (!dd_col_test_box(s->col, *out_pos, size)) return true;
  *out_pos = dd_v2(pos.x, block_center.y + (pos_in_block.y < 16 ? -2.0f : 1.0f));
  if (!dd_col_test_box(s->col, *out_pos, size)) return true;
  *out_pos = dd_v2(block_center.x + (pos_in_block.x < 16 ? -2.0f : 1.0f), block_center.y + (pos_in_block.y < 16 ? -2.0f : 1.0f));
  return !dd_col_test_box(s->col, *out_pos, size);
}

/* CEntity::GetNearestAirPosPlayer */
static bool dd_get_nearest_air_pos_player(const dd_server *s, dd_vec2 player_pos, dd_vec2 *out_pos) {
  for (int distance = 5; distance >= -1; distance--) {
    *out_pos = dd_v2(player_pos.x, player_pos.y - (float)distance);
    if (!dd_col_test_box(s->col, *out_pos, dd_v2(dd_physical_size, dd_physical_size))) return true;
  }
  return false;
}

/* ======================================================================== */
/* CCharacter */

static void dd_chr_set_emote(dd_character *c, int emote, int tick) {
  c->emote_type = emote;
  c->emote_stop = tick;
}

static bool dd_chr_take_damage(dd_server *s, dd_character *c, dd_vec2 force, int dmg) {
  if (dmg) dd_chr_set_emote(c, 3 /* EMOTE_PAIN */, s->tick + 500 * DD_SERVER_TICK_SPEED / 1000);
  dd_vec2 temp = dd_v2_add(c->core.vel, force);
  c->core.vel = dd_clamp_vel(c->move_restrictions, temp);
  return true;
}

static void dd_chr_set_velocity(dd_character *c, dd_vec2 v) { c->core.vel = dd_clamp_vel(c->move_restrictions, v); }
static void dd_chr_add_velocity(dd_character *c, dd_vec2 a) { dd_chr_set_velocity(c, dd_v2_add(c->core.vel, a)); }
static void dd_chr_apply_move_restrictions(dd_character *c) { c->core.vel = dd_clamp_vel(c->move_restrictions, c->core.vel); }

static void dd_chr_set_weapon(dd_character *c, int w) {
  if (w == c->core.active_weapon) return;
  c->last_weapon = c->core.active_weapon;
  c->queued_weapon = -1;
  c->core.active_weapon = w;
  if (c->core.active_weapon < 0 || c->core.active_weapon >= DD_PHYS_NUM_WEAPONS) c->core.active_weapon = 0;
}

static void dd_chr_set_solo(dd_server *s, dd_character *c, bool solo) {
  c->core.solo = solo;
  s->teams.is_solo[dd_chr_cid(c)] = solo;
}

static void dd_chr_release_hook(dd_character *c) {
  dd_core_set_hooked_player(&c->core, -1);
  c->core.hook_state = DD_HOOK_RETRACTED;
  c->core.triggered_events |= DD_COREEVENT_HOOK_RETRACT;
}

static void dd_chr_reset_hook(dd_character *c) {
  dd_chr_release_hook(c);
  c->core.hook_pos = c->core.pos;
}

static bool dd_chr_freeze_for(dd_server *s, dd_character *c, int seconds) {
  if (seconds <= 0 || c->core.is_super || c->core.invincible || c->freeze_time > seconds * DD_SERVER_TICK_SPEED) return false;
  if (c->freeze_time == 0 || c->core.freeze_start < s->tick - DD_SERVER_TICK_SPEED) {
    c->armor = 0;
    c->freeze_time = seconds * DD_SERVER_TICK_SPEED;
    c->core.freeze_start = s->tick;
    return true;
  }
  return false;
}

static bool dd_chr_freeze(dd_server *s, dd_character *c) { return dd_chr_freeze_for(s, c, s->cfg.sv_freeze_delay); }

static bool dd_chr_unfreeze(dd_character *c) {
  if (c->freeze_time > 0) {
    c->armor = 10;
    if (c->core.active_weapon >= 0 && !c->core.weapons[c->core.active_weapon].got) c->core.active_weapon = DD_WEAPON_ID_GUN;
    c->freeze_time = 0;
    c->core.freeze_start = 0;
    c->frozen_last_tick = true;
    return true;
  }
  return false;
}

static void dd_chr_give_ninja(dd_server *s, dd_character *c) {
  c->core.ninja.activation_tick = s->tick;
  c->core.weapons[DD_WEAPON_ID_NINJA].got = true;
  c->core.weapons[DD_WEAPON_ID_NINJA].ammo = -1;
  if (c->core.active_weapon != DD_WEAPON_ID_NINJA) c->last_weapon = c->core.active_weapon;
  c->core.active_weapon = DD_WEAPON_ID_NINJA;
}

static void dd_chr_remove_ninja(dd_character *c) {
  c->core.ninja.activation_dir = dd_v2(0, 0);
  c->core.ninja.activation_tick = 0;
  c->core.ninja.current_move_time = 0;
  c->core.ninja.old_vel_amount = 0;
  c->core.weapons[DD_WEAPON_ID_NINJA].got = false;
  c->core.weapons[DD_WEAPON_ID_NINJA].ammo = 0;
  c->core.active_weapon = c->last_weapon;
  dd_chr_set_weapon(c, c->core.active_weapon);
}

static void dd_chr_give_weapon(dd_server *s, dd_character *c, int weapon, bool remove) {
  if (weapon == DD_WEAPON_ID_NINJA) {
    if (remove)
      dd_chr_remove_ninja(c);
    else
      dd_chr_give_ninja(s, c);
    return;
  }
  if (remove) {
    if (c->core.active_weapon == weapon) c->core.active_weapon = DD_WEAPON_ID_GUN;
  } else {
    c->core.weapons[weapon].ammo = -1;
  }
  c->core.weapons[weapon].got = !remove;
}

static void dd_chr_reset_pickups(dd_character *c) {
  for (int i = DD_WEAPON_ID_SHOTGUN; i < DD_PHYS_NUM_WEAPONS - 1; i++) {
    c->core.weapons[i].got = false;
    if (c->core.active_weapon == i) c->core.active_weapon = DD_WEAPON_ID_GUN;
  }
}

static void dd_server_create_explosion(dd_server *s, dd_vec2 pos, int owner, int weapon, bool no_damage, int activated_team);
static int dd_projectile_new(dd_server *s, int type, int owner, dd_vec2 pos, dd_vec2 dir, int span, bool freeze, bool explosive,
                             dd_vec2 init_dir, int layer, int number);
static int dd_laser_new(dd_server *s, dd_vec2 pos, dd_vec2 direction, float start_energy, int owner, int type);

/* CCharacter::Die: the world part */
static void dd_chr_die(dd_server *s, dd_character *c, int killer, int weapon) {
  (void)killer;
  (void)weapon;
  const int cid = dd_chr_cid(c);
  dd_server_event(s, DD_EVENT_DEATH, dd_chr_pos(s, c), cid, 0.0f, 0);
  s->players[cid].die_tick = s->tick;
  c->alive = false;
  dd_chr_set_solo(s, c, false);
  dd_ent_remove(s, c->ent);
  s->ents[c->ent].used = false;
  dd_world_set_character(&s->core, cid, NULL);
}

static void dd_chr_do_weapon_switch(dd_character *c) {
  if (c->reload_timer != 0 || c->queued_weapon == -1) return;
  if (c->core.weapons[DD_WEAPON_ID_NINJA].got || !c->core.weapons[c->queued_weapon].got) return;
  dd_chr_set_weapon(c, c->queued_weapon);
}

static void dd_chr_handle_weapon_switch(dd_character *c) {
  int wanted_weapon = c->core.active_weapon;
  if (c->queued_weapon != -1) wanted_weapon = c->queued_weapon;

  bool anything = false;
  for (int i = 0; i < DD_PHYS_NUM_WEAPONS - 1; ++i)
    if (c->core.weapons[i].got) anything = true;
  if (!anything) return;
  int next = dd_count_presses(c->latest_prev_input.next_weapon, c->latest_input.next_weapon);
  int prev = dd_count_presses(c->latest_prev_input.prev_weapon, c->latest_input.prev_weapon);

  if (next < 128) {
    while (next) {
      wanted_weapon = (wanted_weapon + 1) % DD_PHYS_NUM_WEAPONS;
      if (c->core.weapons[wanted_weapon].got) next--;
    }
  }
  if (prev < 128) {
    while (prev) {
      wanted_weapon = (wanted_weapon - 1) < 0 ? DD_PHYS_NUM_WEAPONS - 1 : wanted_weapon - 1;
      if (c->core.weapons[wanted_weapon].got) prev--;
    }
  }
  if (c->latest_input.wanted_weapon) wanted_weapon = c->input.wanted_weapon - 1;

  if (wanted_weapon >= 0 && wanted_weapon < DD_PHYS_NUM_WEAPONS && wanted_weapon != c->core.active_weapon && c->core.weapons[wanted_weapon].got)
    c->queued_weapon = wanted_weapon;

  dd_chr_do_weapon_switch(c);
}

static void dd_chr_fire_weapon(dd_server *s, dd_character *c) {
  const int cid = dd_chr_cid(c);
  if (c->reload_timer != 0) return;

  dd_chr_do_weapon_switch(c);
  dd_vec2 mouse_target = dd_v2((float)c->latest_input.target_x, (float)c->latest_input.target_y);
  dd_vec2 direction = dd_v2_normalize(mouse_target);

  bool full_auto = false;
  const int w = c->core.active_weapon;
  if (w == DD_WEAPON_ID_GRENADE || w == DD_WEAPON_ID_SHOTGUN || w == DD_WEAPON_ID_LASER) full_auto = true;
  if (c->core.jetpack && w == DD_WEAPON_ID_GUN) full_auto = true;
  if (c->frozen_last_tick) full_auto = true;

  if (!s->cfg.sv_deepfly && w == DD_WEAPON_ID_HAMMER && c->core.deep_frozen) return;

  bool will_fire = false;
  if (dd_count_presses(c->latest_prev_input.fire, c->latest_input.fire)) will_fire = true;
  if (full_auto && (c->latest_input.fire & 1) && w >= 0 && c->core.weapons[w].ammo) will_fire = true;
  if (!will_fire) return;

  if (c->freeze_time) {
    if (c->pain_sound_timer <= 0 && !(c->latest_prev_input.fire & 1)) c->pain_sound_timer = 1 * DD_SERVER_TICK_SPEED;
    return;
  }

  if (w < 0 || !c->core.weapons[w].ammo) return;

  const dd_vec2 pos = dd_chr_pos(s, c);
  dd_vec2 proj_start_pos = dd_v2_add(pos, dd_v2_mul(dd_v2_mul(direction, dd_physical_size), 0.75f));
  const dd_tuning *tuning = dd_server_tuning(s, c->tune_zone);

  switch (w) {
  case DD_WEAPON_ID_HAMMER: {
    if (c->core.hammer_hit_disabled) break;
    int ents[DD_PHYS_MAX_CLIENTS];
    int hits = 0;
    int num = dd_find_entities(s, proj_start_pos, dd_physical_size * 0.5f, ents, DD_PHYS_MAX_CLIENTS, DD_ENTTYPE_CHARACTER);
    for (int i = 0; i < num; ++i) {
      dd_character *target = &s->chars[s->ents[ents[i]].u.character];
      if (target == c || (target->alive && !dd_chr_can_collide(s, c, dd_chr_cid(target)))) continue;
      const dd_vec2 target_pos = dd_chr_pos(s, target);

      if (dd_v2_length(dd_v2_sub(target_pos, proj_start_pos)) > 0.0f)
        dd_server_event(s, DD_EVENT_HAMMERHIT,
                        dd_v2_sub(target_pos, dd_v2_mul(dd_v2_mul(dd_v2_normalize(dd_v2_sub(target_pos, proj_start_pos)), dd_physical_size), 0.5f)), -1,
                        0.0f, 0);
      else
        dd_server_event(s, DD_EVENT_HAMMERHIT, proj_start_pos, -1, 0.0f, 0);

      dd_vec2 dir;
      if (dd_v2_length(dd_v2_sub(target_pos, pos)) > 0.0f)
        dir = dd_v2_normalize(dd_v2_sub(target_pos, pos));
      else
        dir = dd_v2(0.f, -1.f);

      float strength = dd_tune(tuning->hammer_strength);
      dd_vec2 temp = dd_v2_add(target->core.vel, dd_v2_mul(dd_v2_normalize(dd_v2_add(dir, dd_v2(0.f, -1.1f))), 10.0f));
      temp = dd_clamp_vel(target->move_restrictions, temp);
      temp = dd_v2_sub(temp, target->core.vel);
      dd_chr_take_damage(s, target, dd_v2_mul(dd_v2_add(dd_v2(0.f, -1.0f), temp), strength), DD_HAMMER_DAMAGE);
      dd_chr_unfreeze(target);
      hits++;
    }
    if (hits) {
      float fire_delay = dd_tune(tuning->hammer_hit_fire_delay);
      c->reload_timer = (int)(fire_delay * DD_SERVER_TICK_SPEED / 1000);
    }
  } break;

  case DD_WEAPON_ID_GUN:
    if (!c->core.jetpack || !c->ninja_jetpack || c->core.has_telegun_gun) {
      int lifetime = (int)(DD_SERVER_TICK_SPEED * dd_tune(tuning->gun_lifetime));
      dd_projectile_new(s, DD_WEAPON_ID_GUN, cid, proj_start_pos, direction, lifetime, false, false, mouse_target, 0, 0);
    }
    break;

  case DD_WEAPON_ID_SHOTGUN: {
    float laser_reach = dd_tune(tuning->laser_reach);
    dd_laser_new(s, pos, direction, laser_reach, cid, DD_WEAPON_ID_SHOTGUN);
  } break;

  case DD_WEAPON_ID_GRENADE: {
    int lifetime = (int)(DD_SERVER_TICK_SPEED * dd_tune(tuning->grenade_lifetime));
    dd_projectile_new(s, DD_WEAPON_ID_GRENADE, cid, proj_start_pos, direction, lifetime, false, true, mouse_target, 0, 0);
  } break;

  case DD_WEAPON_ID_LASER: {
    float laser_reach = dd_tune(tuning->laser_reach);
    dd_laser_new(s, pos, direction, laser_reach, cid, DD_WEAPON_ID_LASER);
  } break;

  case DD_WEAPON_ID_NINJA:
    c->num_objects_hit = 0;
    c->core.ninja.activation_dir = direction;
    c->core.ninja.current_move_time = DD_NINJA_MOVETIME * DD_SERVER_TICK_SPEED / 1000;
    c->core.ninja.old_vel_amount = (int)dd_clampf(dd_v2_length(c->core.vel), 0.0f, 6000.0f);
    break;
  }

  c->attack_tick = s->tick;

  if (!c->reload_timer && c->core.active_weapon != -1)
    c->reload_timer = (int)(dd_tuning_fire_delay(tuning, c->core.active_weapon) * DD_SERVER_TICK_SPEED);
}

static void dd_chr_handle_ninja(dd_server *s, dd_character *c) {
  if (c->core.active_weapon != DD_WEAPON_ID_NINJA) return;

  if ((s->tick - c->core.ninja.activation_tick) > (DD_NINJA_DURATION * DD_SERVER_TICK_SPEED / 1000)) {
    dd_chr_remove_ninja(c);
    return;
  }

  int ninja_time = c->core.ninja.activation_tick + (DD_NINJA_DURATION * DD_SERVER_TICK_SPEED / 1000) - s->tick;
  if (ninja_time % DD_SERVER_TICK_SPEED == 0 && ninja_time / DD_SERVER_TICK_SPEED <= 5)
    dd_server_event(s, DD_EVENT_DAMAGEIND, dd_chr_pos(s, c), -1, 0.0f, ninja_time / DD_SERVER_TICK_SPEED);

  c->armor = dd_clampi(10 - (ninja_time / 15), 0, 10); /* CGameControllerDDNet::SetArmorProgress */

  dd_chr_set_weapon(c, DD_WEAPON_ID_NINJA);

  c->core.ninja.current_move_time--;

  if (c->core.ninja.current_move_time == 0)
    c->core.vel = dd_v2_mul(c->core.ninja.activation_dir, (float)c->core.ninja.old_vel_amount);

  if (c->core.ninja.current_move_time > 0) {
    c->core.vel = dd_v2_mul(c->core.ninja.activation_dir, (float)DD_NINJA_VELOCITY);
    dd_vec2 old_pos = dd_chr_pos(s, c);
    const dd_tuning *tuning = dd_server_tuning(s, c->tune_zone);
    dd_vec2 ground_elasticity = dd_v2(dd_tune(tuning->ground_elasticity_x), dd_tune(tuning->ground_elasticity_y));
    dd_col_move_box(s->col, &c->core.pos, &c->core.vel, dd_v2(dd_physical_size, dd_physical_size), ground_elasticity, NULL);

    c->core.vel = dd_v2(0, 0);

    int ents[DD_PHYS_MAX_CLIENTS];
    float radius = dd_physical_size * 2.0f;
    int num = dd_find_entities(s, old_pos, radius, ents, DD_PHYS_MAX_CLIENTS, DD_ENTTYPE_CHARACTER);

    if (dd_teams_get_solo(&s->teams, dd_chr_cid(c))) return;

    for (int i = 0; i < num; ++i) {
      dd_character *chr = &s->chars[s->ents[ents[i]].u.character];
      if (chr == c) continue;
      if (dd_chr_team(s, c) != dd_chr_team(s, chr)) continue;
      const int client_id = dd_chr_cid(chr);
      if (dd_teams_get_solo(&s->teams, client_id)) continue;
      bool already_hit = false;
      for (int j = 0; j < c->num_objects_hit; j++)
        if (c->hit_objects[j] == client_id) already_hit = true;
      if (already_hit) continue;
      if (dd_v2_distance(dd_chr_pos(s, chr), dd_chr_pos(s, c)) > radius) continue;
      c->hit_objects[c->num_objects_hit++] = client_id;
      dd_chr_take_damage(s, chr, dd_v2(0, -10.0f), DD_NINJA_DAMAGE);
    }
  }
}

static void dd_chr_handle_jetpack(dd_server *s, dd_character *c) {
  if (c->core.active_weapon < 0) return;
  dd_vec2 direction = dd_v2_normalize(dd_v2((float)c->latest_input.target_x, (float)c->latest_input.target_y));
  bool full_auto = false;
  const int w = c->core.active_weapon;
  if (w == DD_WEAPON_ID_GRENADE || w == DD_WEAPON_ID_SHOTGUN || w == DD_WEAPON_ID_LASER) full_auto = true;
  if (c->core.jetpack && w == DD_WEAPON_ID_GUN) full_auto = true;

  bool will_fire = false;
  if (dd_count_presses(c->latest_prev_input.fire, c->latest_input.fire)) will_fire = true;
  if (full_auto && (c->latest_input.fire & 1) && c->core.weapons[w].ammo) will_fire = true;
  if (!will_fire) return;
  if (!c->core.weapons[w].ammo || c->freeze_time) return;

  if (w == DD_WEAPON_ID_GUN && c->core.jetpack) {
    float strength = dd_tune(dd_server_tuning(s, c->tune_zone)->jetpack_strength);
    dd_chr_take_damage(s, c, dd_v2_mul(dd_v2_mul(direction, -1.0f), (strength / 100.0f / 6.11f)), 0);
  }
}

static void dd_chr_handle_weapons(dd_server *s, dd_character *c) {
  dd_chr_handle_ninja(s, c);
  dd_chr_handle_jetpack(s, c);
  if (c->pain_sound_timer > 0) c->pain_sound_timer--;
  if (c->reload_timer) {
    c->reload_timer--;
    return;
  }
  dd_chr_fire_weapon(s, c);
}

/* CCharacter::OnPredictedInput */
static void dd_chr_on_predicted_input(dd_server *s, dd_character *c, const dd_player_input *new_input) {
  if (memcmp(&c->saved_input, new_input, sizeof(dd_player_input)) != 0) c->last_action = s->tick;
  c->input = *new_input;
  if (c->input.target_x == 0 && c->input.target_y == 0) c->input.target_y = -1;
  c->saved_input = c->input;
}

/* CCharacter::OnDirectInput */
static void dd_chr_on_direct_input(dd_server *s, dd_character *c, const dd_player_input *new_input) {
  c->latest_prev_input = c->latest_input;
  c->latest_input = *new_input;
  c->num_inputs++;
  if (c->latest_input.target_x == 0 && c->latest_input.target_y == 0) c->latest_input.target_y = -1;
  if (c->num_inputs > 1) {
    dd_chr_handle_weapon_switch(c);
    dd_chr_fire_weapon(s, c);
  }
  c->latest_prev_prev_input = c->latest_prev_input;
  c->latest_prev_input = c->latest_input;
}

static void dd_chr_handle_tune_layer(dd_server *s, dd_character *c) {
  c->tune_zone_old = c->tune_zone;
  int current_index = dd_col_get_map_index(s->col, dd_chr_pos(s, c));
  c->tune_zone = dd_col_is_tune(s->col, current_index);
  c->core.tuning = s->tuning[c->tune_zone];
}

static bool dd_chr_death_tile_near(const dd_server *s, dd_vec2 p) {
  const float r = dd_physical_size / 3.f;
  return dd_col_get_collision_at(s->col, p.x + r, p.y - r) == DD_TILE_DEATH || dd_col_get_collision_at(s->col, p.x + r, p.y + r) == DD_TILE_DEATH ||
         dd_col_get_collision_at(s->col, p.x - r, p.y - r) == DD_TILE_DEATH || dd_col_get_collision_at(s->col, p.x - r, p.y + r) == DD_TILE_DEATH ||
         dd_col_get_front_collision_at(s->col, p.x + r, p.y - r) == DD_TILE_DEATH ||
         dd_col_get_front_collision_at(s->col, p.x + r, p.y + r) == DD_TILE_DEATH ||
         dd_col_get_front_collision_at(s->col, p.x - r, p.y - r) == DD_TILE_DEATH ||
         dd_col_get_front_collision_at(s->col, p.x - r, p.y + r) == DD_TILE_DEATH;
}

/* CCharacter::DDRaceTick */
static void dd_chr_ddrace_tick(dd_server *s, dd_character *c) {
  c->input = c->saved_input;
  if (c->input.direction != 0 || c->input.jump != 0) c->last_move = s->tick;
  c->armor = dd_clampi(10 - (c->freeze_time / 15), 0, 10); /* SetArmorProgress */

  if (c->core.live_frozen && !c->core.is_super && !c->core.invincible) {
    c->input.direction = 0;
    c->input.jump = 0;
  }
  if (c->freeze_time > 0) {
    if (c->freeze_time % DD_SERVER_TICK_SPEED == DD_SERVER_TICK_SPEED - 1)
      dd_server_event(s, DD_EVENT_DAMAGEIND, dd_chr_pos(s, c), -1, 0.0f, (c->freeze_time + 1) / DD_SERVER_TICK_SPEED);
    c->freeze_time--;
    c->input.direction = 0;
    c->input.jump = 0;
    c->input.hook = 0;
    if (c->freeze_time == 1) dd_chr_unfreeze(c);
  }

  dd_chr_handle_tune_layer(s, c);

  const dd_vec2 pos = dd_chr_pos(s, c);
  int index = dd_col_get_pure_map_index(s->col, pos.x, pos.y);
  const int tiles[3] = {dd_col_get_tile_index(s->col, index), dd_col_get_front_tile_index(s->col, index), dd_col_get_switch_type(s->col, index)};
  c->core.is_in_freeze = false;
  for (int i = 0; i < 3; ++i) {
    if (tiles[i] == DD_TILE_FREEZE || tiles[i] == DD_TILE_DFREEZE || tiles[i] == DD_TILE_LFREEZE || tiles[i] == DD_TILE_DEATH) {
      c->core.is_in_freeze = true;
      break;
    }
  }
  c->core.is_in_freeze |= dd_chr_death_tile_near(s, pos);

  /* TrySetRescue: only the rescue command reads it, which a demo cannot replay. */
  c->core.id = dd_chr_cid(c);
}

static bool dd_switch_status(const dd_server *s, int number, int team) {
  return number < s->core.num_switchers && s->core.switchers[number].status[team];
}

/* CCharacter::IsSwitchActiveCb */
static bool dd_chr_switch_active_cb(unsigned char number, void *user) {
  dd_server *s = ((dd_server **)user)[0];
  dd_character *c = ((dd_character **)user)[1];
  return s->core.num_switchers > 0 && dd_chr_team(s, c) != DD_PHYS_TEAM_SUPER && dd_switch_status(s, number, dd_chr_team(s, c));
}

/* CGameControllerDDNet::HandleCharacterTiles: start, finish, solo */
static void dd_controller_handle_character_tiles(dd_server *s, dd_character *c, int map_index) {
  const dd_collision *col = s->col;
  const int cid = dd_chr_cid(c);
  int tile_index = dd_col_get_tile_index(col, map_index);
  int tile_findex = dd_col_get_front_tile_index(col, map_index);
  const dd_vec2 p = dd_chr_pos(s, c);
  const float r = dd_physical_size / 3.f;
  int sens[4] = {dd_col_get_pure_map_index(col, p.x + r, p.y - r), dd_col_get_pure_map_index(col, p.x + r, p.y + r),
                 dd_col_get_pure_map_index(col, p.x - r, p.y - r), dd_col_get_pure_map_index(col, p.x - r, p.y + r)};
  bool start = tile_index == DD_TILE_START || tile_findex == DD_TILE_START;
  bool finish = tile_index == DD_TILE_FINISH || tile_findex == DD_TILE_FINISH;
  for (int i = 0; i < 4; ++i) {
    start |= dd_col_get_tile_index(col, sens[i]) == DD_TILE_START || dd_col_get_front_tile_index(col, sens[i]) == DD_TILE_START;
    finish |= dd_col_get_tile_index(col, sens[i]) == DD_TILE_FINISH || dd_col_get_front_tile_index(col, sens[i]) == DD_TILE_FINISH;
  }
  const int race_state = c->ddrace_state;
  if (start && race_state != DD_RACE_CHEATED) {
    if (s->cfg.sv_reset_pickups) dd_chr_reset_pickups(c);
    /* CGameTeams::OnCharacterStart, for the tee itself */
    if (!(s->cfg.sv_team == DD_SV_TEAM_FORCED_SOLO && race_state == DD_RACE_STARTED)) {
      c->ddrace_state = DD_RACE_STARTED;
      c->start_time = s->tick;
    }
  }
  if (finish && race_state == DD_RACE_STARTED) c->ddrace_state = DD_RACE_FINISHED;

  if ((tile_index == DD_TILE_SOLO_ENABLE || tile_findex == DD_TILE_SOLO_ENABLE) && !dd_teams_get_solo(&s->teams, cid))
    dd_chr_set_solo(s, c, true);
  else if ((tile_index == DD_TILE_SOLO_DISABLE || tile_findex == DD_TILE_SOLO_DISABLE) && dd_teams_get_solo(&s->teams, cid))
    dd_chr_set_solo(s, c, false);
}

static bool dd_chr_teleport_blocked(const dd_character *c) { return c->core.is_super || c->core.invincible; }

/* The spawn a check-teleport falls back to; the first spawn point. */
static bool dd_server_can_spawn(const dd_server *s, dd_vec2 *out) {
  for (int t = 0; t < 3; ++t)
    if (s->num_spawn_points[t] > 0) {
      *out = s->spawn_points[t][0];
      return true;
    }
  return false;
}

/* CCharacter::HandleTiles */
static void dd_chr_handle_tiles(dd_server *s, dd_character *c, int index) {
  const dd_collision *col = s->col;
  const int cid = dd_chr_cid(c);
  int map_index = index;
  c->tile_index = dd_col_get_tile_index(col, map_index);
  c->tile_findex = dd_col_get_front_tile_index(col, map_index);
  void *cb_user[2] = {s, c};
  c->move_restrictions = dd_col_get_move_restrictions(col, dd_chr_switch_active_cb, cb_user, dd_chr_pos(s, c), 18.0f, map_index);
  if (index < 0) {
    c->last_refill_jumps = false;
    c->last_penalty = false;
    c->last_bonus = false;
    return;
  }
  int tele_checkpoint = dd_col_is_tele_checkpoint(col, map_index);
  if (tele_checkpoint) c->tele_checkpoint = tele_checkpoint;

  dd_controller_handle_character_tiles(s, c, index);
  if (!c->alive) return;

  const int ti = c->tile_index, tf = c->tile_findex;
#define DD_TILE_IS(t) (ti == (t) || tf == (t))
  dd_character_core *core = &c->core;

  if (DD_TILE_IS(DD_TILE_FREEZE) && !core->is_super && !core->invincible && !core->deep_frozen)
    dd_chr_freeze(s, c);
  else if (DD_TILE_IS(DD_TILE_UNFREEZE) && !core->deep_frozen)
    dd_chr_unfreeze(c);

  if (DD_TILE_IS(DD_TILE_DFREEZE) && !core->is_super && !core->invincible && !core->deep_frozen)
    core->deep_frozen = true;
  else if (DD_TILE_IS(DD_TILE_DUNFREEZE) && !core->is_super && !core->invincible && core->deep_frozen)
    core->deep_frozen = false;

  if (DD_TILE_IS(DD_TILE_LFREEZE) && !core->is_super && !core->invincible)
    core->live_frozen = true;
  else if (DD_TILE_IS(DD_TILE_LUNFREEZE) && !core->is_super && !core->invincible)
    core->live_frozen = false;

  if (DD_TILE_IS(DD_TILE_EHOOK_ENABLE))
    core->endless_hook = true;
  else if (DD_TILE_IS(DD_TILE_EHOOK_DISABLE))
    core->endless_hook = false;

  if (DD_TILE_IS(DD_TILE_HIT_DISABLE) && (!core->hammer_hit_disabled || !core->shotgun_hit_disabled || !core->grenade_hit_disabled || !core->laser_hit_disabled)) {
    core->hammer_hit_disabled = core->shotgun_hit_disabled = core->grenade_hit_disabled = core->laser_hit_disabled = true;
  } else if (DD_TILE_IS(DD_TILE_HIT_ENABLE) && (core->hammer_hit_disabled || core->shotgun_hit_disabled || core->grenade_hit_disabled || core->laser_hit_disabled)) {
    core->shotgun_hit_disabled = core->grenade_hit_disabled = core->hammer_hit_disabled = core->laser_hit_disabled = false;
  }

  if (DD_TILE_IS(DD_TILE_NPC_DISABLE) && !core->collision_disabled)
    core->collision_disabled = true;
  else if (DD_TILE_IS(DD_TILE_NPC_ENABLE) && core->collision_disabled)
    core->collision_disabled = false;

  if (DD_TILE_IS(DD_TILE_NPH_DISABLE) && !core->hook_hit_disabled)
    core->hook_hit_disabled = true;
  else if (DD_TILE_IS(DD_TILE_NPH_ENABLE) && core->hook_hit_disabled)
    core->hook_hit_disabled = false;

  if (DD_TILE_IS(DD_TILE_UNLIMITED_JUMPS_ENABLE) && !core->endless_jump)
    core->endless_jump = true;
  else if (DD_TILE_IS(DD_TILE_UNLIMITED_JUMPS_DISABLE) && core->endless_jump)
    core->endless_jump = false;

  if (DD_TILE_IS(DD_TILE_WALLJUMP)) {
    if (core->vel.y > 0 && core->colliding && core->left_wall) {
      core->left_wall = false;
      core->jumped_total = core->jumps >= 2 ? core->jumps - 2 : 0;
      core->jumped = 1;
    }
  }

  if (DD_TILE_IS(DD_TILE_JETPACK_ENABLE) && !core->jetpack)
    core->jetpack = true;
  else if (DD_TILE_IS(DD_TILE_JETPACK_DISABLE) && core->jetpack)
    core->jetpack = false;

  if (DD_TILE_IS(DD_TILE_REFILL_JUMPS) && !c->last_refill_jumps) {
    core->jumped_total = 0;
    core->jumped = 0;
    c->last_refill_jumps = true;
  }
  if (ti != DD_TILE_REFILL_JUMPS && tf != DD_TILE_REFILL_JUMPS) c->last_refill_jumps = false;

  if (DD_TILE_IS(DD_TILE_TELE_GUN_ENABLE) && !core->has_telegun_gun)
    core->has_telegun_gun = true;
  else if (DD_TILE_IS(DD_TILE_TELE_GUN_DISABLE) && core->has_telegun_gun)
    core->has_telegun_gun = false;

  if (DD_TILE_IS(DD_TILE_TELE_GRENADE_ENABLE) && !core->has_telegun_grenade)
    core->has_telegun_grenade = true;
  else if (DD_TILE_IS(DD_TILE_TELE_GRENADE_DISABLE) && core->has_telegun_grenade)
    core->has_telegun_grenade = false;

  if (DD_TILE_IS(DD_TILE_TELE_LASER_ENABLE) && !core->has_telegun_laser)
    core->has_telegun_laser = true;
  else if (DD_TILE_IS(DD_TILE_TELE_LASER_DISABLE) && core->has_telegun_laser)
    core->has_telegun_laser = false;
#undef DD_TILE_IS

  /* stopper */
  if (core->vel.y > 0 && (c->move_restrictions & DD_CANTMOVE_DOWN)) {
    core->jumped = 0;
    core->jumped_total = 0;
  }
  dd_chr_apply_move_restrictions(c);

  /* switch tiles */
  const int switch_type = dd_col_get_switch_type(col, map_index);
  const int switch_number = dd_col_get_switch_number(col, map_index);
  const int switch_delay = dd_col_get_switch_delay(col, map_index);
  const int team = dd_chr_team(s, c);
  dd_switcher *sw = switch_number < s->core.num_switchers ? &s->core.switchers[switch_number] : NULL;
  if (switch_type == DD_TILE_SWITCHOPEN && team != DD_PHYS_TEAM_SUPER && switch_number > 0 && sw) {
    sw->status[team] = true;
    sw->end_tick[team] = 0;
    sw->type[team] = DD_TILE_SWITCHOPEN;
    sw->last_update_tick[team] = s->tick;
  } else if (switch_type == DD_TILE_SWITCHTIMEDOPEN && team != DD_PHYS_TEAM_SUPER && switch_number > 0 && sw) {
    sw->status[team] = true;
    sw->end_tick[team] = s->tick + 1 + switch_delay * DD_SERVER_TICK_SPEED;
    sw->type[team] = DD_TILE_SWITCHTIMEDOPEN;
    sw->last_update_tick[team] = s->tick;
  } else if (switch_type == DD_TILE_SWITCHTIMEDCLOSE && team != DD_PHYS_TEAM_SUPER && switch_number > 0 && sw) {
    sw->status[team] = false;
    sw->end_tick[team] = s->tick + 1 + switch_delay * DD_SERVER_TICK_SPEED;
    sw->type[team] = DD_TILE_SWITCHTIMEDCLOSE;
    sw->last_update_tick[team] = s->tick;
  } else if (switch_type == DD_TILE_SWITCHCLOSE && team != DD_PHYS_TEAM_SUPER && switch_number > 0 && sw) {
    sw->status[team] = false;
    sw->end_tick[team] = 0;
    sw->type[team] = DD_TILE_SWITCHCLOSE;
    sw->last_update_tick[team] = s->tick;
  } else if (switch_type == DD_TILE_FREEZE && team != DD_PHYS_TEAM_SUPER && !core->invincible) {
    if (switch_number == 0 || dd_switch_status(s, switch_number, team)) dd_chr_freeze_for(s, c, switch_delay);
  } else if (switch_type == DD_TILE_DFREEZE && team != DD_PHYS_TEAM_SUPER && !core->invincible) {
    if (switch_number == 0 || dd_switch_status(s, switch_number, team)) core->deep_frozen = true;
  } else if (switch_type == DD_TILE_DUNFREEZE && team != DD_PHYS_TEAM_SUPER && !core->invincible) {
    if (switch_number == 0 || dd_switch_status(s, switch_number, team)) core->deep_frozen = false;
  } else if (switch_type == DD_TILE_LFREEZE && team != DD_PHYS_TEAM_SUPER && !core->invincible) {
    if (switch_number == 0 || dd_switch_status(s, switch_number, team)) core->live_frozen = true;
  } else if (switch_type == DD_TILE_LUNFREEZE && team != DD_PHYS_TEAM_SUPER && !core->invincible) {
    if (switch_number == 0 || dd_switch_status(s, switch_number, team)) core->live_frozen = false;
  } else if (switch_type == DD_TILE_HIT_ENABLE && core->hammer_hit_disabled && switch_delay == DD_WEAPON_ID_HAMMER) {
    core->hammer_hit_disabled = false;
  } else if (switch_type == DD_TILE_HIT_DISABLE && !core->hammer_hit_disabled && switch_delay == DD_WEAPON_ID_HAMMER) {
    core->hammer_hit_disabled = true;
  } else if (switch_type == DD_TILE_HIT_ENABLE && core->shotgun_hit_disabled && switch_delay == DD_WEAPON_ID_SHOTGUN) {
    core->shotgun_hit_disabled = false;
  } else if (switch_type == DD_TILE_HIT_DISABLE && !core->shotgun_hit_disabled && switch_delay == DD_WEAPON_ID_SHOTGUN) {
    core->shotgun_hit_disabled = true;
  } else if (switch_type == DD_TILE_HIT_ENABLE && core->grenade_hit_disabled && switch_delay == DD_WEAPON_ID_GRENADE) {
    core->grenade_hit_disabled = false;
  } else if (switch_type == DD_TILE_HIT_DISABLE && !core->grenade_hit_disabled && switch_delay == DD_WEAPON_ID_GRENADE) {
    core->grenade_hit_disabled = true;
  } else if (switch_type == DD_TILE_HIT_ENABLE && core->laser_hit_disabled && switch_delay == DD_WEAPON_ID_LASER) {
    core->laser_hit_disabled = false;
  } else if (switch_type == DD_TILE_HIT_DISABLE && !core->laser_hit_disabled && switch_delay == DD_WEAPON_ID_LASER) {
    core->laser_hit_disabled = true;
  } else if (switch_type == DD_TILE_JUMP) {
    int new_jumps = switch_delay;
    if (new_jumps == 255) new_jumps = -1;
    if (new_jumps != core->jumps) core->jumps = new_jumps;
  } else if (switch_type == DD_TILE_ADD_TIME && !c->last_penalty) {
    const int minutes = switch_delay, seconds = switch_number;
    c->start_time -= (minutes * 60 + seconds) * DD_SERVER_TICK_SPEED;
    c->last_penalty = true;
  } else if (switch_type == DD_TILE_SUBTRACT_TIME && !c->last_bonus) {
    const int minutes = switch_delay, seconds = switch_number;
    c->start_time += (minutes * 60 + seconds) * DD_SERVER_TICK_SPEED;
    if (c->start_time > s->tick) c->start_time = s->tick;
    c->last_bonus = true;
  }
  if (switch_type != DD_TILE_ADD_TIME) c->last_penalty = false;
  if (switch_type != DD_TILE_SUBTRACT_TIME) c->last_bonus = false;

  /* teleporters */
  int z = dd_col_is_teleport(col, map_index);
  if (!s->cfg.sv_old_teleport_hook && !s->cfg.sv_old_teleport_weapons && z && col->tele_outs[z - 1].count > 0) {
    if (dd_chr_teleport_blocked(c)) return;
    int tele_out = dd_world_random_or0(&s->core, col->tele_outs[z - 1].count);
    core->pos = col->tele_outs[z - 1].positions[tele_out];
    if (!s->cfg.sv_teleport_hold_hook) dd_chr_reset_hook(c);
    if (s->cfg.sv_teleport_lose_weapons) dd_chr_reset_pickups(c);
    return;
  }
  const int evil_teleport = dd_col_is_evil_teleport(col, map_index);
  if (evil_teleport && col->tele_outs[evil_teleport - 1].count > 0) {
    if (dd_chr_teleport_blocked(c)) return;
    int tele_out = dd_world_random_or0(&s->core, col->tele_outs[evil_teleport - 1].count);
    core->pos = col->tele_outs[evil_teleport - 1].positions[tele_out];
    if (!s->cfg.sv_old_teleport_hook && !s->cfg.sv_old_teleport_weapons) {
      core->vel = dd_v2(0, 0);
      if (!s->cfg.sv_teleport_hold_hook) {
        dd_chr_reset_hook(c);
        dd_release_hooked(s, cid);
      }
      if (s->cfg.sv_teleport_lose_weapons) dd_chr_reset_pickups(c);
    }
    return;
  }
  const bool check_evil = dd_col_is_check_evil_teleport(col, map_index);
  if (check_evil || dd_col_is_check_teleport(col, map_index)) {
    if (dd_chr_teleport_blocked(c)) return;
    for (int k = c->tele_checkpoint - 1; k >= 0; k--) {
      if (col->tele_check_outs[k].count > 0) {
        int tele_out = dd_world_random_or0(&s->core, col->tele_check_outs[k].count);
        core->pos = col->tele_check_outs[k].positions[tele_out];
        if (check_evil) core->vel = dd_v2(0, 0);
        if (!s->cfg.sv_teleport_hold_hook) {
          dd_chr_reset_hook(c);
          if (check_evil) dd_release_hooked(s, cid);
        }
        return;
      }
    }
    dd_vec2 spawn_pos;
    if (dd_server_can_spawn(s, &spawn_pos)) {
      core->pos = spawn_pos;
      if (check_evil) core->vel = dd_v2(0, 0);
      if (!s->cfg.sv_teleport_hold_hook) {
        dd_chr_reset_hook(c);
        if (check_evil) dd_release_hooked(s, cid);
      }
    }
    return;
  }
}

/* CCharacter::HandleSkippableTiles */
static void dd_chr_handle_skippable_tiles(dd_server *s, dd_character *c, int index) {
  const dd_vec2 pos = dd_chr_pos(s, c);
  if (dd_chr_death_tile_near(s, pos) && !c->core.is_super && !c->core.invincible) {
    if (s->team_practice[dd_chr_team(s, c)]) {
      dd_chr_freeze(s, c);
    } else {
      dd_chr_die(s, c, dd_chr_cid(c), DD_WEAPON_WORLD);
      return;
    }
  }
  if (dd_game_layer_clipped(s, pos)) {
    dd_chr_die(s, c, dd_chr_cid(c), DD_WEAPON_WORLD);
    return;
  }
  if (index < 0) return;

  if (dd_col_is_speedup(s->col, index)) {
    dd_vec2 direction, temp_vel = c->core.vel;
    int force, type, max_speed = 0;
    dd_col_get_speedup(s->col, index, &direction, &force, &max_speed, &type);

    if (type == DD_TILE_SPEED_BOOST_OLD) {
      float tee_angle, speeder_angle, diff_angle, speed_left, tee_speed;
      if (force == 255 && max_speed) {
        c->core.vel = dd_v2_mul(direction, (float)(max_speed / 5));
      } else {
        if (max_speed > 0 && max_speed < 5) max_speed = 5;
        if (max_speed > 0) {
          if (direction.x > 0.0000001f)
            speeder_angle = -atanf(direction.y / direction.x);
          else if (direction.x < 0.0000001f)
            speeder_angle = atanf(direction.y / direction.x) + 2.0f * asinf(1.0f);
          else if (direction.y > 0.0000001f)
            speeder_angle = asinf(1.0f);
          else
            speeder_angle = asinf(-1.0f);
          if (speeder_angle < 0) speeder_angle = 4.0f * asinf(1.0f) + speeder_angle;

          if (temp_vel.x > 0.0000001f)
            tee_angle = -atanf(temp_vel.y / temp_vel.x);
          else if (temp_vel.x < 0.0000001f)
            tee_angle = atanf(temp_vel.y / temp_vel.x) + 2.0f * asinf(1.0f);
          else if (temp_vel.y > 0.0000001f)
            tee_angle = asinf(1.0f);
          else
            tee_angle = asinf(-1.0f);
          if (tee_angle < 0) tee_angle = 4.0f * asinf(1.0f) + tee_angle;

          /* std::pow(float, int) promotes to double in DDNet */
          tee_speed = (float)sqrt(pow((double)temp_vel.x, 2) + pow((double)temp_vel.y, 2));

          diff_angle = speeder_angle - tee_angle;
          speed_left = max_speed / 5.0f - cosf(diff_angle) * tee_speed;
          if (dd_absi((int)speed_left) > force && speed_left > 0.0000001f)
            temp_vel = dd_v2_add(temp_vel, dd_v2_mul(direction, (float)force));
          else if (dd_absi((int)speed_left) > force)
            temp_vel = dd_v2_add(temp_vel, dd_v2_mul(direction, (float)-force));
          else
            temp_vel = dd_v2_add(temp_vel, dd_v2_mul(direction, speed_left));
        } else {
          temp_vel = dd_v2_add(temp_vel, dd_v2_mul(direction, (float)force));
        }
        c->core.vel = dd_clamp_vel(c->move_restrictions, temp_vel);
      }
    } else if (type == DD_TILE_SPEED_BOOST) {
      const float max_speed_scale = 5.0f;
      const dd_tuning *tuning = dd_server_tuning(s, c->tune_zone);
      if (max_speed == 0) {
        /* DDNet calls unqualified log() with only <cmath>: the double overload */
        float max_ramp_speed =
            (float)((double)dd_tune(tuning->velramp_range) / (50 * log((double)dd_maxf(dd_tune(tuning->velramp_curvature), 1.01f))));
        max_speed = (int)(dd_maxf(max_ramp_speed, dd_tune(tuning->velramp_start) / 50) * max_speed_scale);
      }
      float current_directional_speed = dd_v2_dot(direction, c->core.vel);
      float temp_max_speed = max_speed / max_speed_scale;
      if (current_directional_speed + force > temp_max_speed)
        temp_vel = dd_v2_add(temp_vel, dd_v2_mul(direction, (temp_max_speed - current_directional_speed)));
      else
        temp_vel = dd_v2_add(temp_vel, dd_v2_mul(direction, (float)force));
      c->core.vel = dd_clamp_vel(c->move_restrictions, temp_vel);
    }
  }
}

/* CCharacter::DDRacePostCoreTick */
static void dd_chr_ddrace_post_core_tick(dd_server *s, dd_character *c) {
  dd_character_core *core = &c->core;
  if (core->endless_hook || (core->is_super && s->cfg.sv_endless_super_hook)) core->hook_tick = 0;

  c->frozen_last_tick = false;

  if (core->deep_frozen && !core->is_super && !core->invincible) dd_chr_freeze(s, c);

  if (core->jumps == -1)
    core->jumped |= 2;
  else if (core->jumps == 0)
    core->jumped |= 2;
  else if (core->jumps == 1 && core->jumped > 0)
    core->jumped |= 2;
  else if (core->jumped_total < core->jumps - 1 && core->jumped > 1)
    core->jumped = 1;

  if ((core->is_super || core->endless_jump) && core->jumped > 1) core->jumped = 1;

  int current_index = dd_col_get_map_index(s->col, dd_chr_pos(s, c));
  dd_chr_handle_skippable_tiles(s, c, current_index);
  if (!c->alive) return;

  static DD_THREAD_LOCAL int indices[DD_MAX_MAP_INDICES];
  int count = dd_col_get_map_indices(s->col, c->prev_pos, dd_chr_pos(s, c), indices);
  if (count > 0) {
    for (int i = 0; i < count; ++i) {
      dd_chr_handle_tiles(s, c, indices[i]);
      if (!c->alive) return;
    }
  } else {
    dd_chr_handle_tiles(s, c, current_index);
    if (!c->alive) return;
  }

  if (c->tele_gun_teleport) {
    dd_server_event(s, DD_EVENT_DEATH, dd_chr_pos(s, c), dd_chr_cid(c), 0.0f, 0);
    core->pos = c->tele_gun_pos;
    if (!c->is_blue_tele_gun_teleport) core->vel = dd_v2(0, 0);
    dd_server_event(s, DD_EVENT_DEATH, c->tele_gun_pos, dd_chr_cid(c), 0.0f, 0);
    c->tele_gun_teleport = false;
    c->is_blue_tele_gun_teleport = false;
  }
}

/* CCharacter::PreTick */
static void dd_chr_pre_tick(dd_server *s, dd_character *c) {
  if (c->start_time > s->tick) {
    dd_chr_die(s, c, dd_chr_cid(c), DD_WEAPON_WORLD);
    return;
  }
  if (c->emote_stop < s->tick) dd_chr_set_emote(c, 0, -1);

  dd_chr_ddrace_tick(s, c);

  c->core.input = c->input;
  dd_core_tick(&c->core, true, !s->cfg.sv_no_weak_hook);
}

/* CCharacter::Tick */
static void dd_chr_tick(dd_server *s, dd_character *c) {
  if (s->cfg.sv_no_weak_hook)
    dd_core_tick_deferred(&c->core);
  else
    dd_chr_pre_tick(s, c);
  if (!c->alive) return;

  dd_chr_handle_weapons(s, c);

  dd_chr_ddrace_post_core_tick(s, c);
  if (!c->alive) return;

  c->prev_input = c->input;
  c->prev_pos = c->core.pos;
}

/* CCharacter::TickDeferred, without the reckoning bookkeeping */
static void dd_chr_tick_deferred(dd_server *s, dd_character *c) {
  c->core.id = dd_chr_cid(c);
  dd_core_move(&c->core);
  dd_core_quantize(&c->core);
  dd_chr_set_pos(s, c, c->core.pos);
  if (c->core.triggered_events & DD_COREEVENT_HOOK_ATTACH_PLAYER) {
    /* sound only */
  }
}

/* Creates a character for `cid` at `pos`, as CCharacter::Spawn does, with
 * DDNet's defaults. The reconstruction overwrites its state from the demo. */
static dd_character *dd_server_spawn_character(dd_server *s, int cid, dd_vec2 pos) {
  dd_character *c = &s->chars[cid];
  memset(c, 0, sizeof(*c));
  s->chars_touched |= (uint64_t)1 << cid;
  c->ent = dd_ent_alloc(s, DD_ENT_CHARACTER, DD_ENTTYPE_CHARACTER, pos, dd_physical_size);
  if (c->ent < 0) return NULL;
  s->ents[c->ent].u.character = cid;
  c->emote_stop = -1;
  c->last_action = -1;
  c->last_weapon = DD_WEAPON_ID_HAMMER;
  c->queued_weapon = -1;
  c->input.target_y = -1;
  c->latest_prev_prev_input.target_y = -1;
  c->latest_prev_input = c->latest_input = c->prev_input = c->saved_input = c->input;
  c->spawn_tick = s->tick;
  c->weapon_change_tick = s->tick;

  dd_core_init(&c->core, &s->core, s->col, &s->teams);
  dd_core_reset(&c->core);
  c->core.active_weapon = DD_WEAPON_ID_GUN;
  c->core.pos = pos;
  c->core.id = cid;
  c->core.weapons[DD_WEAPON_ID_HAMMER].got = true;
  c->core.weapons[DD_WEAPON_ID_HAMMER].ammo = -1;
  c->core.weapons[DD_WEAPON_ID_GUN].got = true;
  c->core.weapons[DD_WEAPON_ID_GUN].ammo = -1;
  int tune_zone = dd_col_is_tune(s->col, dd_col_get_map_index(s->col, pos));
  c->core.tuning = s->tuning[tune_zone];
  dd_world_set_character(&s->core, cid, &c->core);
  dd_ent_insert(s, c->ent);
  c->alive = true;

  /* DDRaceInit */
  c->prev_pos = pos;
  c->tele_checkpoint = 0;
  c->core.endless_hook = s->cfg.sv_endless_drag;
  const bool no_hit = !s->cfg.sv_hit;
  c->core.hammer_hit_disabled = c->core.shotgun_hit_disabled = c->core.grenade_hit_disabled = c->core.laser_hit_disabled = no_hit;
  c->core.jumps = 2;
  c->tune_zone = tune_zone;
  c->tune_zone_old = -1;
  c->health = 10;
  return c;
}

/* ======================================================================== */
/* explosions */

static void dd_server_create_explosion(dd_server *s, dd_vec2 pos, int owner, int weapon, bool no_damage, int activated_team) {
  (void)weapon;
  dd_server_event(s, DD_EVENT_EXPLOSION, pos, -1, 0.0f, 0);

  int ents[DD_PHYS_MAX_CLIENTS];
  float radius = 135.0f;
  float inner_radius = 48.0f;
  int num = dd_find_entities(s, pos, radius, ents, DD_PHYS_MAX_CLIENTS, DD_ENTTYPE_CHARACTER);
  bool team_done[DD_PHYS_NUM_DDRACE_TEAMS] = {0};
  dd_character *owner_chr = dd_chr(s, owner);
  for (int i = 0; i < num; i++) {
    dd_character *chr = &s->chars[s->ents[ents[i]].u.character];
    dd_vec2 diff = dd_v2_sub(dd_chr_pos(s, chr), pos);
    dd_vec2 force_dir = dd_v2(0, 1);
    float l = dd_v2_length(diff);
    if (l != 0.0f) force_dir = dd_v2_normalize(diff);
    l = 1 - dd_clampf((l - inner_radius) / (radius - inner_radius), 0.0f, 1.0f);
    float strength;
    if (owner == -1 || owner >= DD_PHYS_MAX_CLIENTS || !s->players[owner].tune_zone)
      strength = dd_tune(s->tuning[0].explosion_strength);
    else
      strength = dd_tune(s->tuning[s->players[owner].tune_zone].explosion_strength);

    float dmg = strength * l;
    if (!(int)dmg) continue;

    if ((owner_chr ? !owner_chr->core.grenade_hit_disabled : s->cfg.sv_hit) || no_damage || owner == dd_chr_cid(chr)) {
      if (owner != -1 && chr->alive && !dd_chr_can_collide(s, chr, owner)) continue;
      if (owner == -1 && activated_team != -1 && chr->alive && dd_chr_team(s, chr) != activated_team) continue;

      int player_team = dd_chr_team(s, chr);
      if ((owner_chr ? owner_chr->core.grenade_hit_disabled : !s->cfg.sv_hit) || no_damage) {
        if (player_team == DD_PHYS_TEAM_SUPER) continue;
        if (team_done[player_team]) continue;
        team_done[player_team] = true;
      }
      dd_chr_take_damage(s, chr, dd_v2_mul(dd_v2_mul(force_dir, dmg), 2), (int)dmg);
    }
  }
}

/* ======================================================================== */
/* projectiles (entities/projectile.cpp) */

/* CalcPos (gamecore.h) */
static dd_vec2 dd_calc_pos(dd_vec2 pos, dd_vec2 velocity, float curvature, float speed, float time) {
  dd_vec2 n;
  time *= speed;
  n.x = pos.x + velocity.x * time;
  n.y = pos.y + velocity.y * time + curvature / 10000 * (time * time);
  return n;
}

static int dd_projectile_new(dd_server *s, int type, int owner, dd_vec2 pos, dd_vec2 dir, int span, bool freeze, bool explosive,
                             dd_vec2 init_dir, int layer, int number) {
  int index = dd_ent_alloc(s, DD_ENT_PROJECTILE, DD_ENTTYPE_PROJECTILE, pos, 0);
  if (index < 0) return -1;
  dd_entity *e = &s->ents[index];
  e->u.projectile.type = type;
  e->u.projectile.direction = dir;
  e->u.projectile.life_span = span;
  e->u.projectile.owner = owner;
  e->u.projectile.start_tick = s->tick;
  e->u.projectile.explosive = explosive;
  e->u.projectile.layer = layer;
  e->u.projectile.number = number;
  e->u.projectile.bouncing = 0;
  e->u.projectile.freeze = freeze;
  e->u.projectile.init_dir = init_dir;
  e->u.projectile.tune_zone = dd_col_is_tune(s->col, dd_col_get_map_index(s->col, pos));
  dd_character *owner_chr = dd_chr(s, owner);
  e->u.projectile.belongs_to_practice_team = owner_chr && s->team_practice[dd_chr_team(s, owner_chr)];
  e->u.projectile.ddrace_team = owner < 0 || owner >= DD_PHYS_MAX_CLIENTS ? 0 : dd_teams_team(&s->teams, owner);
  e->u.projectile.is_solo = owner_chr && owner_chr->core.solo;
  dd_ent_insert(s, index);
  return index;
}

static dd_vec2 dd_projectile_get_pos(const dd_server *s, const dd_entity *e, float time) {
  float curvature = 0, speed = 0;
  const dd_tuning *t = dd_server_tuning(s, e->u.projectile.tune_zone);
  switch (e->u.projectile.type) {
  case DD_WEAPON_ID_GRENADE:
    curvature = dd_tune(t->grenade_curvature);
    speed = dd_tune(t->grenade_speed);
    break;
  case DD_WEAPON_ID_SHOTGUN:
    curvature = dd_tune(t->shotgun_curvature);
    speed = dd_tune(t->shotgun_speed);
    break;
  case DD_WEAPON_ID_GUN:
    curvature = dd_tune(t->gun_curvature);
    speed = dd_tune(t->gun_speed);
    break;
  }
  return dd_calc_pos(e->pos, e->u.projectile.direction, curvature, speed, time);
}

static void dd_projectile_tick(dd_server *s, int index) {
  dd_entity *e = &s->ents[index];
  const dd_collision *col = s->col;
  float pt = (s->tick - e->u.projectile.start_tick - 1) / (float)DD_SERVER_TICK_SPEED;
  float ct = (s->tick - e->u.projectile.start_tick) / (float)DD_SERVER_TICK_SPEED;
  dd_vec2 prev_pos = dd_projectile_get_pos(s, e, pt);
  dd_vec2 cur_pos = dd_projectile_get_pos(s, e, ct);
  dd_vec2 col_pos, new_pos;
  int collide = dd_col_intersect_line(col, prev_pos, cur_pos, &col_pos, &new_pos);
  const int owner = e->u.projectile.owner;
  dd_character *owner_chr = owner >= 0 ? dd_chr(s, owner) : NULL;

  int target = -1;
  if (owner_chr ? !owner_chr->core.grenade_hit_disabled : s->cfg.sv_hit)
    target = dd_intersect_character(s, prev_pos, col_pos, e->u.projectile.freeze ? 1.0f : 6.0f, &col_pos, owner_chr ? owner : -2, owner, -1);
  dd_character *target_chr = target >= 0 ? &s->chars[target] : NULL;

  if (e->u.projectile.life_span > -1) e->u.projectile.life_span--;

  bool is_weapon_collide = false;
  if (owner_chr && target_chr && owner_chr->alive && target_chr->alive && !dd_chr_can_collide(s, target_chr, owner)) is_weapon_collide = true;

  if (!(owner_chr && owner_chr->alive) && owner >= 0 &&
      (e->u.projectile.type != DD_WEAPON_ID_GRENADE || s->cfg.sv_destroy_bullets_on_death || e->u.projectile.belongs_to_practice_team)) {
    e->marked_for_destroy = true;
    return;
  }

  if (((target_chr && (owner_chr ? !owner_chr->core.grenade_hit_disabled : s->cfg.sv_hit || owner == -1 || target_chr == owner_chr)) || collide ||
       dd_game_layer_clipped(s, cur_pos)) &&
      !is_weapon_collide) {
    if (e->u.projectile.explosive &&
        (!target_chr || (target_chr && (!e->u.projectile.freeze || (e->u.projectile.type == DD_WEAPON_ID_SHOTGUN && collide))))) {
      int number = 1;
      if (s->cfg.bug_grenade_double_explosion && e->u.projectile.life_span == -1) number = 2;
      for (int i = 0; i < number; i++)
        dd_server_create_explosion(s, col_pos, owner, e->u.projectile.type, owner == -1, (!target_chr ? -1 : dd_chr_team(s, target_chr)));
      e = &s->ents[index];
    } else if (e->u.projectile.freeze) {
      int ents[DD_PHYS_MAX_CLIENTS];
      int num = dd_find_entities(s, cur_pos, 1.0f, ents, DD_PHYS_MAX_CLIENTS, DD_ENTTYPE_CHARACTER);
      for (int i = 0; i < num; ++i) {
        dd_character *chr = &s->chars[s->ents[ents[i]].u.character];
        if (e->u.projectile.layer != DD_LAYER_SWITCH ||
            (e->u.projectile.layer == DD_LAYER_SWITCH && e->u.projectile.number > 0 && dd_switch_status(s, e->u.projectile.number, dd_chr_team(s, chr))))
          dd_chr_freeze(s, chr);
      }
    } else if (target_chr) {
      dd_chr_take_damage(s, target_chr, dd_v2(0, 0), 0);
    }

    if (owner_chr && !dd_game_layer_clipped(s, col_pos) &&
        ((e->u.projectile.type == DD_WEAPON_ID_GRENADE && owner_chr->core.has_telegun_grenade) ||
         (e->u.projectile.type == DD_WEAPON_ID_GUN && owner_chr->core.has_telegun_gun))) {
      const dd_vec2 at = target_chr ? dd_chr_pos(s, target_chr) : col_pos;
      int map_index = dd_col_get_pure_map_index(col, at.x, at.y);
      int tile_findex = dd_col_get_front_tile_index(col, map_index);
      bool is_switch_tele_gun = dd_col_get_switch_type(col, map_index) == DD_TILE_ALLOW_TELE_GUN;
      bool is_blue_switch_tele_gun = dd_col_get_switch_type(col, map_index) == DD_TILE_ALLOW_BLUE_TELE_GUN;
      if (is_switch_tele_gun || is_blue_switch_tele_gun) {
        int delay = dd_col_get_switch_delay(col, map_index);
        if (delay == 1 && e->u.projectile.type != DD_WEAPON_ID_GUN) is_switch_tele_gun = is_blue_switch_tele_gun = false;
        if (delay == 2 && e->u.projectile.type != DD_WEAPON_ID_GRENADE) is_switch_tele_gun = is_blue_switch_tele_gun = false;
        if (delay == 3 && e->u.projectile.type != DD_WEAPON_ID_LASER) is_switch_tele_gun = is_blue_switch_tele_gun = false;
      }
      if (tile_findex == DD_TILE_ALLOW_TELE_GUN || tile_findex == DD_TILE_ALLOW_BLUE_TELE_GUN || is_switch_tele_gun || is_blue_switch_tele_gun ||
          target_chr) {
        bool found;
        dd_vec2 possible_pos;
        if (!collide)
          found = dd_get_nearest_air_pos_player(s, target_chr ? dd_chr_pos(s, target_chr) : col_pos, &possible_pos);
        else
          found = dd_get_nearest_air_pos(s, new_pos, cur_pos, &possible_pos);
        if (found) {
          owner_chr->tele_gun_pos = possible_pos;
          owner_chr->tele_gun_teleport = true;
          owner_chr->is_blue_tele_gun_teleport = tile_findex == DD_TILE_ALLOW_BLUE_TELE_GUN || is_blue_switch_tele_gun;
        }
      }
    }

    if (collide && e->u.projectile.bouncing != 0) {
      e->u.projectile.start_tick = s->tick;
      e->pos = dd_v2_add(new_pos, dd_v2_mul(dd_v2_mul(e->u.projectile.direction, 4), -1.0f));
      if (e->u.projectile.bouncing == 1)
        e->u.projectile.direction.x = -e->u.projectile.direction.x;
      else if (e->u.projectile.bouncing == 2)
        e->u.projectile.direction.y = -e->u.projectile.direction.y;
      if (dd_absf(e->u.projectile.direction.x) < 1e-6f) e->u.projectile.direction.x = 0;
      if (dd_absf(e->u.projectile.direction.y) < 1e-6f) e->u.projectile.direction.y = 0;
      e->pos = dd_v2_add(e->pos, e->u.projectile.direction);
    } else if (e->u.projectile.type == DD_WEAPON_ID_GUN) {
      dd_server_event(s, DD_EVENT_DAMAGEIND, cur_pos, -1, -atan2f(e->u.projectile.direction.x, e->u.projectile.direction.y), 10);
      e->marked_for_destroy = true;
      return;
    } else {
      if (!e->u.projectile.freeze) {
        e->marked_for_destroy = true;
        return;
      }
    }
  }

  if (e->u.projectile.life_span == -1) {
    if (e->u.projectile.explosive) {
      dd_server_create_explosion(s, col_pos, owner, e->u.projectile.type, owner == -1, (!owner_chr ? -1 : dd_chr_team(s, owner_chr)));
      e = &s->ents[index];
    }
    e->marked_for_destroy = true;
    return;
  }

  /* GetIndex(PrevPos, CurPos) */
  int x;
  {
    float distance = dd_v2_distance(prev_pos, cur_pos);
    x = -1;
    if (distance == 0.0f) {
      int nx = dd_clampi((int)cur_pos.x / 32, 0, col->width - 1);
      int ny = dd_clampi((int)cur_pos.y / 32, 0, col->height - 1);
      if (col->tele_type || (col->speedup_force && col->speedup_force[ny * col->width + nx] > 0)) x = ny * col->width + nx;
    }
    if (x < 0) {
      const int distance_rounded = (int)ceilf(distance);
      for (int i = 0; i < distance_rounded; i++) {
        float a = (float)i / distance;
        dd_vec2 tmp = dd_v2_mix(prev_pos, cur_pos, a);
        int nx = dd_clampi((int)tmp.x / 32, 0, col->width - 1);
        int ny = dd_clampi((int)tmp.y / 32, 0, col->height - 1);
        if (col->tele_type || (col->speedup_force && col->speedup_force[ny * col->width + nx] > 0)) {
          x = ny * col->width + nx;
          break;
        }
      }
    }
  }
  int z = s->cfg.sv_old_teleport_weapons ? dd_col_is_teleport(col, x) : dd_col_is_teleport_weapon(col, x);
  if (z && col->tele_outs[z - 1].count > 0) {
    int tele_out = dd_world_random_or0(&s->core, col->tele_outs[z - 1].count);
    e->pos = col->tele_outs[z - 1].positions[tele_out];
    e->u.projectile.start_tick = s->tick;
  }
}

/* ======================================================================== */
/* lasers (entities/laser.cpp) */

static void dd_laser_sync_interact_state(dd_server *s, dd_entity *e) {
  const int owner = e->u.laser.owner;
  dd_character *owner_chr = dd_chr(s, owner);
  /* A player, alive or not, exists while their client is connected. The
   * reconstruction treats every known client as connected. */
  if (owner >= 0 && owner < DD_PHYS_MAX_CLIENTS) {
    bool no_hit_others = s->cfg.sv_hit;
    if (owner_chr)
      no_hit_others = (e->u.laser.type == DD_WEAPON_ID_LASER && owner_chr->core.laser_hit_disabled) ||
                      (e->u.laser.type == DD_WEAPON_ID_SHOTGUN && owner_chr->core.shotgun_hit_disabled);
    bool no_hit_self = s->cfg.sv_old_laser || (e->u.laser.bounces == 0 && !e->u.laser.was_tele);
    e->u.laser.interact.owner_alive = owner_chr && owner_chr->alive;
    e->u.laser.interact.ddrace_team = dd_teams_team(&s->teams, owner);
    e->u.laser.interact.solo = owner_chr && owner_chr->core.solo;
    e->u.laser.interact.no_hit_others = no_hit_others;
    e->u.laser.interact.no_hit_self = no_hit_self;
  } else {
    e->u.laser.interact.owner_alive = false;
    e->u.laser.interact.owner_id = -1;
  }
}

/* CInteractions::CanHit; unique ids equal client ids within one demo */
static bool dd_interact_can_hit(const dd_server *s, const dd_interactions *st, int client_id) {
  if (client_id < 0 || client_id >= DD_PHYS_MAX_CLIENTS) return false;
  if (st->ddrace_team && dd_teams_team(&s->teams, client_id) != st->ddrace_team) return false;
  if (st->solo && st->owner_id != client_id) return false;
  if (st->no_hit_others && st->owner_id != client_id) return false;
  if (st->no_hit_self && st->owner_id == client_id) return false;
  return true;
}

static bool dd_laser_hit_character(dd_server *s, int index, dd_vec2 from, dd_vec2 to) {
  dd_entity *e = &s->ents[index];
  dd_laser_sync_interact_state(s, e);
  dd_vec2 at;
  const int owner = e->u.laser.owner;
  dd_character *owner_chr = dd_chr(s, owner);
  bool dont_hit_self = s->cfg.sv_old_laser || (e->u.laser.bounces == 0 && !e->u.laser.was_tele);
  const int not_this = dont_hit_self && owner_chr ? owner : -2;
  int hit;
  if (owner_chr ? (!owner_chr->core.laser_hit_disabled && e->u.laser.type == DD_WEAPON_ID_LASER) ||
                      (!owner_chr->core.shotgun_hit_disabled && e->u.laser.type == DD_WEAPON_ID_SHOTGUN)
                : s->cfg.sv_hit)
    hit = dd_intersect_character(s, e->pos, to, 0.f, &at, not_this, owner, -1);
  else
    hit = dd_intersect_character(s, e->pos, to, 0.f, &at, not_this, owner, owner_chr ? owner : -2);

  if (hit < 0 || !dd_interact_can_hit(s, &e->u.laser.interact, hit)) return false;
  dd_character *hit_chr = &s->chars[hit];
  e->u.laser.from = from;
  e->pos = at;
  e->u.laser.energy = -1;
  if (e->u.laser.type == DD_WEAPON_ID_SHOTGUN) {
    float strength = dd_tune(s->tuning[e->u.laser.tune_zone].shotgun_strength);
    const dd_vec2 hit_pos = hit_chr->core.pos;
    if (!s->cfg.sv_old_laser) {
      if (!dd_v2_eq(e->u.laser.prev_pos, hit_pos))
        dd_chr_add_velocity(hit_chr, dd_v2_mul(dd_v2_normalize(dd_v2_sub(e->u.laser.prev_pos, hit_pos)), strength));
      else
        hit_chr->core.vel = dd_v2(-2147483648.0f, -2147483648.0f);
    } else if (s->cfg.sv_old_laser && owner_chr) {
      if (!dd_v2_eq(owner_chr->core.pos, hit_pos))
        dd_chr_add_velocity(hit_chr, dd_v2_mul(dd_v2_normalize(dd_v2_sub(owner_chr->core.pos, hit_pos)), strength));
      else
        hit_chr->core.vel = dd_v2(-2147483648.0f, -2147483648.0f);
    } else {
      dd_chr_apply_move_restrictions(hit_chr);
    }
  } else if (e->u.laser.type == DD_WEAPON_ID_LASER) {
    dd_chr_unfreeze(hit_chr);
  }
  dd_chr_take_damage(s, hit_chr, dd_v2(0, 0), 0);
  return true;
}

static void dd_laser_do_bounce(dd_server *s, int index) {
  dd_entity *e = &s->ents[index];
  const dd_collision *col = s->col;
  e->u.laser.eval_tick = s->tick;

  if (e->u.laser.energy < 0) {
    e->marked_for_destroy = true;
    return;
  }
  e->u.laser.prev_pos = e->pos;
  dd_vec2 coltile;
  int res, z;

  if (e->u.laser.was_tele) {
    e->u.laser.prev_pos = e->u.laser.tele_pos;
    e->pos = e->u.laser.tele_pos;
    e->u.laser.tele_pos = dd_v2(0, 0);
  }

  dd_vec2 to = dd_v2_add(e->pos, dd_v2_mul(e->u.laser.dir, e->u.laser.energy));
  res = dd_col_intersect_line_tele_weapon(col, e->pos, to, &coltile, &to, &z);

  if (res) {
    if (!dd_laser_hit_character(s, index, e->pos, to)) {
      e = &s->ents[index];
      e->u.laser.from = e->pos;
      e->pos = to;
      dd_vec2 temp_pos = e->pos;
      dd_vec2 temp_dir = dd_v2_mul(e->u.laser.dir, 4.0f);
      /* DDNet's Res == -1 branch cannot happen: IntersectLineTeleWeapon never returns -1. */
      dd_col_move_point(col, &temp_pos, &temp_dir, 1.0f, NULL);
      e->pos = temp_pos;
      e->u.laser.dir = dd_v2_normalize(temp_dir);

      const float distance = dd_v2_distance(e->u.laser.from, e->pos);
      if (distance == 0.0f && e->u.laser.zero_energy_bounce_in_last_tick)
        e->u.laser.energy = -1;
      else
        e->u.laser.energy -= distance + dd_tune(s->tuning[e->u.laser.tune_zone].laser_bounce_cost);
      e->u.laser.zero_energy_bounce_in_last_tick = distance == 0.0f;

      if (res == DD_TILE_TELEINWEAPON && col->tele_outs[z - 1].count > 0) {
        int tele_out = dd_world_random_or0(&s->core, col->tele_outs[z - 1].count);
        e->u.laser.tele_pos = col->tele_outs[z - 1].positions[tele_out];
        e->u.laser.was_tele = true;
      } else {
        e->u.laser.bounces++;
        e->u.laser.was_tele = false;
      }

      int bounce_num = (int)dd_tune(s->tuning[e->u.laser.tune_zone].laser_bounce_num);
      if (e->u.laser.bounces > bounce_num) e->u.laser.energy = -1;
    }
  } else {
    if (!dd_laser_hit_character(s, index, e->pos, to)) {
      e = &s->ents[index];
      e->u.laser.from = e->pos;
      e->pos = to;
      e->u.laser.energy = -1;
    }
  }
  e = &s->ents[index];

  const int owner = e->u.laser.owner;
  dd_character *owner_chr = dd_chr(s, owner);
  if (owner >= 0 && e->u.laser.energy <= 0 && !e->u.laser.teleport_cancelled && owner_chr && owner_chr->alive &&
      owner_chr->core.has_telegun_laser && e->u.laser.type == DD_WEAPON_ID_LASER) {
    dd_vec2 possible_pos;
    bool found = false;
    bool dont_hit_self = s->cfg.sv_old_laser || (e->u.laser.bounces == 0 && !e->u.laser.was_tele);
    const int not_this = dont_hit_self ? owner : -2;
    dd_vec2 at;
    int hit;
    if (!owner_chr->core.laser_hit_disabled && e->u.laser.type == DD_WEAPON_ID_LASER)
      hit = dd_intersect_character(s, e->pos, to, 0.f, &at, not_this, owner, -1);
    else
      hit = dd_intersect_character(s, e->pos, to, 0.f, &at, not_this, owner, owner);
    if (hit >= 0)
      found = dd_get_nearest_air_pos_player(s, dd_chr_pos(s, &s->chars[hit]), &possible_pos);
    else
      found = dd_get_nearest_air_pos(s, e->pos, e->u.laser.from, &possible_pos);
    if (found) {
      owner_chr->tele_gun_pos = possible_pos;
      owner_chr->tele_gun_teleport = true;
      owner_chr->is_blue_tele_gun_teleport = e->u.laser.is_blue_teleport;
    }
  } else if (owner >= 0) {
    int map_index = dd_col_get_pure_map_index(col, coltile.x, coltile.y);
    int tile_findex = dd_col_get_front_tile_index(col, map_index);
    bool is_switch_tele_gun = dd_col_get_switch_type(col, map_index) == DD_TILE_ALLOW_TELE_GUN;
    bool is_blue_switch_tele_gun = dd_col_get_switch_type(col, map_index) == DD_TILE_ALLOW_BLUE_TELE_GUN;
    int is_tele_in_weapon = dd_col_is_teleport_weapon(col, map_index);
    if (!is_tele_in_weapon) {
      if (is_switch_tele_gun || is_blue_switch_tele_gun) {
        const int delay = dd_col_get_switch_delay(col, map_index);
        if ((delay != 3 && delay != 0) && e->u.laser.type == DD_WEAPON_ID_LASER) is_switch_tele_gun = is_blue_switch_tele_gun = false;
      }
      e->u.laser.is_blue_teleport = tile_findex == DD_TILE_ALLOW_BLUE_TELE_GUN || is_blue_switch_tele_gun;
      e->u.laser.teleport_cancelled =
          e->u.laser.type == DD_WEAPON_ID_LASER &&
          (tile_findex != DD_TILE_ALLOW_TELE_GUN && tile_findex != DD_TILE_ALLOW_BLUE_TELE_GUN && !is_switch_tele_gun && !is_blue_switch_tele_gun);
    }
  }
}

static int dd_laser_new(dd_server *s, dd_vec2 pos, dd_vec2 direction, float start_energy, int owner, int type) {
  int index = dd_ent_alloc(s, DD_ENT_LASER, DD_ENTTYPE_LASER, pos, 0);
  if (index < 0) return -1;
  dd_entity *e = &s->ents[index];
  e->u.laser.number = 0;
  e->u.laser.layer = DD_LAYER_GAME;
  e->u.laser.owner = owner;
  e->u.laser.energy = start_energy;
  e->u.laser.dir = direction;
  e->u.laser.type = type;
  e->u.laser.tune_zone = dd_col_is_tune(s->col, dd_col_get_map_index(s->col, pos));
  dd_character *owner_chr = dd_chr(s, owner);
  e->u.laser.belongs_to_practice_team = owner_chr && s->team_practice[dd_chr_team(s, owner_chr)];
  e->u.laser.interact.owner_id = owner;
  dd_laser_sync_interact_state(s, e);
  dd_ent_insert(s, index);
  dd_laser_do_bounce(s, index);
  return index;
}

static void dd_laser_tick(dd_server *s, int index) {
  dd_entity *e = &s->ents[index];
  dd_laser_sync_interact_state(s, e);
  if ((s->cfg.sv_destroy_lasers_on_death || e->u.laser.belongs_to_practice_team) && e->u.laser.owner >= 0) {
    dd_character *owner_chr = dd_chr(s, e->u.laser.owner);
    if (!(owner_chr && owner_chr->alive)) e->marked_for_destroy = true;
  }
  float delay = dd_tune(s->tuning[e->u.laser.tune_zone].laser_bounce_delay);
  if ((s->tick - e->u.laser.eval_tick) > (DD_SERVER_TICK_SPEED * delay / 1000.0f)) dd_laser_do_bounce(s, index);
}

/* ======================================================================== */
/* pickups, doors, draggers, turrets, plasma, lights */

static void dd_pickup_tick(dd_server *s, int index) {
  dd_entity *e = &s->ents[index];
  if (s->tick % DD_MOVER_STEP == 0) {
    dd_col_mover_speed(s->col, (int)e->pos.x, (int)e->pos.y, &e->u.pickup.core);
    e->pos = dd_v2_add(e->pos, e->u.pickup.core);
  }
  int ents[DD_PHYS_MAX_CLIENTS];
  int num = dd_find_entities(s, e->pos, e->proximity_radius + 6, ents, DD_PHYS_MAX_CLIENTS, DD_ENTTYPE_CHARACTER);
  for (int i = 0; i < num; ++i) {
    dd_character *chr = &s->chars[s->ents[ents[i]].u.character];
    if (!chr->alive) continue;
    if (e->u.pickup.layer == DD_LAYER_SWITCH && e->u.pickup.number > 0 && !dd_switch_status(s, e->u.pickup.number, dd_chr_team(s, chr))) continue;
    const bool super = dd_chr_team(s, chr) == DD_PHYS_TEAM_SUPER;
    dd_character_core *core = &chr->core;
    switch (e->u.pickup.type) {
    case DD_POWERUP_FREEZE: dd_chr_freeze(s, chr); break;
    case DD_POWERUP_ARMOR:
      if (super) continue;
      for (int j = DD_WEAPON_ID_SHOTGUN; j < DD_PHYS_NUM_WEAPONS; j++) {
        if (core->weapons[j].got) {
          core->weapons[j].got = false;
          core->weapons[j].ammo = 0;
          chr->last_weapon = DD_WEAPON_ID_GUN;
        }
      }
      core->ninja.activation_dir = dd_v2(0, 0);
      core->ninja.activation_tick = -500;
      core->ninja.current_move_time = 0;
      if (core->active_weapon >= DD_WEAPON_ID_SHOTGUN) core->active_weapon = DD_WEAPON_ID_HAMMER;
      break;
    case DD_POWERUP_ARMOR_SHOTGUN:
    case DD_POWERUP_ARMOR_GRENADE:
    case DD_POWERUP_ARMOR_LASER: {
      if (super) continue;
      const int w = e->u.pickup.type == DD_POWERUP_ARMOR_SHOTGUN ? DD_WEAPON_ID_SHOTGUN
                    : e->u.pickup.type == DD_POWERUP_ARMOR_GRENADE ? DD_WEAPON_ID_GRENADE
                                                                   : DD_WEAPON_ID_LASER;
      if (core->weapons[w].got) {
        core->weapons[w].got = false;
        core->weapons[w].ammo = 0;
        chr->last_weapon = DD_WEAPON_ID_GUN;
      }
      if (core->active_weapon == w) core->active_weapon = DD_WEAPON_ID_HAMMER;
    } break;
    case DD_POWERUP_ARMOR_NINJA:
      if (super) continue;
      core->ninja.activation_dir = dd_v2(0, 0);
      core->ninja.activation_tick = -500;
      core->ninja.current_move_time = 0;
      break;
    case DD_POWERUP_WEAPON:
      if (e->u.pickup.subtype >= 0 && e->u.pickup.subtype < DD_PHYS_NUM_WEAPONS &&
          (!core->weapons[e->u.pickup.subtype].got || core->weapons[e->u.pickup.subtype].ammo != -1))
        dd_chr_give_weapon(s, chr, e->u.pickup.subtype, false);
      break;
    case DD_POWERUP_NINJA: dd_chr_give_ninja(s, chr); break;
    default: break;
    }
  }
}

static void dd_door_new(dd_server *s, dd_collision *col, dd_vec2 pos, float rotation, int length, int number) {
  int index = dd_ent_alloc(s, DD_ENT_DOOR, DD_ENTTYPE_LASER, pos, 0);
  if (index < 0) return;
  dd_entity *e = &s->ents[index];
  e->u.door.number = number;
  e->u.door.length = length;
  e->u.door.direction = dd_v2(sinf(rotation), cosf(rotation));
  dd_vec2 to = dd_v2_add(pos, dd_v2_mul(dd_v2_normalize(e->u.door.direction), (float)length));
  dd_col_intersect_no_laser(col, pos, to, &e->u.door.to, NULL);
  /* ResetCollision */
  if (!dd_col_get_tile(col, (int)pos.x, (int)pos.y) && !dd_col_get_front_tile(col, (int)pos.x, (int)pos.y)) {
    for (int i = 0; i < length - 1; i++) {
      dd_vec2 current_pos = dd_v2_add(pos, dd_v2_mul(e->u.door.direction, (float)i));
      if (dd_col_check_point(col, current_pos.x, current_pos.y)) break;
      dd_col_set_door_collision_at(col, current_pos.x, current_pos.y, DD_TILE_STOPA, 0, (unsigned char)number);
    }
  }
  dd_ent_insert(s, index);
}

static void dd_dragger_beam_reset(dd_server *s, int index) {
  dd_entity *e = &s->ents[index];
  e->marked_for_destroy = true;
  e->u.beam.active = false;
  s->ents[e->u.beam.dragger].u.dragger.beam[e->u.beam.for_client_id] = -1;
}

static void dd_dragger_beam_tick(dd_server *s, int index) {
  dd_entity *e = &s->ents[index];
  if (!e->u.beam.active) return;
  dd_character *target = dd_chr(s, e->u.beam.for_client_id);
  if (!target) {
    dd_dragger_beam_reset(s, index);
    return;
  }
  if (s->tick % DD_MOVER_STEP == 0) {
    if (e->u.beam.layer == DD_LAYER_SWITCH && e->u.beam.number > 0 && !dd_switch_status(s, e->u.beam.number, dd_chr_team(s, target))) {
      dd_dragger_beam_reset(s, index);
      return;
    }
  }
  const dd_vec2 target_pos = dd_chr_pos(s, target);
  if (dd_v2_distance(target_pos, e->pos) >= s->cfg.sv_dragger_range || !target->alive ||
      (e->u.beam.ignore_walls ? dd_col_intersect_no_laser_no_walls(s->col, e->pos, target_pos, NULL, NULL)
                              : dd_col_intersect_no_laser(s->col, e->pos, target_pos, NULL, NULL))) {
    dd_dragger_beam_reset(s, index);
    return;
  } else if (dd_v2_distance(target_pos, e->pos) > 28) {
    dd_chr_add_velocity(target, dd_v2_mul(dd_v2_normalize(dd_v2_sub(e->pos, target_pos)), e->u.beam.strength));
  }
}

static void dd_dragger_look_for_players(dd_server *s, int index) {
  dd_entity *e = &s->ents[index];
  int in_range[DD_PHYS_MAX_CLIENTS];
  int num = dd_find_entities(s, e->pos, (float)s->cfg.sv_dragger_range - dd_physical_size, in_range, DD_PHYS_MAX_CLIENTS,
                             DD_ENTTYPE_CHARACTER);
  int closest_target_id_in_team[DD_PHYS_MAX_CLIENTS];
  bool can_still_be_team_target[DD_PHYS_MAX_CLIENTS] = {0};
  bool is_target[DD_PHYS_MAX_CLIENTS] = {0};
  int min_dist_in_team[DD_PHYS_MAX_CLIENTS] = {0};
  for (int i = 0; i < DD_PHYS_MAX_CLIENTS; ++i) closest_target_id_in_team[i] = -1;

  for (int i = 0; i < num; i++) {
    dd_character *target = &s->chars[s->ents[in_range[i]].u.character];
    const int target_team = dd_chr_team(s, target);
    if (target_team == DD_PHYS_TEAM_SUPER) continue;
    if (e->u.dragger.layer == DD_LAYER_SWITCH && e->u.dragger.number > 0 && !dd_switch_status(s, e->u.dragger.number, target_team)) continue;
    const dd_vec2 target_pos = dd_chr_pos(s, target);
    int is_reachable = e->u.dragger.ignore_walls ? !dd_col_intersect_no_laser_no_walls(s->col, e->pos, target_pos, NULL, NULL)
                                                 : !dd_col_intersect_no_laser(s->col, e->pos, target_pos, NULL, NULL);
    if (is_reachable && target->alive) {
      const int target_cid = dd_chr_cid(target);
      if (dd_teams_get_solo(&s->teams, target_cid)) {
        is_target[target_cid] = true;
      } else {
        int distance = (int)dd_v2_distance(target_pos, e->pos);
        if (min_dist_in_team[target_team] == 0 || min_dist_in_team[target_team] > distance) {
          min_dist_in_team[target_team] = distance;
          closest_target_id_in_team[target_team] = target_cid;
        }
        can_still_be_team_target[target_cid] = true;
      }
    }
  }

  for (int i = 0; i < DD_PHYS_MAX_CLIENTS; i++) {
    int *t = &s->ents[index].u.dragger.target_id_in_team[i];
    if ((*t != -1 && !can_still_be_team_target[*t]) || *t == -1) *t = closest_target_id_in_team[i];
    if (*t != -1) is_target[*t] = true;
  }

  for (int i = 0; i < DD_PHYS_MAX_CLIENTS; i++) {
    const int beam = s->ents[index].u.dragger.beam[i];
    if (is_target[i] && beam == -1) {
      int b = dd_ent_alloc(s, DD_ENT_DRAGGER_BEAM, DD_ENTTYPE_LASER, s->ents[index].pos, 0);
      if (b < 0) continue;
      dd_entity *be = &s->ents[b];
      be->u.beam.dragger = index;
      be->u.beam.strength = s->ents[index].u.dragger.strength;
      be->u.beam.ignore_walls = s->ents[index].u.dragger.ignore_walls;
      be->u.beam.for_client_id = i;
      be->u.beam.active = true;
      be->u.beam.layer = s->ents[index].u.dragger.layer;
      be->u.beam.number = s->ents[index].u.dragger.number;
      be->u.beam.eval_tick = s->tick;
      dd_ent_insert(s, b);
      s->ents[index].u.dragger.beam[i] = b;
      /* inserted at the head, it would miss this tick, so DDNet ticks it now */
      dd_dragger_beam_tick(s, b);
    } else if (!is_target[i] && beam != -1) {
      dd_dragger_beam_reset(s, beam);
    }
  }
}

static void dd_dragger_tick(dd_server *s, int index) {
  if (s->tick % DD_MOVER_STEP == 0) {
    dd_entity *e = &s->ents[index];
    e->u.dragger.eval_tick = s->tick;
    dd_col_mover_speed(s->col, (int)e->pos.x, (int)e->pos.y, &e->u.dragger.core);
    e->pos = dd_v2_add(e->pos, e->u.dragger.core);
    for (int i = 0; i < DD_PHYS_MAX_CLIENTS; ++i)
      if (e->u.dragger.beam[i] != -1) s->ents[e->u.dragger.beam[i]].pos = e->pos;
    dd_dragger_look_for_players(s, index);
  }
}

static void dd_dragger_new(dd_server *s, dd_vec2 pos, float strength, bool ignore_walls, int layer, int number) {
  int index = dd_ent_alloc(s, DD_ENT_DRAGGER, DD_ENTTYPE_LASER, pos, 0);
  if (index < 0) return;
  dd_entity *e = &s->ents[index];
  e->u.dragger.strength = strength;
  e->u.dragger.ignore_walls = ignore_walls;
  e->u.dragger.layer = layer;
  e->u.dragger.number = number;
  e->u.dragger.eval_tick = s->tick;
  for (int i = 0; i < DD_PHYS_MAX_CLIENTS; ++i) {
    e->u.dragger.target_id_in_team[i] = -1;
    e->u.dragger.beam[i] = -1;
  }
  dd_ent_insert(s, index);
}

static void dd_plasma_new(dd_server *s, dd_vec2 pos, dd_vec2 dir, bool freeze, bool explosive, int for_client_id) {
  int index = dd_ent_alloc(s, DD_ENT_PLASMA, DD_ENTTYPE_LASER, pos, 0);
  if (index < 0) return;
  dd_entity *e = &s->ents[index];
  e->u.plasma.core = dir;
  e->u.plasma.freeze = freeze;
  e->u.plasma.explosive = explosive;
  e->u.plasma.for_client_id = for_client_id;
  e->u.plasma.eval_tick = s->tick;
  e->u.plasma.life_time = (int)(DD_SERVER_TICK_SPEED * 1.5f);
  dd_ent_insert(s, index);
}

static void dd_plasma_tick(dd_server *s, int index) {
  dd_entity *e = &s->ents[index];
  if (e->u.plasma.life_time == 0) {
    e->marked_for_destroy = true;
    return;
  }
  dd_character *target = dd_chr(s, e->u.plasma.for_client_id);
  if (!target) {
    e->marked_for_destroy = true;
    return;
  }
  e->u.plasma.life_time--;
  e->pos = dd_v2_add(e->pos, e->u.plasma.core);
  e->u.plasma.core = dd_v2_mul(e->u.plasma.core, 1.1f);

  /* HitCharacter */
  dd_vec2 intersect_pos;
  int hit = dd_intersect_character(s, e->pos, dd_v2_add(e->pos, e->u.plasma.core), 0.0f, &intersect_pos, -2, e->u.plasma.for_client_id, -1);
  if (hit >= 0 && dd_teams_team(&s->teams, hit) != DD_PHYS_TEAM_SUPER) {
    dd_character *hit_chr = &s->chars[hit];
    if (e->u.plasma.freeze)
      dd_chr_freeze(s, hit_chr);
    else
      dd_chr_unfreeze(hit_chr);
    if (e->u.plasma.explosive)
      dd_server_create_explosion(s, e->pos, e->u.plasma.for_client_id, DD_WEAPON_ID_GRENADE, true, dd_chr_team(s, target));
    s->ents[index].marked_for_destroy = true;
  }
  e = &s->ents[index];
  /* HitObstacle: runs even after a hit, so a bullet may explode twice */
  if (dd_col_intersect_no_laser(s->col, e->pos, dd_v2_add(e->pos, e->u.plasma.core), NULL, NULL)) {
    if (e->u.plasma.explosive)
      dd_server_create_explosion(s, e->pos, e->u.plasma.for_client_id, DD_WEAPON_ID_GRENADE, true, dd_chr_team(s, target));
    s->ents[index].marked_for_destroy = true;
  }
}

static void dd_gun_fire(dd_server *s, int index) {
  dd_entity *e = &s->ents[index];
  int in_range[DD_PHYS_MAX_CLIENTS];
  int num = dd_find_entities(s, e->pos, (float)s->cfg.sv_plasma_range, in_range, DD_PHYS_MAX_CLIENTS, DD_ENTTYPE_CHARACTER);
  int target_id_in_team[DD_PHYS_MAX_CLIENTS];
  bool is_target[DD_PHYS_MAX_CLIENTS] = {0};
  int min_dist_in_team[DD_PHYS_MAX_CLIENTS] = {0};
  for (int i = 0; i < DD_PHYS_MAX_CLIENTS; ++i) target_id_in_team[i] = -1;

  for (int i = 0; i < num; i++) {
    dd_character *target = &s->chars[s->ents[in_range[i]].u.character];
    const int target_team = dd_chr_team(s, target);
    if (target_team == DD_PHYS_TEAM_SUPER) continue;
    if (e->u.gun.layer == DD_LAYER_SWITCH && e->u.gun.number > 0 && !dd_switch_status(s, e->u.gun.number, target_team)) continue;
    const int target_cid = dd_chr_cid(target);
    const bool target_is_solo = dd_teams_get_solo(&s->teams, target_cid);
    if ((target_is_solo && e->u.gun.last_fire_solo[target_cid] + DD_SERVER_TICK_SPEED / s->cfg.sv_plasma_per_sec > s->tick) ||
        (!target_is_solo && e->u.gun.last_fire_team[target_team] + DD_SERVER_TICK_SPEED / s->cfg.sv_plasma_per_sec > s->tick))
      continue;
    const dd_vec2 target_pos = dd_chr_pos(s, target);
    int is_reachable = !dd_col_intersect_line(s->col, e->pos, target_pos, NULL, NULL);
    if (is_reachable && target->alive) {
      if (target_is_solo) {
        is_target[target_cid] = true;
        e->u.gun.last_fire_solo[target_cid] = s->tick;
      } else {
        int distance = (int)dd_v2_distance(target_pos, e->pos);
        if (min_dist_in_team[target_team] == 0 || min_dist_in_team[target_team] > distance) {
          min_dist_in_team[target_team] = distance;
          target_id_in_team[target_team] = target_cid;
        }
      }
    }
  }
  for (int i = 0; i < DD_PHYS_MAX_CLIENTS; i++) {
    if (target_id_in_team[i] != -1) {
      is_target[target_id_in_team[i]] = true;
      e->u.gun.last_fire_team[i] = s->tick;
    }
  }
  const dd_vec2 gun_pos = e->pos;
  const bool freeze = e->u.gun.freeze, explosive = e->u.gun.explosive;
  for (int i = 0; i < DD_PHYS_MAX_CLIENTS; i++) {
    if (is_target[i]) {
      dd_character *target = &s->chars[i];
      dd_plasma_new(s, gun_pos, dd_v2_normalize(dd_v2_sub(dd_chr_pos(s, target), gun_pos)), freeze, explosive, i);
    }
  }
}

static void dd_gun_tick(dd_server *s, int index) {
  if (s->tick % DD_MOVER_STEP == 0) {
    dd_entity *e = &s->ents[index];
    e->u.gun.eval_tick = s->tick;
    dd_col_mover_speed(s->col, (int)e->pos.x, (int)e->pos.y, &e->u.gun.core);
    e->pos = dd_v2_add(e->pos, e->u.gun.core);
  }
  if (s->cfg.sv_plasma_per_sec > 0) dd_gun_fire(s, index);
}

static void dd_gun_new(dd_server *s, dd_vec2 pos, bool freeze, bool explosive, int layer, int number) {
  int index = dd_ent_alloc(s, DD_ENT_GUN, DD_ENTTYPE_LASER, pos, 0);
  if (index < 0) return;
  dd_entity *e = &s->ents[index];
  e->u.gun.freeze = freeze;
  e->u.gun.explosive = explosive;
  e->u.gun.layer = layer;
  e->u.gun.number = number;
  e->u.gun.eval_tick = s->tick;
  dd_ent_insert(s, index);
}

static void dd_light_step(dd_server *s, dd_entity *e) {
  /* Move */
  if (e->u.light.speed != 0) {
    if ((e->u.light.curve_length >= e->u.light.length && e->u.light.speed > 0) || (e->u.light.curve_length <= 0 && e->u.light.speed < 0))
      e->u.light.speed = -e->u.light.speed;
    e->u.light.curve_length += e->u.light.speed * e->u.light.tick + e->u.light.length_l;
    e->u.light.length_l = 0;
    if (e->u.light.curve_length > e->u.light.length) {
      e->u.light.length_l = e->u.light.curve_length - e->u.light.length;
      e->u.light.curve_length = e->u.light.length;
    } else if (e->u.light.curve_length < 0) {
      e->u.light.length_l = 0 + e->u.light.curve_length;
      e->u.light.curve_length = 0;
    }
  }
  e->u.light.rotation += e->u.light.angular_speed * e->u.light.tick;
  if (e->u.light.rotation > dd_pi * 2)
    e->u.light.rotation -= dd_pi * 2;
  else if (e->u.light.rotation < 0)
    e->u.light.rotation += dd_pi * 2;
  /* Step */
  const dd_vec2 direction = dd_v2(sinf(e->u.light.rotation), cosf(e->u.light.rotation));
  const dd_vec2 next_position = dd_v2_add(e->pos, dd_v2_mul(dd_v2_normalize(direction), (float)e->u.light.curve_length));
  dd_col_intersect_no_laser(s->col, e->pos, next_position, &e->u.light.to, NULL);
}

static void dd_light_tick(dd_server *s, int index) {
  dd_entity *e = &s->ents[index];
  if (s->tick % DD_MOVER_STEP == 0) {
    e->u.light.eval_tick = s->tick;
    dd_col_mover_speed(s->col, (int)e->pos.x, (int)e->pos.y, &e->u.light.core);
    e->pos = dd_v2_add(e->pos, e->u.light.core);
    dd_light_step(s, e);
  }
  int hit[DD_PHYS_MAX_CLIENTS];
  int num = dd_intersected_characters(s, e->pos, e->u.light.to, 0.0f, hit);
  for (int i = 0; i < num; ++i) {
    dd_character *chr = &s->chars[hit[i]];
    if (e->u.light.layer == DD_LAYER_SWITCH && e->u.light.number > 0 && !dd_switch_status(s, e->u.light.number, dd_chr_team(s, chr))) continue;
    dd_chr_freeze(s, chr);
  }
}

static int dd_light_new(dd_server *s, dd_vec2 pos, float rotation, int length, int layer, int number) {
  int index = dd_ent_alloc(s, DD_ENT_LIGHT, DD_ENTTYPE_LASER, pos, 0);
  if (index < 0) return -1;
  dd_entity *e = &s->ents[index];
  e->u.light.layer = layer;
  e->u.light.number = number;
  e->u.light.tick = (int)(DD_SERVER_TICK_SPEED * 0.15f);
  e->u.light.rotation = rotation;
  e->u.light.length = length;
  e->u.light.eval_tick = s->tick;
  dd_ent_insert(s, index);
  return index;
}

/* ======================================================================== */
/* world setup: settings, tuning, map entities */

/* A map setting or tune command line, as DDNet's console would run it. */
static void dd_server_apply_command(dd_server *s, const char *line) {
  char cmd[64] = {0};
  int n = 0;
  while (*line == ' ') line++;
  while (*line && *line != ' ' && n < 63) cmd[n++] = *line++;
  while (*line == ' ') line++;
  const char *args = line;

  if (!strcmp(cmd, "tune")) {
    char name[64] = {0};
    float value = 0;
    if (sscanf(args, "%63s %f", name, &value) == 2) dd_tuning_set(&s->tuning[0], name, value);
    return;
  }
  if (!strcmp(cmd, "tune_zone")) {
    int zone = 0;
    char name[64] = {0};
    float value = 0;
    if (sscanf(args, "%d %63s %f", &zone, name, &value) == 3 && zone >= 0 && zone < DD_NUM_TUNEZONES) dd_tuning_set(&s->tuning[zone], name, value);
    return;
  }
  int value = 0;
  if (sscanf(args, "%d", &value) != 1) return;
  static const struct {
    const char *name;
    size_t offset;
  } ints[] = {
#define DD_CFG(n) {#n, offsetof(dd_server_config, n)}
      DD_CFG(sv_old_teleport_weapons), DD_CFG(sv_old_teleport_hook), DD_CFG(sv_teleport_hold_hook), DD_CFG(sv_teleport_lose_weapons),
      DD_CFG(sv_deepfly), DD_CFG(sv_destroy_bullets_on_death), DD_CFG(sv_destroy_lasers_on_death), DD_CFG(sv_hit), DD_CFG(sv_endless_drag),
      DD_CFG(sv_freeze_delay), DD_CFG(sv_team), DD_CFG(sv_old_laser), DD_CFG(sv_no_weak_hook), DD_CFG(sv_reset_pickups),
      DD_CFG(sv_plasma_range), DD_CFG(sv_plasma_per_sec), DD_CFG(sv_dragger_range), DD_CFG(sv_solo_server), DD_CFG(sv_endless_super_hook),
      DD_CFG(sv_min_team_size),
#undef DD_CFG
  };
  for (size_t i = 0; i < sizeof(ints) / sizeof(ints[0]); ++i)
    if (!strcmp(cmd, ints[i].name)) *(int *)((char *)&s->cfg + ints[i].offset) = value;
}

/* IGameController::OnEntity */
static void dd_server_on_entity(dd_server *s, dd_collision *col, int index, int x, int y, int layer, int flags, int number) {
  const dd_vec2 pos = dd_v2(x * 32.0f + 16.0f, y * 32.0f + 16.0f);
  int sides[8];
  sides[0] = dd_col_entity(col, x, y + 1, layer);
  sides[1] = dd_col_entity(col, x + 1, y + 1, layer);
  sides[2] = dd_col_entity(col, x + 1, y, layer);
  sides[3] = dd_col_entity(col, x + 1, y - 1, layer);
  sides[4] = dd_col_entity(col, x, y - 1, layer);
  sides[5] = dd_col_entity(col, x - 1, y - 1, layer);
  sides[6] = dd_col_entity(col, x - 1, y, layer);
  sides[7] = dd_col_entity(col, x - 1, y + 1, layer);

  if (index >= DD_ENTITY_SPAWN && index <= DD_ENTITY_SPAWN_BLUE) {
    const int spawn_type = index - DD_ENTITY_SPAWN;
    dd_vec2 *grown = (dd_vec2 *)realloc(s->spawn_points[spawn_type], sizeof(dd_vec2) * (size_t)(s->num_spawn_points[spawn_type] + 1));
    if (grown) {
      s->spawn_points[spawn_type] = grown;
      grown[s->num_spawn_points[spawn_type]++] = pos;
    }
  } else if (index == DD_ENTITY_DOOR) {
    for (int i = 0; i < 8; i++)
      if (sides[i] >= DD_ENTITY_LASER_SHORT && sides[i] <= DD_ENTITY_LASER_LONG)
        dd_door_new(s, col, pos, dd_pi / 4 * i, 32 * 3 + 32 * (sides[i] - DD_ENTITY_LASER_SHORT) * 3, number);
  } else if (index == DD_ENTITY_CRAZY_SHOTGUN_EX || index == DD_ENTITY_CRAZY_SHOTGUN) {
    int dir;
    if (index == DD_ENTITY_CRAZY_SHOTGUN_EX) {
      if (!flags) dir = 0;
      else if (flags == DD_ROTATION_90) dir = 1;
      else if (flags == DD_ROTATION_180) dir = 2;
      else dir = 3;
    } else {
      if (!flags) dir = 0;
      else if (flags == DD_TILEFLAG_ROTATE) dir = 1;
      else if (flags == (DD_TILEFLAG_XFLIP | DD_TILEFLAG_YFLIP)) dir = 2;
      else dir = 3;
    }
    float deg = dir * (dd_pi / 2);
    int p = dd_projectile_new(s, DD_WEAPON_ID_SHOTGUN, -1, pos, dd_v2(sinf(deg), cosf(deg)), -2, true, index == DD_ENTITY_CRAZY_SHOTGUN_EX,
                              dd_v2(sinf(deg), cosf(deg)), layer, number);
    if (p >= 0) s->ents[p].u.projectile.bouncing = 2 - (dir % 2);
  }

  int type = -1, sub_type = 0;
  if (index == DD_ENTITY_ARMOR_1) type = DD_POWERUP_ARMOR;
  else if (index == DD_ENTITY_ARMOR_SHOTGUN) type = DD_POWERUP_ARMOR_SHOTGUN;
  else if (index == DD_ENTITY_ARMOR_GRENADE) type = DD_POWERUP_ARMOR_GRENADE;
  else if (index == DD_ENTITY_ARMOR_NINJA) type = DD_POWERUP_ARMOR_NINJA;
  else if (index == DD_ENTITY_ARMOR_LASER) type = DD_POWERUP_ARMOR_LASER;
  else if (index == DD_ENTITY_HEALTH_1) type = DD_POWERUP_FREEZE;
  else if (index == DD_ENTITY_WEAPON_SHOTGUN) {
    type = DD_POWERUP_WEAPON;
    sub_type = DD_WEAPON_ID_SHOTGUN;
  } else if (index == DD_ENTITY_WEAPON_GRENADE) {
    type = DD_POWERUP_WEAPON;
    sub_type = DD_WEAPON_ID_GRENADE;
  } else if (index == DD_ENTITY_WEAPON_LASER) {
    type = DD_POWERUP_WEAPON;
    sub_type = DD_WEAPON_ID_LASER;
  } else if (index == DD_ENTITY_POWERUP_NINJA) {
    type = DD_POWERUP_NINJA;
    sub_type = DD_WEAPON_ID_NINJA;
  } else if (index >= DD_ENTITY_LASER_FAST_CCW && index <= DD_ENTITY_LASER_FAST_CW) {
    int sides2[8];
    sides2[0] = dd_col_entity(col, x, y + 2, layer);
    sides2[1] = dd_col_entity(col, x + 2, y + 2, layer);
    sides2[2] = dd_col_entity(col, x + 2, y, layer);
    sides2[3] = dd_col_entity(col, x + 2, y - 2, layer);
    sides2[4] = dd_col_entity(col, x, y - 2, layer);
    sides2[5] = dd_col_entity(col, x - 2, y - 2, layer);
    sides2[6] = dd_col_entity(col, x - 2, y, layer);
    sides2[7] = dd_col_entity(col, x - 2, y + 2, layer);

    int ind = index - DD_ENTITY_LASER_STOP;
    int m;
    if (ind < 0) {
      ind = -ind;
      m = 1;
    } else if (ind == 0)
      m = 0;
    else
      m = -1;

    float angular_speed = 0.0f;
    if (ind == 0) angular_speed = 0.0f;
    else if (ind == 1) angular_speed = dd_pi / 360;
    else if (ind == 2) angular_speed = dd_pi / 180;
    else if (ind == 3) angular_speed = dd_pi / 90;
    angular_speed *= m;

    for (int i = 0; i < 8; i++) {
      if (sides[i] >= DD_ENTITY_LASER_SHORT && sides[i] <= DD_ENTITY_LASER_LONG) {
        int l = dd_light_new(s, pos, dd_pi / 4 * i, 32 * 3 + 32 * (sides[i] - DD_ENTITY_LASER_SHORT) * 3, layer, number);
        if (l < 0) continue;
        dd_entity *light = &s->ents[l];
        light->u.light.angular_speed = angular_speed;
        if (sides2[i] >= DD_ENTITY_LASER_C_SLOW && sides2[i] <= DD_ENTITY_LASER_C_FAST) {
          light->u.light.speed = 1 + (sides2[i] - DD_ENTITY_LASER_C_SLOW) * 2;
          light->u.light.curve_length = light->u.light.length;
        } else if (sides2[i] >= DD_ENTITY_LASER_O_SLOW && sides2[i] <= DD_ENTITY_LASER_O_FAST) {
          light->u.light.speed = 1 + (sides2[i] - DD_ENTITY_LASER_O_SLOW) * 2;
          light->u.light.curve_length = 0;
        } else {
          light->u.light.curve_length = light->u.light.length;
        }
        /* CLight's constructor steps once before these are set */
      }
    }
  } else if (index >= DD_ENTITY_DRAGGER_WEAK && index <= DD_ENTITY_DRAGGER_STRONG) {
    dd_dragger_new(s, pos, (float)(index - DD_ENTITY_DRAGGER_WEAK + 1), false, layer, number);
  } else if (index >= DD_ENTITY_DRAGGER_WEAK_NW && index <= DD_ENTITY_DRAGGER_STRONG_NW) {
    dd_dragger_new(s, pos, (float)(index - DD_ENTITY_DRAGGER_WEAK_NW + 1), true, layer, number);
  } else if (index == DD_ENTITY_PLASMAE) {
    dd_gun_new(s, pos, false, true, layer, number);
  } else if (index == DD_ENTITY_PLASMAF) {
    dd_gun_new(s, pos, true, false, layer, number);
  } else if (index == DD_ENTITY_PLASMA) {
    dd_gun_new(s, pos, true, true, layer, number);
  } else if (index == DD_ENTITY_PLASMAU) {
    dd_gun_new(s, pos, false, false, layer, number);
  }

  if (type != -1) {
    int p = dd_ent_alloc(s, DD_ENT_PICKUP, DD_ENTTYPE_PICKUP, pos, 14);
    if (p >= 0) {
      s->ents[p].u.pickup.type = type;
      s->ents[p].u.pickup.subtype = sub_type;
      s->ents[p].u.pickup.layer = layer;
      s->ents[p].u.pickup.number = number;
      s->ents[p].u.pickup.flags = flags;
      dd_ent_insert(s, p);
    }
  }
}

/* Builds a world for a map: settings, tune zones, doors, and the map's
 * entities, like CGameContext::OnInit and CreateAllEntities. `col` must
 * outlive the server; its door layer is written here. */
static bool dd_server_init(dd_server *s, dd_collision *col, char **settings, int num_settings) {
  memset(s, 0, sizeof(*s));
  s->col = col;
  for (int i = 0; i < DD_NUM_ENTTYPES; ++i) s->first[i] = -1;
  s->next_traverse = -1;
  dd_server_config_default(&s->cfg);
  for (int z = 0; z < DD_NUM_TUNEZONES; ++z) dd_tuning_default(&s->tuning[z]);
  dd_teams_reset(&s->teams);
  for (int i = 0; i < num_settings; ++i)
    if (settings && settings[i]) dd_server_apply_command(s, settings[i]);
  col->old_teleport_hook = s->cfg.sv_old_teleport_hook;
  col->old_teleport_weapons = s->cfg.sv_old_teleport_weapons;
  if (!dd_world_init_switchers(&s->core, col->highest_switch_number)) return false;

  for (int y = 0; y < col->height; y++) {
    for (int x = 0; x < col->width; x++) {
      const int index = y * col->width + x;
      const int game_index = col->tiles[index];
      if (game_index == DD_TILE_OLDLASER) s->cfg.sv_old_laser = 1;
      else if (game_index == DD_TILE_NPC) s->tuning[0].player_collision = dd_tune_param(0);
      else if (game_index == DD_TILE_EHOOK) s->cfg.sv_endless_drag = 1;
      else if (game_index == DD_TILE_NOHIT) s->cfg.sv_hit = 0;
      else if (game_index == DD_TILE_NPH) s->tuning[0].player_hooking = dd_tune_param(0);
      else if (game_index >= DD_ENTITY_OFFSET) dd_server_on_entity(s, col, game_index - DD_ENTITY_OFFSET, x, y, DD_LAYER_GAME, col->tile_flags[index], 0);

      if (col->front) {
        const int front_index = col->front[index];
        if (front_index == DD_TILE_OLDLASER) s->cfg.sv_old_laser = 1;
        else if (front_index == DD_TILE_NPC) s->tuning[0].player_collision = dd_tune_param(0);
        else if (front_index == DD_TILE_EHOOK) s->cfg.sv_endless_drag = 1;
        else if (front_index == DD_TILE_NOHIT) s->cfg.sv_hit = 0;
        else if (front_index == DD_TILE_NPH) s->tuning[0].player_hooking = dd_tune_param(0);
        else if (front_index >= DD_ENTITY_OFFSET)
          dd_server_on_entity(s, col, front_index - DD_ENTITY_OFFSET, x, y, DD_LAYER_FRONT, col->front_flags[index], 0);
      }
      if (col->switch_type) {
        const int switch_type = col->switch_type[index];
        if (switch_type >= DD_ENTITY_OFFSET)
          dd_server_on_entity(s, col, switch_type - DD_ENTITY_OFFSET, x, y, DD_LAYER_SWITCH, col->switch_flags[index], col->switch_number[index]);
      }
    }
  }
  /* Lights step once in their constructor, before OnEntity sets their speed;
   * with speed 0 that first step only moves the rotation by 0. */
  for (int e = s->first[DD_ENTTYPE_LASER]; e >= 0; e = s->ents[e].next)
    if (s->ents[e].kind == DD_ENT_LIGHT) {
      const int speed = s->ents[e].u.light.speed;
      const float angular = s->ents[e].u.light.angular_speed;
      const int curve = s->ents[e].u.light.curve_length;
      s->ents[e].u.light.speed = 0;
      s->ents[e].u.light.angular_speed = 0;
      s->ents[e].u.light.curve_length = 0;
      dd_light_step(s, &s->ents[e]);
      s->ents[e].u.light.speed = speed;
      s->ents[e].u.light.angular_speed = angular;
      s->ents[e].u.light.curve_length = curve;
    }
  return true;
}

static void dd_server_free(dd_server *s) {
  free(s->ents);
  free(s->core.switchers);
  for (int t = 0; t < 3; ++t) free(s->spawn_points[t]);
  memset(s, 0, sizeof(*s));
}

/* Deep copy for trying alternatives from the same state. `dst` must be
 * zero-initialised or a previous copy. */
static bool dd_server_copy(dd_server *dst, const dd_server *src) {
  dd_entity *ents = dst->ents;
  dd_switcher *switchers = dst->core.switchers;
  dd_vec2 *spawns[3] = {dst->spawn_points[0], dst->spawn_points[1], dst->spawn_points[2]};
  const int old_capacity = dst->ent_capacity, old_switchers = dst->core.num_switchers;
  const int old_spawns[3] = {dst->num_spawn_points[0], dst->num_spawn_points[1], dst->num_spawn_points[2]};

  memcpy(dst, src, sizeof(*dst));
  for (int i = 0; i < DD_PHYS_MAX_CLIENTS; ++i)
    if (src->chars[i].alive) dst->chars_touched |= (uint64_t)1 << i;

  if (old_capacity < src->ent_capacity) {
    dd_entity *grown = (dd_entity *)realloc(ents, sizeof(dd_entity) * (size_t)src->ent_capacity);
    if (!grown) return false;
    ents = grown;
  }
  if (src->ent_capacity) memcpy(ents, src->ents, sizeof(dd_entity) * (size_t)src->ent_capacity);
  dst->ents = ents;
  dst->ent_capacity = old_capacity > src->ent_capacity ? old_capacity : src->ent_capacity;
  if (dst->ent_capacity > src->ent_capacity) memset(ents + src->ent_capacity, 0, sizeof(dd_entity) * (size_t)(dst->ent_capacity - src->ent_capacity));

  if (old_switchers != src->core.num_switchers) {
    dd_switcher *grown = (dd_switcher *)realloc(switchers, sizeof(dd_switcher) * (size_t)(src->core.num_switchers ? src->core.num_switchers : 1));
    if (!grown) return false;
    switchers = grown;
  }
  if (src->core.num_switchers) memcpy(switchers, src->core.switchers, sizeof(dd_switcher) * (size_t)src->core.num_switchers);
  dst->core.switchers = src->core.num_switchers ? switchers : NULL;
  if (!src->core.num_switchers) free(switchers);

  for (int t = 0; t < 3; ++t) {
    if (old_spawns[t] != src->num_spawn_points[t]) {
      dd_vec2 *grown = (dd_vec2 *)realloc(spawns[t], sizeof(dd_vec2) * (size_t)(src->num_spawn_points[t] ? src->num_spawn_points[t] : 1));
      if (!grown) return false;
      spawns[t] = grown;
    }
    if (src->num_spawn_points[t]) memcpy(spawns[t], src->spawn_points[t], sizeof(dd_vec2) * (size_t)src->num_spawn_points[t]);
    dst->spawn_points[t] = spawns[t];
  }

  /* re-point the cores into the copy */
  for (int i = 0; i < DD_PHYS_MAX_CLIENTS; ++i) {
    dst->chars[i].core.world = &dst->core;
    dst->chars[i].core.teams = &dst->teams;
    dst->core.characters[i] = src->core.characters[i] ? &dst->chars[i].core : NULL;
  }
  return true;
}

/* dd_server_copy between worlds that descend from the same dd_server_copy
 * and have only been ticked since: ticks never change the tune zones, spawn
 * points or the number of switchers, so those are not copied again. Neither
 * are events past the used ones, nor character slots and entities that
 * neither world used since that copy: those are still equal. */
static bool dd_server_restore(dd_server *dst, const dd_server *src) {
  if (dst->ent_capacity < src->ent_capacity || dst->core.num_switchers != src->core.num_switchers) return dd_server_copy(dst, src);
  dd_entity *ents = dst->ents;
  dd_switcher *switchers = dst->core.switchers;
  dd_vec2 *spawns[3] = {dst->spawn_points[0], dst->spawn_points[1], dst->spawn_points[2]};
  const int capacity = dst->ent_capacity;

  uint64_t touched = src->chars_touched | dst->chars_touched;
  for (int i = 0; i < DD_PHYS_MAX_CLIENTS; ++i)
    if (src->chars[i].alive || dst->chars[i].alive) touched |= (uint64_t)1 << i;
  const int ent_high = dd_maxi(src->ent_high, dst->ent_high);

  memcpy(dst, src, offsetof(dd_server, tuning));
  memcpy(&dst->core, &src->core, offsetof(dd_server, chars) - offsetof(dd_server, core));
  for (int i = 0; i < DD_PHYS_MAX_CLIENTS; ++i)
    if (touched & ((uint64_t)1 << i)) dst->chars[i] = src->chars[i];
  memcpy(&dst->players, &src->players, offsetof(dd_server, events) - offsetof(dd_server, players));
  memcpy(dst->events, src->events, sizeof(dst->events[0]) * (size_t)src->num_events);
  dst->num_events = src->num_events;
  dst->chars_touched = touched;
  dst->ent_high = ent_high;

  const int copied = dd_mini(ent_high, src->ent_capacity);
  if (copied > 0) memcpy(ents, src->ents, sizeof(dd_entity) * (size_t)copied);
  if (dd_mini(ent_high, capacity) > src->ent_capacity)
    memset(ents + src->ent_capacity, 0, sizeof(dd_entity) * (size_t)(dd_mini(ent_high, capacity) - src->ent_capacity));
  dst->ents = ents;
  dst->ent_capacity = capacity;

  if (src->core.num_switchers) memcpy(switchers, src->core.switchers, sizeof(dd_switcher) * (size_t)src->core.num_switchers);
  dst->core.switchers = switchers;

  for (int t = 0; t < 3; ++t) dst->spawn_points[t] = spawns[t];

  for (int i = 0; i < DD_PHYS_MAX_CLIENTS; ++i) {
    dst->chars[i].core.world = &dst->core;
    dst->chars[i].core.teams = &dst->teams;
    dst->core.characters[i] = src->core.characters[i] ? &dst->chars[i].core : NULL;
  }
  return true;
}

/* ======================================================================== */
/* the tick (CGameContext::OnTick, world part) */

/* Input for the next tick: arrives as direct input (weapons may fire right
 * away), then becomes the tick's predicted input. */
static void dd_server_set_input(dd_server *s, int cid, const dd_player_input *input) {
  dd_character *c = dd_chr(s, cid);
  if (!c) return;
  dd_chr_on_direct_input(s, c, input);
  c = dd_chr(s, cid);
  if (c) dd_chr_on_predicted_input(s, c, input);
}

static void dd_ent_tick(dd_server *s, int index) {
  switch (s->ents[index].kind) {
  case DD_ENT_CHARACTER: dd_chr_tick(s, &s->chars[s->ents[index].u.character]); break;
  case DD_ENT_PROJECTILE: dd_projectile_tick(s, index); break;
  case DD_ENT_LASER: dd_laser_tick(s, index); break;
  case DD_ENT_PICKUP: dd_pickup_tick(s, index); break;
  case DD_ENT_DRAGGER: dd_dragger_tick(s, index); break;
  case DD_ENT_DRAGGER_BEAM: dd_dragger_beam_tick(s, index); break;
  case DD_ENT_GUN: dd_gun_tick(s, index); break;
  case DD_ENT_LIGHT: dd_light_tick(s, index); break;
  case DD_ENT_PLASMA: dd_plasma_tick(s, index); break;
  default: break; /* doors do nothing per tick */
  }
}

static void dd_server_tick(dd_server *s) {
  s->tick++;
  s->num_events = 0;

  /* CGameWorld::Tick */
  for (int type = 0; type < DD_NUM_ENTTYPES; type++) {
    if (s->cfg.sv_no_weak_hook && type == DD_ENTTYPE_CHARACTER) {
      for (int e = s->first[type]; e >= 0;) {
        s->next_traverse = s->ents[e].next;
        dd_character *c = &s->chars[s->ents[e].u.character];
        if (c->alive) dd_chr_pre_tick(s, c);
        e = s->next_traverse;
      }
    }
    for (int e = s->first[type]; e >= 0;) {
      s->next_traverse = s->ents[e].next;
      dd_ent_tick(s, e);
      e = s->next_traverse;
    }
  }
  for (int type = 0; type < DD_NUM_ENTTYPES; type++) {
    for (int e = s->first[type]; e >= 0;) {
      s->next_traverse = s->ents[e].next;
      if (s->ents[e].kind == DD_ENT_CHARACTER) {
        dd_character *c = &s->chars[s->ents[e].u.character];
        if (c->alive) dd_chr_tick_deferred(s, c);
      }
      e = s->next_traverse;
    }
  }

  /* RemoveEntities */
  for (int type = 0; type < DD_NUM_ENTTYPES; type++) {
    for (int e = s->first[type]; e >= 0;) {
      s->next_traverse = s->ents[e].next;
      if (s->ents[e].marked_for_destroy) dd_ent_free(s, e);
      e = s->next_traverse;
    }
  }

  /* strong/weak ids */
  int strong_weak_id = 0;
  for (int e = s->first[DD_ENTTYPE_CHARACTER]; e >= 0; e = s->ents[e].next) s->chars[s->ents[e].u.character].strong_weak_id = strong_weak_id++;

  /* CPlayer::Tick: the view position follows the character */
  for (int i = 0; i < DD_PHYS_MAX_CLIENTS; ++i) {
    dd_character *c = dd_chr(s, i);
    if (c) s->players[i].tune_zone = dd_col_is_tune(s->col, dd_col_get_map_index(s->col, dd_chr_pos(s, c)));
  }

  /* switch timers */
  for (int i = 0; i < s->core.num_switchers; ++i) {
    dd_switcher *sw = &s->core.switchers[i];
    for (int j = 0; j < DD_PHYS_NUM_DDRACE_TEAMS; ++j) {
      if (sw->end_tick[j] <= s->tick && sw->type[j] == DD_TILE_SWITCHTIMEDOPEN) {
        sw->status[j] = false;
        sw->end_tick[j] = 0;
        sw->type[j] = DD_TILE_SWITCHCLOSE;
      } else if (sw->end_tick[j] <= s->tick && sw->type[j] == DD_TILE_SWITCHTIMEDCLOSE) {
        sw->status[j] = true;
        sw->end_tick[j] = 0;
        sw->type[j] = DD_TILE_SWITCHOPEN;
      }
    }
  }
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif

#endif /* DD_SERVER_INTERNAL_H */
