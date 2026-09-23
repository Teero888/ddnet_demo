/*
 * internal/dd_core.h - 1:1 port of DDNet's CCharacterCore, CWorldCore and
 * CTeamsCore (game/gamecore.cpp, game/teamscore.cpp).
 *
 * INTERNAL: not part of the library's API.
 */
#ifndef DD_CORE_INTERNAL_H
#define DD_CORE_INTERNAL_H

#include "dd_collision.h"
#ifndef DDNET_DEMO_H
#include "../ddnet_demo.h"
#endif

#define DD_PHYS_TEAM_FLOCK 0
#define DD_PHYS_TEAM_SUPER DD_PHYS_MAX_CLIENTS
#define DD_PHYS_NUM_DDRACE_TEAMS (DD_PHYS_TEAM_SUPER + 1)
#define DD_PHYS_NUM_WEAPONS 6

enum {
  DD_WEAPON_ID_HAMMER = 0,
  DD_WEAPON_ID_GUN,
  DD_WEAPON_ID_SHOTGUN,
  DD_WEAPON_ID_GRENADE,
  DD_WEAPON_ID_LASER,
  DD_WEAPON_ID_NINJA,
};

/* ---- teams (game/teamscore.cpp) ---- */
typedef struct {
  int team[DD_PHYS_MAX_CLIENTS];
  bool is_solo[DD_PHYS_MAX_CLIENTS];
  int num_ddrace_teams;
} dd_teams_core;

static inline int dd_teams_super(const dd_teams_core *teams) { return teams->num_ddrace_teams - 1; }

static void dd_teams_reset(dd_teams_core *teams) {
  teams->num_ddrace_teams = DD_PHYS_NUM_DDRACE_TEAMS;
  for (int i = 0; i < DD_PHYS_MAX_CLIENTS; ++i) {
    teams->team[i] = DD_PHYS_TEAM_FLOCK;
    teams->is_solo[i] = false;
  }
}

static int dd_teams_team(const dd_teams_core *teams, int id) { return teams->team[id]; }
static bool dd_teams_get_solo(const dd_teams_core *teams, int id) { return id >= 0 && id < DD_PHYS_MAX_CLIENTS && teams->is_solo[id]; }
static bool dd_teams_same_team(const dd_teams_core *teams, int id1, int id2) {
  return teams->team[id1] == dd_teams_super(teams) || teams->team[id2] == dd_teams_super(teams) || teams->team[id1] == teams->team[id2];
}

static bool dd_teams_can_keep_hook(const dd_teams_core *teams, int id1, int id2) {
  if (teams->team[id1] == dd_teams_super(teams) || teams->team[id2] == dd_teams_super(teams) || id1 == id2) return true;
  return teams->team[id1] == teams->team[id2];
}

static bool dd_teams_can_collide(const dd_teams_core *teams, int id1, int id2) {
  if (teams->team[id1] == dd_teams_super(teams) || teams->team[id2] == dd_teams_super(teams) || id1 == id2) return true;
  if (teams->is_solo[id1] || teams->is_solo[id2]) return false;
  return teams->team[id1] == teams->team[id2];
}

/* ---- world core (gamecore.h CWorldCore, SSwitchers) ---- */
typedef struct {
  bool status[DD_PHYS_NUM_DDRACE_TEAMS];
  bool initial;
  int end_tick[DD_PHYS_NUM_DDRACE_TEAMS];
  int type[DD_PHYS_NUM_DDRACE_TEAMS];
  int last_update_tick[DD_PHYS_NUM_DDRACE_TEAMS];
} dd_switcher;

/* CWorldCore::RandomOr0 draws from the server's prng, which a demo does not
 * carry. The reconstruction supplies the choice instead; without a callback it
 * is 0, which is also what an empty reckoning world returns. */
typedef int (*dd_random_fn)(void *user, int below_this);

struct dd_character_core;
typedef struct {
  struct dd_character_core *characters[DD_PHYS_MAX_CLIENTS];
  uint64_t occupied; /* bit i: characters[i] is set; keep it with dd_world_set_character */
  dd_switcher *switchers; /* highest switch number + 1 entries, or NULL */
  int num_switchers;
  dd_random_fn random;
  void *random_user;
} dd_world_core;

static void dd_world_set_character(dd_world_core *world, int id, struct dd_character_core *core) {
  world->characters[id] = core;
  if (core)
    world->occupied |= (uint64_t)1 << id;
  else
    world->occupied &= ~((uint64_t)1 << id);
}

/* The lowest set bit's index; `mask` must not be 0. Walking a mask with this
 * visits characters in ascending id order, like DDNet's loops. */
static inline int dd_lowest_bit(uint64_t mask) {
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_ctzll(mask);
#else
  int i = 0;
  while (!(mask & 1)) {
    mask >>= 1;
    i++;
  }
  return i;
#endif
}

static int dd_world_random_or0(const dd_world_core *world, int below_this) {
  if (below_this <= 1 || !world || !world->random) return 0;
  return world->random(world->random_user, below_this);
}

/* CWorldCore::InitSwitchers */
static bool dd_world_init_switchers(dd_world_core *world, int highest_switch_number) {
  free(world->switchers);
  world->switchers = NULL;
  world->num_switchers = 0;
  if (highest_switch_number <= 0) return true;
  world->switchers = (dd_switcher *)calloc((size_t)highest_switch_number + 1, sizeof(dd_switcher));
  if (!world->switchers) return false;
  world->num_switchers = highest_switch_number + 1;
  for (int i = 0; i < world->num_switchers; ++i) {
    world->switchers[i].initial = true;
    for (int j = 0; j < DD_PHYS_NUM_DDRACE_TEAMS; j++) world->switchers[i].status[j] = true;
  }
  return true;
}

/* ---- character core ---- */
enum {
  DD_HOOK_RETRACTED = -1,
  DD_HOOK_IDLE = 0,
  DD_HOOK_RETRACT_START = 1,
  DD_HOOK_RETRACT_END = 3,
  DD_HOOK_FLYING = 4,
  DD_HOOK_GRABBED = 5,

  DD_COREEVENT_GROUND_JUMP = 0x01,
  DD_COREEVENT_AIR_JUMP = 0x02,
  DD_COREEVENT_HOOK_LAUNCH = 0x04,
  DD_COREEVENT_HOOK_ATTACH_PLAYER = 0x08,
  DD_COREEVENT_HOOK_ATTACH_GROUND = 0x10,
  DD_COREEVENT_HOOK_HIT_NOHOOK = 0x20,
  DD_COREEVENT_HOOK_RETRACT = 0x40,
};

/* CNetObj_PlayerInput */
typedef struct {
  int direction, target_x, target_y, jump, fire, hook, player_flags, wanted_weapon, next_weapon, prev_weapon;
} dd_player_input;

typedef struct {
  int ammo_regen_start, ammo, ammocost;
  bool got;
} dd_weapon_stat;

typedef struct dd_character_core {
  dd_world_core *world; /* NULL: no world at all */
  const dd_collision *collision;
  dd_teams_core *teams;
  int id;

  dd_vec2 pos, vel;
  dd_vec2 hook_pos, hook_dir, hook_tele_base;
  int hook_tick, hook_state, hooked_player;
  int active_weapon;
  dd_weapon_stat weapons[DD_PHYS_NUM_WEAPONS];
  struct {
    dd_vec2 activation_dir;
    int activation_tick, current_move_time, old_vel_amount;
  } ninja;
  bool new_hook;
  int jumped, jumped_total, jumps;
  int direction, angle;
  dd_player_input input;
  int triggered_events;
  bool reset;
  int colliding;
  bool left_wall;
  int move_restrictions;
  dd_tuning tuning;

  bool solo, jetpack, collision_disabled, endless_hook, endless_jump, hammer_hit_disabled, grenade_hit_disabled, laser_hit_disabled,
      shotgun_hit_disabled, hook_hit_disabled, is_super, invincible, has_telegun_gun, has_telegun_grenade, has_telegun_laser;
  int freeze_start, freeze_end;
  bool is_in_freeze, deep_frozen, live_frozen;
} dd_character_core;

static const float dd_physical_size = 28.0f;

static float dd_velocity_ramp(float value, float start, float range, float curvature) {
  if (value < start) return 1.0f;
  return 1.0f / powf(curvature, (value - start) / range);
}

static void dd_core_set_hooked_player(dd_character_core *core, int hooked_player) {
  /* DDNet also maintains the hooked player's m_AttachedPlayers set here; no
   * physics reads it, so it is not kept. */
  core->hooked_player = hooked_player;
}

static void dd_core_tick_deferred(dd_character_core *core);

/* CCharacterCore::IsSwitchActiveCb */
static bool dd_core_switch_active_cb(unsigned char number, void *user) {
  const dd_character_core *core = (const dd_character_core *)user;
  if (core->world && core->world->num_switchers > 0)
    if (core->id != -1 && dd_teams_team(core->teams, core->id) != dd_teams_super(core->teams))
      return number < core->world->num_switchers && core->world->switchers[number].status[dd_teams_team(core->teams, core->id)];
  return false;
}

static void dd_core_init(dd_character_core *core, dd_world_core *world, const dd_collision *collision, dd_teams_core *teams) {
  memset(core, 0, sizeof(*core));
  core->world = world;
  core->collision = collision;
  core->teams = teams;
  core->id = -1;
  core->hooked_player = -1;
  dd_tuning_default(&core->tuning);
}

/* CCharacterCore::Reset */
static void dd_core_reset(dd_character_core *core) {
  core->pos = dd_v2(0, 0);
  core->vel = dd_v2(0, 0);
  core->new_hook = false;
  core->hook_pos = dd_v2(0, 0);
  core->hook_dir = dd_v2(0, 0);
  core->hook_tele_base = dd_v2(0, 0);
  core->hook_tick = 0;
  core->hook_state = DD_HOOK_IDLE;
  dd_core_set_hooked_player(core, -1);
  core->jumped = 0;
  core->jumped_total = 0;
  core->jumps = 2;
  core->triggered_events = 0;

  core->solo = false;
  core->jetpack = false;
  core->collision_disabled = false;
  core->endless_hook = false;
  core->endless_jump = false;
  core->hammer_hit_disabled = false;
  core->grenade_hit_disabled = false;
  core->laser_hit_disabled = false;
  core->shotgun_hit_disabled = false;
  core->hook_hit_disabled = false;
  core->is_super = false;
  core->invincible = false;
  core->has_telegun_gun = false;
  core->has_telegun_grenade = false;
  core->has_telegun_laser = false;
  core->freeze_start = 0;
  core->freeze_end = 0;
  core->is_in_freeze = false;
  core->deep_frozen = false;
  core->live_frozen = false;

  core->input.target_x = 0;
  core->input.target_y = -1;
}

static void dd_core_tick(dd_character_core *core, bool use_input, bool do_deferred_tick) {
  const dd_tuning *tuning = &core->tuning;
  core->move_restrictions =
      dd_col_get_move_restrictions(core->collision, use_input ? dd_core_switch_active_cb : NULL, core, core->pos, 18.0f, -1);
  core->triggered_events = 0;

  const bool grounded = dd_col_is_on_ground(core->collision, core->pos, dd_physical_size);
  dd_vec2 target_direction = dd_v2_normalize(dd_v2((float)core->input.target_x, (float)core->input.target_y));

  core->vel.y += dd_tune(tuning->gravity);

  float max_speed = grounded ? dd_tune(tuning->ground_control_speed) : dd_tune(tuning->air_control_speed);
  float accel = grounded ? dd_tune(tuning->ground_control_accel) : dd_tune(tuning->air_control_accel);
  float friction = grounded ? dd_tune(tuning->ground_friction) : dd_tune(tuning->air_friction);

  if (use_input) {
    core->direction = core->input.direction;

    float tmp_angle = atan2f((float)core->input.target_y, (float)core->input.target_x);
    if (tmp_angle < -(dd_pi / 2.0f))
      core->angle = (int)((tmp_angle + (2.0f * dd_pi)) * 256.0f);
    else
      core->angle = (int)(tmp_angle * 256.0f);

    if (core->input.jump) {
      if (!(core->jumped & 1)) {
        if (grounded && (!(core->jumped & 2) || core->jumps != 0)) {
          core->triggered_events |= DD_COREEVENT_GROUND_JUMP;
          core->vel.y = -dd_tune(tuning->ground_jump_impulse);
          if (core->jumps > 1)
            core->jumped |= 1;
          else
            core->jumped |= 3;
          core->jumped_total = 0;
        } else if (!(core->jumped & 2)) {
          core->triggered_events |= DD_COREEVENT_AIR_JUMP;
          core->vel.y = -dd_tune(tuning->air_jump_impulse);
          core->jumped |= 3;
          core->jumped_total++;
        }
      }
    } else {
      core->jumped &= ~1;
    }

    if (core->input.hook) {
      if (core->hook_state == DD_HOOK_IDLE) {
        core->hook_state = DD_HOOK_FLYING;
        core->hook_pos = dd_v2_add(core->pos, dd_v2_mul(dd_v2_mul(target_direction, dd_physical_size), 1.5f));
        core->hook_dir = target_direction;
        dd_core_set_hooked_player(core, -1);
        core->hook_tick = (int)((float)DD_PHYS_SERVER_TICK_SPEED * (1.25f - dd_tune(tuning->hook_duration)));
        core->triggered_events |= DD_COREEVENT_HOOK_LAUNCH;
      }
    } else {
      dd_core_set_hooked_player(core, -1);
      core->hook_state = DD_HOOK_IDLE;
      core->hook_pos = core->pos;
    }
  }

  if (grounded) {
    core->jumped &= ~2;
    core->jumped_total = 0;
  }

  if (core->direction < 0) core->vel.x = dd_saturated_add(-max_speed, max_speed, core->vel.x, -accel);
  if (core->direction > 0) core->vel.x = dd_saturated_add(-max_speed, max_speed, core->vel.x, accel);
  if (core->direction == 0) core->vel.x *= friction;

  if (core->hook_state == DD_HOOK_IDLE) {
    dd_core_set_hooked_player(core, -1);
    core->hook_pos = core->pos;
  } else if (core->hook_state >= DD_HOOK_RETRACT_START && core->hook_state < DD_HOOK_RETRACT_END) {
    core->hook_state++;
  } else if (core->hook_state == DD_HOOK_RETRACT_END) {
    core->triggered_events |= DD_COREEVENT_HOOK_RETRACT;
    core->hook_state = DD_HOOK_RETRACTED;
  } else if (core->hook_state == DD_HOOK_FLYING) {
    dd_vec2 hook_base = core->pos;
    if (core->new_hook) hook_base = core->hook_tele_base;
    dd_vec2 new_pos = dd_v2_add(core->hook_pos, dd_v2_mul(core->hook_dir, dd_tune(tuning->hook_fire_speed)));
    if (dd_v2_distance(hook_base, new_pos) > dd_tune(tuning->hook_length)) {
      core->hook_state = DD_HOOK_RETRACT_START;
      new_pos = dd_v2_add(hook_base, dd_v2_mul(dd_v2_normalize(dd_v2_sub(new_pos, hook_base)), dd_tune(tuning->hook_length)));
      core->reset = true;
    }

    bool going_to_hit_ground = false;
    bool going_to_retract = false;
    bool going_through_tele = false;
    int tele_nr = 0;
    int hit = dd_col_intersect_line_tele_hook(core->collision, core->hook_pos, new_pos, &new_pos, NULL, &tele_nr);

    if (hit) {
      if (hit == DD_TILE_NOHOOK)
        going_to_retract = true;
      else if (hit == DD_TILE_TELEINHOOK)
        going_through_tele = true;
      else
        going_to_hit_ground = true;
      core->reset = true;
    }

    if (!core->hook_hit_disabled && core->world && dd_tune(tuning->player_hooking) != 0.0f &&
        (core->hook_state == DD_HOOK_FLYING || !core->new_hook)) {
      float distance = 0.0f;
      for (uint64_t left = core->world->occupied; left; left &= left - 1) {
        const int i = dd_lowest_bit(left);
        dd_character_core *other = core->world->characters[i];
        if (!other || other == core ||
            (!(core->is_super || other->is_super) &&
             ((core->id != -1 && !dd_teams_can_collide(core->teams, i, core->id)) || other->solo || core->solo)))
          continue;

        dd_vec2 closest_point;
        if (dd_closest_point_on_line(core->hook_pos, new_pos, other->pos, &closest_point)) {
          if (dd_v2_distance(other->pos, closest_point) < dd_physical_size + 2.0f) {
            if (core->hooked_player == -1 || dd_v2_distance(core->hook_pos, other->pos) < distance) {
              core->triggered_events |= DD_COREEVENT_HOOK_ATTACH_PLAYER;
              core->hook_state = DD_HOOK_GRABBED;
              dd_core_set_hooked_player(core, i);
              distance = dd_v2_distance(core->hook_pos, other->pos);
            }
          }
        }
      }
    }

    if (core->hook_state == DD_HOOK_FLYING) {
      if (going_to_hit_ground) {
        core->triggered_events |= DD_COREEVENT_HOOK_ATTACH_GROUND;
        core->hook_state = DD_HOOK_GRABBED;
      } else if (going_to_retract) {
        core->triggered_events |= DD_COREEVENT_HOOK_HIT_NOHOOK;
        core->hook_state = DD_HOOK_RETRACT_START;
      }

      if (going_through_tele && core->world && core->collision->tele_outs[tele_nr - 1].count > 0) {
        core->triggered_events = 0;
        dd_core_set_hooked_player(core, -1);

        core->new_hook = true;
        int random_out = dd_world_random_or0(core->world, core->collision->tele_outs[tele_nr - 1].count);
        core->hook_pos = dd_v2_add(core->collision->tele_outs[tele_nr - 1].positions[random_out],
                                   dd_v2_mul(dd_v2_mul(target_direction, dd_physical_size), 1.5f));
        core->hook_dir = target_direction;
        core->hook_tele_base = core->hook_pos;
      } else {
        core->hook_pos = new_pos;
      }
    }
  }

  if (core->hook_state == DD_HOOK_GRABBED) {
    if (core->hooked_player != -1 && core->world) {
      dd_character_core *other = core->world->characters[core->hooked_player];
      if (other && core->id != -1 && dd_teams_can_keep_hook(core->teams, core->id, other->id)) {
        core->hook_pos = other->pos;
      } else {
        dd_core_set_hooked_player(core, -1);
        core->hook_state = DD_HOOK_RETRACTED;
        core->hook_pos = core->pos;
      }
    }

    if (core->hooked_player == -1 && dd_v2_distance(core->hook_pos, core->pos) > 46.0f) {
      dd_vec2 hook_vel = dd_v2_mul(dd_v2_normalize(dd_v2_sub(core->hook_pos, core->pos)), dd_tune(tuning->hook_drag_accel));
      if (hook_vel.y > 0) hook_vel.y *= 0.3f;

      if ((hook_vel.x < 0 && core->direction < 0) || (hook_vel.x > 0 && core->direction > 0))
        hook_vel.x *= 0.95f;
      else
        hook_vel.x *= 0.75f;

      dd_vec2 new_vel = dd_v2_add(core->vel, hook_vel);

      const float new_vel_length = dd_v2_length(new_vel);
      if (new_vel_length < dd_tune(tuning->hook_drag_speed) || new_vel_length < dd_v2_length(core->vel)) core->vel = new_vel;
    }

    core->hook_tick++;
    if (core->hooked_player != -1 &&
        (core->hook_tick > DD_PHYS_SERVER_TICK_SPEED + DD_PHYS_SERVER_TICK_SPEED / 5 || (core->world && !core->world->characters[core->hooked_player]))) {
      dd_core_set_hooked_player(core, -1);
      core->hook_state = DD_HOOK_RETRACTED;
      core->hook_pos = core->pos;
    }
  }

  if (do_deferred_tick) dd_core_tick_deferred(core);
}

static void dd_core_tick_deferred(dd_character_core *core) {
  const dd_tuning *tuning = &core->tuning;
  if (core->world) {
    for (uint64_t left = core->world->occupied; left; left &= left - 1) {
      const int i = dd_lowest_bit(left);
      dd_character_core *other = core->world->characters[i];
      if (!other) continue;
      if (other == core || (core->id != -1 && !dd_teams_can_collide(core->teams, core->id, i))) continue;
      if (!(core->is_super || other->is_super) && (core->solo || other->solo)) continue;

      /* Not hooked and clearly out of collision range: nothing below can
       * apply (distance >= |dx|, |dy| holds for the float sqrt too). */
      if (core->hooked_player != i) {
        dd_vec2 d = dd_v2_sub(core->pos, other->pos);
        if (fabsf(d.x) > dd_physical_size * 1.5f || fabsf(d.y) > dd_physical_size * 1.5f) continue;
      }

      float distance = dd_v2_distance(core->pos, other->pos);
      if (distance > 0) {
        dd_vec2 dir = dd_v2_normalize(dd_v2_sub(core->pos, other->pos));

        bool can_collide = (core->is_super || other->is_super) ||
                           (!core->collision_disabled && !other->collision_disabled && dd_tune(tuning->player_collision) != 0.0f);

        if (can_collide && distance < dd_physical_size * 1.25f) {
          float a = (dd_physical_size * 1.45f - distance);
          float velocity = 0.5f;
          if (dd_v2_length(core->vel) > 0.0001f) velocity = 1 - (dd_v2_dot(dd_v2_normalize(core->vel), dir) + 1) / 2;

          core->vel = dd_v2_add(core->vel, dd_v2_mul(dd_v2_mul(dir, a), velocity * 0.75f));
          core->vel = dd_v2_mul(core->vel, 0.85f);
        }

        if (!core->hook_hit_disabled && core->hooked_player == i && dd_tune(tuning->player_hooking) != 0.0f) {
          if (distance > dd_physical_size * 1.50f) {
            float hook_accel = dd_tune(tuning->hook_drag_accel) * (distance / dd_tune(tuning->hook_length));
            float drag_speed = dd_tune(tuning->hook_drag_speed);

            dd_vec2 temp;
            temp.x = dd_saturated_add(-drag_speed, drag_speed, other->vel.x, hook_accel * dir.x * 1.5f);
            temp.y = dd_saturated_add(-drag_speed, drag_speed, other->vel.y, hook_accel * dir.y * 1.5f);
            other->vel = dd_clamp_vel(other->move_restrictions, temp);
            temp.x = dd_saturated_add(-drag_speed, drag_speed, core->vel.x, -hook_accel * dir.x * 0.25f);
            temp.y = dd_saturated_add(-drag_speed, drag_speed, core->vel.y, -hook_accel * dir.y * 0.25f);
            core->vel = dd_clamp_vel(core->move_restrictions, temp);
          }
        }
      }
    }

    if (core->hook_state != DD_HOOK_FLYING) core->new_hook = false;
  }

  if (dd_v2_length(core->vel) > 6000) core->vel = dd_v2_mul(dd_v2_normalize(core->vel), 6000);
}

static void dd_core_move(dd_character_core *core) {
  const dd_tuning *tuning = &core->tuning;
  float ramp_value = dd_velocity_ramp(dd_v2_length(core->vel) * 50, dd_tune(tuning->velramp_start), dd_tune(tuning->velramp_range),
                                      dd_tune(tuning->velramp_curvature));

  core->vel.x = core->vel.x * ramp_value;

  dd_vec2 new_pos = core->pos;
  dd_vec2 old_vel = core->vel;
  bool grounded = false;
  dd_col_move_box(core->collision, &new_pos, &core->vel, dd_v2(dd_physical_size, dd_physical_size),
                  dd_v2(dd_tune(tuning->ground_elasticity_x), dd_tune(tuning->ground_elasticity_y)), &grounded);

  if (grounded) {
    core->jumped &= ~2;
    core->jumped_total = 0;
  }

  core->colliding = 0;
  if (core->vel.x < 0.001f && core->vel.x > -0.001f) {
    if (old_vel.x > 0)
      core->colliding = 1;
    else if (old_vel.x < 0)
      core->colliding = 2;
  } else {
    core->left_wall = true;
  }

  core->vel.x = core->vel.x * (1.0f / ramp_value);

  if (core->world && (core->is_super || (dd_tune(tuning->player_collision) != 0.0f && !core->collision_disabled && !core->solo))) {
    float distance = dd_v2_distance(core->pos, new_pos);
    if (distance > 0) {
      /* DDNet tests every other character at every step along the path. The
       * per-character conditions do not change along it, and the steps all
       * lie on the segment, so characters that fail them or sit outside the
       * segment's box grown by the hit distance (plus a unit for rounding)
       * can never be hit. Dropping them keeps the order of the remaining
       * tests, so the first hit, and the result, stay the same. */
      const float reach = dd_physical_size + 1.0f;
      const float min_x = dd_minf(core->pos.x, new_pos.x) - reach, max_x = dd_maxf(core->pos.x, new_pos.x) + reach;
      const float min_y = dd_minf(core->pos.y, new_pos.y) - reach, max_y = dd_maxf(core->pos.y, new_pos.y) + reach;
      const dd_character_core *candidates[DD_PHYS_MAX_CLIENTS];
      int num_candidates = 0;
      for (uint64_t left = core->world->occupied; left; left &= left - 1) {
        const int p = dd_lowest_bit(left);
        const dd_character_core *other = core->world->characters[p];
        if (!other || other == core) continue;
        if ((!(other->is_super || core->is_super) &&
             (core->solo || other->solo || other->collision_disabled || (core->id != -1 && !dd_teams_can_collide(core->teams, core->id, p)))))
          continue;
        if (other->pos.x < min_x || other->pos.x > max_x || other->pos.y < min_y || other->pos.y > max_y) continue;
        candidates[num_candidates++] = other;
      }
      if (num_candidates > 0) {
        int end = (int)(distance + 1);
        dd_vec2 last_pos = core->pos;
        for (int i = 0; i < end; i++) {
          float a = i / distance;
          dd_vec2 pos = dd_v2_mix(core->pos, new_pos, a);
          for (int c = 0; c < num_candidates; c++) {
            const dd_character_core *other = candidates[c];
            float d = dd_v2_distance(pos, other->pos);
            if (d < dd_physical_size) {
              if (a > 0.0f)
                core->pos = last_pos;
              else if (dd_v2_distance(new_pos, other->pos) > d)
                core->pos = new_pos;
              return;
            }
          }
          last_pos = pos;
        }
      }
    }
  }

  core->pos = new_pos;
}

static void dd_core_write(const dd_character_core *core, dd_netobj_character_core *obj) {
  obj->m_X = dd_round_to_int(core->pos.x);
  obj->m_Y = dd_round_to_int(core->pos.y);
  obj->m_VelX = dd_round_to_int(core->vel.x * 256.0f);
  obj->m_VelY = dd_round_to_int(core->vel.y * 256.0f);
  obj->m_HookState = core->hook_state;
  obj->m_HookTick = core->hook_tick;
  obj->m_HookX = dd_round_to_int(core->hook_pos.x);
  obj->m_HookY = dd_round_to_int(core->hook_pos.y);
  obj->m_HookDx = dd_round_to_int(core->hook_dir.x * 256.0f);
  obj->m_HookDy = dd_round_to_int(core->hook_dir.y * 256.0f);
  obj->m_HookedPlayer = core->hooked_player;
  obj->m_Jumped = core->jumped;
  obj->m_Direction = core->direction;
  obj->m_Angle = core->angle;
}

static void dd_core_read(dd_character_core *core, const dd_netobj_character_core *obj) {
  core->pos.x = (float)obj->m_X;
  core->pos.y = (float)obj->m_Y;
  core->vel.x = obj->m_VelX / 256.0f;
  core->vel.y = obj->m_VelY / 256.0f;
  core->hook_state = obj->m_HookState;
  core->hook_tick = obj->m_HookTick;
  core->hook_pos.x = (float)obj->m_HookX;
  core->hook_pos.y = (float)obj->m_HookY;
  core->hook_dir.x = obj->m_HookDx / 256.0f;
  core->hook_dir.y = obj->m_HookDy / 256.0f;
  dd_core_set_hooked_player(core, obj->m_HookedPlayer);
  core->jumped = obj->m_Jumped;
  core->direction = obj->m_Direction;
  core->angle = obj->m_Angle;
}

/* CCharacterCore::ReadDDNet */
static void dd_core_read_ddnet(dd_character_core *core, const dd_netobj_ddnet_character *obj) {
  core->solo = obj->m_Flags & DD_CHARACTERFLAG_SOLO;
  core->jetpack = obj->m_Flags & DD_CHARACTERFLAG_JETPACK;
  core->collision_disabled = obj->m_Flags & DD_CHARACTERFLAG_COLLISION_DISABLED;
  core->hammer_hit_disabled = obj->m_Flags & DD_CHARACTERFLAG_HAMMER_HIT_DISABLED;
  core->shotgun_hit_disabled = obj->m_Flags & DD_CHARACTERFLAG_SHOTGUN_HIT_DISABLED;
  core->grenade_hit_disabled = obj->m_Flags & DD_CHARACTERFLAG_GRENADE_HIT_DISABLED;
  core->laser_hit_disabled = obj->m_Flags & DD_CHARACTERFLAG_LASER_HIT_DISABLED;
  core->hook_hit_disabled = obj->m_Flags & DD_CHARACTERFLAG_HOOK_HIT_DISABLED;
  core->is_super = obj->m_Flags & DD_CHARACTERFLAG_SUPER;
  core->invincible = obj->m_Flags & DD_CHARACTERFLAG_INVINCIBLE;
  core->endless_hook = obj->m_Flags & DD_CHARACTERFLAG_ENDLESS_HOOK;
  core->endless_jump = obj->m_Flags & DD_CHARACTERFLAG_ENDLESS_JUMP;
  core->freeze_end = obj->m_FreezeEnd;
  core->deep_frozen = obj->m_FreezeEnd == -1;
  core->live_frozen = (obj->m_Flags & DD_CHARACTERFLAG_MOVEMENTS_DISABLED) != 0;
  core->has_telegun_grenade = obj->m_Flags & DD_CHARACTERFLAG_TELEGUN_GRENADE;
  core->has_telegun_gun = obj->m_Flags & DD_CHARACTERFLAG_TELEGUN_GUN;
  core->has_telegun_laser = obj->m_Flags & DD_CHARACTERFLAG_TELEGUN_LASER;
  core->weapons[DD_WEAPON_ID_HAMMER].got = (obj->m_Flags & DD_CHARACTERFLAG_WEAPON_HAMMER) != 0;
  core->weapons[DD_WEAPON_ID_GUN].got = (obj->m_Flags & DD_CHARACTERFLAG_WEAPON_GUN) != 0;
  core->weapons[DD_WEAPON_ID_SHOTGUN].got = (obj->m_Flags & DD_CHARACTERFLAG_WEAPON_SHOTGUN) != 0;
  core->weapons[DD_WEAPON_ID_GRENADE].got = (obj->m_Flags & DD_CHARACTERFLAG_WEAPON_GRENADE) != 0;
  core->weapons[DD_WEAPON_ID_LASER].got = (obj->m_Flags & DD_CHARACTERFLAG_WEAPON_LASER) != 0;
  core->weapons[DD_WEAPON_ID_NINJA].got = (obj->m_Flags & DD_CHARACTERFLAG_WEAPON_NINJA) != 0;
  core->jumps = obj->m_Jumps;
  if (obj->m_JumpedTotal != -1) core->jumped_total = obj->m_JumpedTotal;
  if (obj->m_NinjaActivationTick != -1) core->ninja.activation_tick = obj->m_NinjaActivationTick;
  if (obj->m_FreezeStart != -1) {
    core->freeze_start = obj->m_FreezeStart;
    core->is_in_freeze = obj->m_Flags & DD_CHARACTERFLAG_IN_FREEZE;
  }
}

static void dd_core_quantize(dd_character_core *core) {
  dd_netobj_character_core obj;
  memset(&obj, 0, sizeof(obj));
  dd_core_write(core, &obj);
  dd_core_read(core, &obj);
}

static void dd_evolve_character(const dd_collision *collision, dd_netobj_character *character, int tick) {
  /* gameclient.cpp, the Evolve lambda: a fresh core in a fresh empty world. */
  dd_world_core world;
  memset(&world, 0, sizeof(world));
  dd_teams_core teams;
  dd_teams_reset(&teams);
  dd_character_core core;
  dd_core_init(&core, &world, collision, &teams);
  dd_core_read(&core, &character->core);

  while (character->core.m_Tick < tick) {
    character->core.m_Tick++;
    dd_core_tick(&core, false, true);
    dd_core_move(&core);
    dd_core_quantize(&core);
  }

  const int core_tick = character->core.m_Tick;
  dd_core_write(&core, &character->core);
  character->core.m_Tick = core_tick;
}

#endif /* DD_CORE_INTERNAL_H */
