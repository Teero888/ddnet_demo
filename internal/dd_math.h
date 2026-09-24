/*
 * internal/dd_math.h - DDNet's base/math.h and base/vmath.h, and the tuning
 * parameters (game/tuning.h), for the demo library's physics port.
 *
 * INTERNAL: not part of the library's API. All math is single precision in
 * DDNet's operation order; see dd_physics.h for the float rules.
 */
#ifndef DD_MATH_INTERNAL_H
#define DD_MATH_INTERNAL_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __FAST_MATH__
#error "the demo library's physics must not be compiled with -ffast-math: results would no longer match DDNet"
#endif

/* No FMA contraction: DDNet's release builds round after every operation.
 * GCC takes this per function (see DD_PHYS_BEGIN in dd_physics.h). */
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

#define DD_PHYS_MAX_CLIENTS 64
#define DD_PHYS_SERVER_TICK_SPEED 50

typedef struct {
  float x, y;
} dd_vec2;

static const float dd_pi = 3.1415926535897932384626433f;

static inline int dd_round_to_int(float f) { return f > 0 ? (int)(f + 0.5f) : (int)(f - 0.5f); }

static inline int dd_clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static inline float dd_clampf(float v, float lo, float hi) { return v < lo ? lo : (hi < v ? hi : v); }

static inline float dd_absf(float a) { return a < 0 ? -a : a; }

static inline dd_vec2 dd_v2(float x, float y) {
  dd_vec2 v = {x, y};
  return v;
}

static inline dd_vec2 dd_v2_add(dd_vec2 a, dd_vec2 b) { return dd_v2(a.x + b.x, a.y + b.y); }

static inline dd_vec2 dd_v2_sub(dd_vec2 a, dd_vec2 b) { return dd_v2(a.x - b.x, a.y - b.y); }

static inline dd_vec2 dd_v2_mul(dd_vec2 a, float s) { return dd_v2(a.x * s, a.y * s); }

static inline bool dd_v2_eq(dd_vec2 a, dd_vec2 b) { return a.x == b.x && a.y == b.y; }

static inline float dd_v2_dot(dd_vec2 a, dd_vec2 b) { return a.x * b.x + a.y * b.y; }

static inline float dd_v2_length(dd_vec2 a) { return sqrtf(dd_v2_dot(a, a)); }

static inline float dd_v2_distance(dd_vec2 a, dd_vec2 b) { return dd_v2_length(dd_v2_sub(a, b)); }

static inline dd_vec2 dd_v2_mix(dd_vec2 a, dd_vec2 b, float amount) { return dd_v2_add(a, dd_v2_mul(dd_v2_sub(b, a), amount)); }

static inline dd_vec2 dd_v2_normalize(dd_vec2 v) {
  float divisor = dd_v2_length(v);
  if (divisor == 0.0f) return dd_v2(0.0f, 0.0f);
  float l = 1.0f / divisor;
  return dd_v2(v.x * l, v.y * l);
}

static inline bool dd_closest_point_on_line(dd_vec2 line_a, dd_vec2 line_b, dd_vec2 target, dd_vec2 *out) {
  dd_vec2 ab = dd_v2_sub(line_b, line_a);
  float squared_magnitude_ab = dd_v2_dot(ab, ab);
  if (squared_magnitude_ab > 0) {
    dd_vec2 ap = dd_v2_sub(target, line_a);
    float ap_dot_ab = dd_v2_dot(ap, ab);
    float t = ap_dot_ab / squared_magnitude_ab;
    /* DDNet clamps to the segment: a target just past an end is measured from that end */
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    *out = dd_v2_add(line_a, dd_v2_mul(ab, t));
    return true;
  }
  return false;
}

static inline float dd_saturated_add(float min, float max, float current, float modifier) {
  if (modifier < 0) {
    if (current < min) return current;
    current += modifier;
    if (current < min) current = min;
    return current;
  } else {
    if (current > max) return current;
    current += modifier;
    if (current > max) current = max;
    return current;
  }
}

static inline float dd_minf(float a, float b) { return a < b ? a : b; }
static inline float dd_maxf(float a, float b) { return a > b ? a : b; }
static inline int dd_mini(int a, int b) { return a < b ? a : b; }
static inline int dd_maxi(int a, int b) { return a > b ? a : b; }
static inline int dd_absi(int a) { return a < 0 ? -a : a; }
static inline float dd_v2_length_squared(dd_vec2 a) { return dd_v2_dot(a, a); }
/* direction(angle) */
static inline dd_vec2 dd_direction(float angle) { return dd_v2(cosf(angle), sinf(angle)); }
/* angle(vec2) */
static inline float dd_angle(dd_vec2 a) {
  if (a.x == 0 && a.y == 0) return 0.0f;
  else if (a.x == 0) return a.y < 0 ? -dd_pi / 2 : dd_pi / 2;
  float result = atanf(a.y / a.x);
  if (a.x < 0) result = result + dd_pi;
  return result;
}

/* ---- tuning (game/tuning.h, CTuneParam) ----
 * DDNet stores every tuning value as an int of value * 100 and converts back
 * on every read, so 0.95 is really 95 / 100.0f. The same storage is kept here. */
typedef struct {
  int ground_control_speed, ground_control_accel, ground_friction, ground_jump_impulse, air_jump_impulse, air_control_speed,
      air_control_accel, air_friction, hook_length, hook_fire_speed, hook_drag_accel, hook_drag_speed, gravity, velramp_start,
      velramp_range, velramp_curvature, gun_curvature, gun_speed, gun_lifetime, shotgun_curvature, shotgun_speed, shotgun_speeddiff,
      shotgun_lifetime, grenade_curvature, grenade_speed, grenade_lifetime, laser_reach, laser_bounce_delay, laser_bounce_num,
      laser_bounce_cost, laser_damage, player_collision, player_hooking, jetpack_strength, shotgun_strength, explosion_strength,
      hammer_strength, hook_duration, hammer_fire_delay, gun_fire_delay, shotgun_fire_delay, grenade_fire_delay, laser_fire_delay,
      ninja_fire_delay, hammer_hit_fire_delay, ground_elasticity_x, ground_elasticity_y;
} dd_tuning;

#define DD_TUNING_NUM ((int)(sizeof(dd_tuning) / sizeof(int)))

static inline float dd_tune(int param) { return param / 100.0f; } /* CTuneParam::operator float */

static int dd_tune_param(float value) {
  const float fixed = value * 100.0f;
  if (fixed >= (float)INT32_MIN && fixed < (float)INT32_MAX) return (int)fixed;
  return INT32_MIN;
}

static void dd_tuning_default(dd_tuning *t) {
  const float tick_speed = (float)DD_PHYS_SERVER_TICK_SPEED;
  t->ground_control_speed = dd_tune_param(10.0f);
  t->ground_control_accel = dd_tune_param(100.0f / tick_speed);
  t->ground_friction = dd_tune_param(0.5f);
  t->ground_jump_impulse = dd_tune_param(13.2f);
  t->air_jump_impulse = dd_tune_param(12.0f);
  t->air_control_speed = dd_tune_param(250.0f / tick_speed);
  t->air_control_accel = dd_tune_param(1.5f);
  t->air_friction = dd_tune_param(0.95f);
  t->hook_length = dd_tune_param(380.0f);
  t->hook_fire_speed = dd_tune_param(80.0f);
  t->hook_drag_accel = dd_tune_param(3.0f);
  t->hook_drag_speed = dd_tune_param(15.0f);
  t->gravity = dd_tune_param(0.5f);
  t->velramp_start = dd_tune_param(550);
  t->velramp_range = dd_tune_param(2000);
  t->velramp_curvature = dd_tune_param(1.4f);
  t->gun_curvature = dd_tune_param(1.25f);
  t->gun_speed = dd_tune_param(2200.0f);
  t->gun_lifetime = dd_tune_param(2.0f);
  t->shotgun_curvature = dd_tune_param(1.25f);
  t->shotgun_speed = dd_tune_param(2750.0f);
  t->shotgun_speeddiff = dd_tune_param(0.8f);
  t->shotgun_lifetime = dd_tune_param(0.20f);
  t->grenade_curvature = dd_tune_param(7.0f);
  t->grenade_speed = dd_tune_param(1000.0f);
  t->grenade_lifetime = dd_tune_param(2.0f);
  t->laser_reach = dd_tune_param(800.0f);
  t->laser_bounce_delay = dd_tune_param(150);
  t->laser_bounce_num = dd_tune_param(1000);
  t->laser_bounce_cost = dd_tune_param(0);
  t->laser_damage = dd_tune_param(5);
  t->player_collision = dd_tune_param(1);
  t->player_hooking = dd_tune_param(1);
  t->jetpack_strength = dd_tune_param(400.0f);
  t->shotgun_strength = dd_tune_param(10.0f);
  t->explosion_strength = dd_tune_param(6.0f);
  t->hammer_strength = dd_tune_param(1.0f);
  t->hook_duration = dd_tune_param(1.25f);
  t->hammer_fire_delay = dd_tune_param(125);
  t->gun_fire_delay = dd_tune_param(125);
  t->shotgun_fire_delay = dd_tune_param(500);
  t->grenade_fire_delay = dd_tune_param(500);
  t->laser_fire_delay = dd_tune_param(800);
  t->ninja_fire_delay = dd_tune_param(800);
  t->hammer_hit_fire_delay = dd_tune_param(320);
  t->ground_elasticity_x = dd_tune_param(0);
  t->ground_elasticity_y = dd_tune_param(0);
}

/* CTuningParams::GetWeaponFireDelay, in seconds */
static inline float dd_tuning_fire_delay(const dd_tuning *t, int weapon) {
  switch (weapon) {
  case 0: return (float)dd_tune(t->hammer_fire_delay) / 1000.0f;
  case 1: return (float)dd_tune(t->gun_fire_delay) / 1000.0f;
  case 2: return (float)dd_tune(t->shotgun_fire_delay) / 1000.0f;
  case 3: return (float)dd_tune(t->grenade_fire_delay) / 1000.0f;
  case 4: return (float)dd_tune(t->laser_fire_delay) / 1000.0f;
  case 5: return (float)dd_tune(t->ninja_fire_delay) / 1000.0f;
  default: return 0.0f;
  }
}

#endif /* DD_MATH_INTERNAL_H */
