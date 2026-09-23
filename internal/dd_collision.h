/*
 * internal/dd_collision.h - 1:1 port of DDNet's CCollision (game/collision.cpp).
 *
 * INTERNAL: not part of the library's API.
 */
#ifndef DD_COLLISION_INTERNAL_H
#define DD_COLLISION_INTERNAL_H

#include "dd_math.h"
#ifndef DDNET_MAP_LOADER_H
#include "../ddnet_maploader/ddnet_map_loader.h"
#endif

/* game/mapitems.h */
enum {
  DD_TILE_AIR = 0,
  DD_TILE_SOLID = 1,
  DD_TILE_DEATH = 2,
  DD_TILE_NOHOOK = 3,
  DD_TILE_NOLASER = 4,
  DD_TILE_THROUGH_CUT = 5,
  DD_TILE_THROUGH = 6,
  DD_TILE_JUMP = 7,
  DD_TILE_FREEZE = 9,
  DD_TILE_TELEINEVIL = 10,
  DD_TILE_UNFREEZE = 11,
  DD_TILE_DFREEZE = 12,
  DD_TILE_DUNFREEZE = 13,
  DD_TILE_TELEINWEAPON = 14,
  DD_TILE_TELEINHOOK = 15,
  DD_TILE_WALLJUMP = 16,
  DD_TILE_EHOOK_ENABLE = 17,
  DD_TILE_EHOOK_DISABLE = 18,
  DD_TILE_HIT_ENABLE = 19,
  DD_TILE_HIT_DISABLE = 20,
  DD_TILE_SOLO_ENABLE = 21,
  DD_TILE_SOLO_DISABLE = 22,
  DD_TILE_SWITCHTIMEDOPEN = 22,
  DD_TILE_SWITCHTIMEDCLOSE = 23,
  DD_TILE_SWITCHOPEN = 24,
  DD_TILE_SWITCHCLOSE = 25,
  DD_TILE_TELEIN = 26,
  DD_TILE_TELEOUT = 27,
  DD_TILE_SPEED_BOOST_OLD = 28,
  DD_TILE_SPEED_BOOST = 29,
  DD_TILE_TELECHECK = 29,
  DD_TILE_TELECHECKOUT = 30,
  DD_TILE_TELECHECKIN = 31,
  DD_TILE_REFILL_JUMPS = 32,
  DD_TILE_START = 33,
  DD_TILE_FINISH = 34,
  DD_TILE_TIME_CHECKPOINT_FIRST = 35,
  DD_TILE_TIME_CHECKPOINT_LAST = 59,
  DD_TILE_STOP = 60,
  DD_TILE_STOPS = 61,
  DD_TILE_STOPA = 62,
  DD_TILE_TELECHECKINEVIL = 63,
  DD_TILE_CP = 64,
  DD_TILE_CP_F = 65,
  DD_TILE_THROUGH_ALL = 66,
  DD_TILE_THROUGH_DIR = 67,
  DD_TILE_TUNE = 68,
  DD_TILE_OLDLASER = 71,
  DD_TILE_NPC = 72,
  DD_TILE_EHOOK = 73,
  DD_TILE_NOHIT = 74,
  DD_TILE_NPH = 75,
  DD_TILE_UNLOCK_TEAM = 76,
  DD_TILE_ADD_TIME = 79,
  DD_TILE_NPC_DISABLE = 88,
  DD_TILE_UNLIMITED_JUMPS_DISABLE = 89,
  DD_TILE_JETPACK_DISABLE = 90,
  DD_TILE_NPH_DISABLE = 91,
  DD_TILE_SUBTRACT_TIME = 95,
  DD_TILE_TELE_GUN_ENABLE = 96,
  DD_TILE_TELE_GUN_DISABLE = 97,
  DD_TILE_ALLOW_TELE_GUN = 98,
  DD_TILE_ALLOW_BLUE_TELE_GUN = 99,
  DD_TILE_NPC_ENABLE = 104,
  DD_TILE_UNLIMITED_JUMPS_ENABLE = 105,
  DD_TILE_JETPACK_ENABLE = 106,
  DD_TILE_NPH_ENABLE = 107,
  DD_TILE_TELE_GRENADE_ENABLE = 112,
  DD_TILE_TELE_GRENADE_DISABLE = 113,
  DD_TILE_TELE_LASER_ENABLE = 128,
  DD_TILE_TELE_LASER_DISABLE = 129,
  DD_TILE_LFREEZE = 144,
  DD_TILE_LUNFREEZE = 145,

  DD_ENTITY_OFFSET = 255 - 16 * 4,

  DD_TILEFLAG_XFLIP = 1 << 0,
  DD_TILEFLAG_YFLIP = 1 << 1,
  DD_TILEFLAG_OPAQUE = 1 << 2,
  DD_TILEFLAG_ROTATE = 1 << 3,
  DD_ROTATION_0 = 0,
  DD_ROTATION_90 = DD_TILEFLAG_ROTATE,
  DD_ROTATION_180 = DD_TILEFLAG_XFLIP | DD_TILEFLAG_YFLIP,
  DD_ROTATION_270 = DD_TILEFLAG_XFLIP | DD_TILEFLAG_YFLIP | DD_TILEFLAG_ROTATE,

  DD_CANTMOVE_LEFT = 1 << 0,
  DD_CANTMOVE_RIGHT = 1 << 1,
  DD_CANTMOVE_UP = 1 << 2,
  DD_CANTMOVE_DOWN = 1 << 3,

  DD_LAYER_GAME = 0,
  DD_LAYER_FRONT,
  DD_LAYER_TELE,
  DD_LAYER_SPEEDUP,
  DD_LAYER_SWITCH,
  DD_LAYER_TUNE,
};

typedef struct {
  int count, capacity;
  dd_vec2 *positions;
} dd_tele_list;

/* CDoorTile: written at runtime by door entities. */
typedef struct {
  uint8_t index, flags, number;
} dd_door_tile;

typedef bool (*dd_switch_active_fn)(unsigned char number, void *user);

typedef struct {
  int width, height;
  const uint8_t *tiles, *tile_flags;      /* game layer */
  const uint8_t *front, *front_flags;     /* may be NULL */
  const uint8_t *tele_number, *tele_type; /* may be NULL */
  const uint8_t *speedup_force, *speedup_max_speed, *speedup_type; /* may be NULL */
  const short *speedup_angle;
  uint8_t *switch_number, *switch_type, *switch_flags, *switch_delay; /* owned; types cleaned like CCollision::Init */
  const uint8_t *tune_number, *tune_type;                             /* may be NULL */
  dd_door_tile *doors;                                                /* owned; NULL without a switch layer */
  /* owned lookup tables, NULL if they could not be allocated: whether a
   * tile exists (dd_col_tile_exists) and the move restrictions of the game
   * and front layers per tile and direction, and whether a hook can stop in
   * a tile (dd_col_intersect_line_tele_hook) */
  uint8_t *exists, *mr_static, *hook_stop;
  int highest_switch_number;
  /* keyed by teleporter number - 1, like DDNet's maps */
  dd_tele_list tele_ins[256], tele_outs[256], tele_check_outs[256], tele_others[256];
  bool has_hook_tele_ins;
  bool old_teleport_hook;    /* sv_old_teleport_hook */
  bool old_teleport_weapons; /* sv_old_teleport_weapons */
} dd_collision;

/* ClampVel (collision.cpp) */
static inline dd_vec2 dd_clamp_vel(int move_restriction, dd_vec2 vel) {
  if (vel.x > 0 && (move_restriction & DD_CANTMOVE_RIGHT)) vel.x = 0;
  if (vel.x < 0 && (move_restriction & DD_CANTMOVE_LEFT)) vel.x = 0;
  if (vel.y > 0 && (move_restriction & DD_CANTMOVE_DOWN)) vel.y = 0;
  if (vel.y < 0 && (move_restriction & DD_CANTMOVE_UP)) vel.y = 0;
  return vel;
}

static void dd_tele_list_push(dd_tele_list *list, dd_vec2 pos) {
  if (list->count == list->capacity) {
    int capacity = list->capacity ? list->capacity * 2 : 4;
    dd_vec2 *grown = (dd_vec2 *)realloc(list->positions, sizeof(dd_vec2) * (size_t)capacity);
    if (!grown) return;
    list->positions = grown;
    list->capacity = capacity;
  }
  list->positions[list->count++] = pos;
}

static uint8_t *dd_col_copy_plane(const unsigned char *src, size_t size) {
  if (!src) return NULL;
  uint8_t *copy = (uint8_t *)malloc(size);
  if (copy) memcpy(copy, src, size);
  return copy;
}

static void dd_collision_free(dd_collision *col) {
  for (int i = 0; i < 256; ++i) {
    free(col->tele_ins[i].positions);
    free(col->tele_outs[i].positions);
    free(col->tele_check_outs[i].positions);
    free(col->tele_others[i].positions);
  }
  free(col->switch_number);
  free(col->switch_type);
  free(col->switch_flags);
  free(col->switch_delay);
  free(col->doors);
  free(col->exists);
  free(col->mr_static);
  free(col->hook_stop);
  memset(col, 0, sizeof(*col));
}

static void dd_col_build_tables(dd_collision *col);

/* CCollision::Init. The map data must outlive the collision. */
static bool dd_collision_init(dd_collision *col, const map_data_t *map) {
  memset(col, 0, sizeof(*col));
  if (!map || !map->game_layer.data || map->width <= 0 || map->height <= 0) return false;
  const size_t size = (size_t)map->width * (size_t)map->height;
  col->width = map->width;
  col->height = map->height;
  col->tiles = map->game_layer.data;
  col->tile_flags = map->game_layer.flags;
  col->front = map->front_layer.data;
  col->front_flags = map->front_layer.flags;
  col->tele_number = map->tele_layer.number;
  col->tele_type = map->tele_layer.type;
  col->speedup_force = map->speedup_layer.force;
  col->speedup_max_speed = map->speedup_layer.max_speed;
  col->speedup_type = map->speedup_layer.type;
  col->speedup_angle = map->speedup_layer.angle;
  col->tune_number = map->tune_layer.number;
  col->tune_type = map->tune_layer.type;

  if (map->switch_layer.type && map->switch_layer.number) {
    col->switch_number = dd_col_copy_plane(map->switch_layer.number, size);
    col->switch_type = dd_col_copy_plane(map->switch_layer.type, size);
    col->switch_flags = map->switch_layer.flags ? dd_col_copy_plane(map->switch_layer.flags, size) : (uint8_t *)calloc(size, 1);
    col->switch_delay = map->switch_layer.delay ? dd_col_copy_plane(map->switch_layer.delay, size) : (uint8_t *)calloc(size, 1);
    col->doors = (dd_door_tile *)calloc(size, sizeof(dd_door_tile));
    if (!col->switch_number || !col->switch_type || !col->switch_flags || !col->switch_delay || !col->doors) {
      dd_collision_free(col);
      return false;
    }
    for (size_t i = 0; i < size; i++) {
      if (col->switch_number[i] > col->highest_switch_number) col->highest_switch_number = col->switch_number[i];
      col->doors[i].number = col->switch_number[i];
      const unsigned char index = col->switch_type[i];
      if (index <= DD_TILE_NPH_ENABLE) {
        if ((index >= DD_TILE_JUMP && index <= DD_TILE_SUBTRACT_TIME) || index == DD_TILE_ALLOW_TELE_GUN || index == DD_TILE_ALLOW_BLUE_TELE_GUN)
          col->switch_type[i] = index;
        else
          col->switch_type[i] = 0;
      }
    }
  }

  if (col->tele_number && col->tele_type) {
    for (int i = 0; i < col->width * col->height; i++) {
      const unsigned char number = col->tele_number[i];
      const unsigned char type = col->tele_type[i];
      if (!number || !type) continue;
      const dd_vec2 tele_pos = dd_v2(i % col->width * 32.0f + 16.0f, i / col->width * 32.0f + 16.0f);
      if (type == DD_TILE_TELEIN) {
        dd_tele_list_push(&col->tele_ins[number - 1], tele_pos);
      } else if (type == DD_TILE_TELEOUT) {
        dd_tele_list_push(&col->tele_outs[number - 1], tele_pos);
      } else if (type == DD_TILE_TELECHECKOUT) {
        dd_tele_list_push(&col->tele_check_outs[number - 1], tele_pos);
      } else {
        dd_tele_list_push(&col->tele_others[number - 1], tele_pos);
        if (type == DD_TILE_TELEINHOOK) col->has_hook_tele_ins = true;
      }
    }
  }
  dd_col_build_tables(col);
  return true;
}

/* ---- tile access ---- */

static int dd_col_get_tile(const dd_collision *col, int x, int y) {
  if (!col->tiles) return 0;
  int nx = dd_clampi(x / 32, 0, col->width - 1);
  int ny = dd_clampi(y / 32, 0, col->height - 1);
  const int index = ny * col->width + nx;
  if (col->tiles[index] >= DD_TILE_SOLID && col->tiles[index] <= DD_TILE_NOLASER) return col->tiles[index];
  return 0;
}

static int dd_col_is_solid(const dd_collision *col, int x, int y) {
  const int index = dd_col_get_tile(col, x, y);
  return index == DD_TILE_SOLID || index == DD_TILE_NOHOOK;
}

static bool dd_col_check_point(const dd_collision *col, float x, float y) { return dd_col_is_solid(col, dd_round_to_int(x), dd_round_to_int(y)); }

static int dd_col_get_collision_at(const dd_collision *col, float x, float y) {
  return dd_col_get_tile(col, dd_round_to_int(x), dd_round_to_int(y));
}

static int dd_col_get_pure_map_index(const dd_collision *col, float x, float y) {
  int nx = dd_clampi(dd_round_to_int(x) / 32, 0, col->width - 1);
  int ny = dd_clampi(dd_round_to_int(y) / 32, 0, col->height - 1);
  return ny * col->width + nx;
}

static int dd_col_get_front_tile(const dd_collision *col, int x, int y) {
  if (!col->front) return 0;
  int nx = dd_clampi(x / 32, 0, col->width - 1);
  int ny = dd_clampi(y / 32, 0, col->height - 1);
  const int index = ny * col->width + nx;
  if (col->front[index] == DD_TILE_DEATH || col->front[index] == DD_TILE_NOLASER) return col->front[index];
  return 0;
}

static int dd_col_get_front_collision_at(const dd_collision *col, float x, float y) {
  return dd_col_get_front_tile(col, dd_round_to_int(x), dd_round_to_int(y));
}

static int dd_col_get_tile_index(const dd_collision *col, int index) { return index < 0 ? 0 : col->tiles[index]; }
static int dd_col_get_front_tile_index(const dd_collision *col, int index) { return index < 0 || !col->front ? 0 : col->front[index]; }
static int dd_col_get_tile_flags(const dd_collision *col, int index) { return index < 0 ? 0 : col->tile_flags[index]; }
static int dd_col_get_front_tile_flags(const dd_collision *col, int index) { return index < 0 || !col->front ? 0 : col->front_flags[index]; }
static int dd_col_get_index(const dd_collision *col, int nx, int ny) { return col->tiles[ny * col->width + nx]; }
static int dd_col_get_front_index(const dd_collision *col, int nx, int ny) { return col->front ? col->front[ny * col->width + nx] : 0; }

static dd_vec2 dd_col_get_pos(const dd_collision *col, int index) {
  if (index < 0) return dd_v2(0, 0);
  int x = index % col->width;
  int y = index / col->width;
  return dd_v2((float)(x * 32 + 16), (float)(y * 32 + 16));
}

static int dd_col_is_no_laser(const dd_collision *col, int x, int y) { return dd_col_get_tile(col, x, y) == DD_TILE_NOLASER; }
static int dd_col_is_front_no_laser(const dd_collision *col, int x, int y) { return dd_col_get_front_tile(col, x, y) == DD_TILE_NOLASER; }
static int dd_col_is_wall_jump(const dd_collision *col, int index) { return index < 0 ? 0 : col->tiles[index] == DD_TILE_WALLJUMP; }

/* CCollision::Entity: the entity number of a tile, or 0 outside the map. */
static int dd_col_entity(const dd_collision *col, int x, int y, int layer) {
  if (x < 0 || x >= col->width || y < 0 || y >= col->height) return 0;
  const int index = y * col->width + x;
  switch (layer) {
  case DD_LAYER_GAME: return col->tiles[index] - DD_ENTITY_OFFSET;
  case DD_LAYER_FRONT: return col->front ? col->front[index] - DD_ENTITY_OFFSET : -DD_ENTITY_OFFSET;
  case DD_LAYER_SWITCH: return col->switch_type ? col->switch_type[index] - DD_ENTITY_OFFSET : -DD_ENTITY_OFFSET;
  case DD_LAYER_TELE: return col->tele_type ? col->tele_type[index] - DD_ENTITY_OFFSET : -DD_ENTITY_OFFSET;
  case DD_LAYER_SPEEDUP: return col->speedup_type ? col->speedup_type[index] - DD_ENTITY_OFFSET : -DD_ENTITY_OFFSET;
  case DD_LAYER_TUNE: return col->tune_type ? col->tune_type[index] - DD_ENTITY_OFFSET : -DD_ENTITY_OFFSET;
  default: return 0;
  }
}

/* ---- teleporters, speedups, switches, tune zones ---- */

static int dd_col_is_teleport(const dd_collision *col, int index) {
  if (index < 0 || !col->tele_type) return 0;
  if (col->tele_type[index] == DD_TILE_TELEIN) return col->tele_number[index];
  return 0;
}

static int dd_col_is_evil_teleport(const dd_collision *col, int index) {
  if (index < 0 || !col->tele_type) return 0;
  if (col->tele_type[index] == DD_TILE_TELEINEVIL) return col->tele_number[index];
  return 0;
}

static bool dd_col_is_check_teleport(const dd_collision *col, int index) {
  if (index < 0 || !col->tele_type) return false;
  return col->tele_type[index] == DD_TILE_TELECHECKIN;
}

static bool dd_col_is_check_evil_teleport(const dd_collision *col, int index) {
  if (index < 0 || !col->tele_type) return false;
  return col->tele_type[index] == DD_TILE_TELECHECKINEVIL;
}

static int dd_col_is_tele_checkpoint(const dd_collision *col, int index) {
  if (index < 0 || !col->tele_type) return 0;
  if (col->tele_type[index] == DD_TILE_TELECHECK) return col->tele_number[index];
  return 0;
}

static int dd_col_is_teleport_weapon(const dd_collision *col, int index) {
  if (index < 0 || !col->tele_type) return 0;
  if (col->tele_type[index] == DD_TILE_TELEINWEAPON) return col->tele_number[index];
  return 0;
}

static int dd_col_is_teleport_hook(const dd_collision *col, int index) {
  if (index < 0 || !col->tele_type) return 0;
  if (col->tele_type[index] == DD_TILE_TELEINHOOK) return col->tele_number[index];
  return 0;
}

static bool dd_col_is_speedup(const dd_collision *col, int index) { return col->speedup_force && col->speedup_force[index] > 0; }

static int dd_col_is_tune(const dd_collision *col, int index) {
  if (index < 0 || !col->tune_type) return 0;
  if (col->tune_type[index]) return col->tune_number[index];
  return 0;
}

static void dd_col_get_speedup(const dd_collision *col, int index, dd_vec2 *dir, int *force, int *max_speed, int *type) {
  if (index < 0 || !col->speedup_force) return;
  float angle = col->speedup_angle[index] * (dd_pi / 180.0f);
  *force = col->speedup_force[index];
  *type = col->speedup_type[index];
  *dir = dd_direction(angle);
  if (max_speed) *max_speed = col->speedup_max_speed[index];
}

static int dd_col_get_switch_type(const dd_collision *col, int index) {
  if (index < 0 || !col->switch_type) return 0;
  if (col->switch_type[index] > 0) return col->switch_type[index];
  return 0;
}

static int dd_col_get_switch_number(const dd_collision *col, int index) {
  if (index < 0 || !col->switch_type) return 0;
  if (col->switch_type[index] > 0 && col->switch_number[index] > 0 && col->switch_number[index] <= col->highest_switch_number)
    return col->switch_number[index];
  return 0;
}

static int dd_col_get_switch_delay(const dd_collision *col, int index) {
  if (index < 0 || !col->switch_type) return 0;
  if (col->switch_type[index] > 0) return col->switch_delay[index];
  return 0;
}

static int dd_col_is_time_checkpoint(const dd_collision *col, int index) {
  if (index < 0) return -1;
  int z = col->tiles[index];
  if (z >= DD_TILE_TIME_CHECKPOINT_FIRST && z <= DD_TILE_TIME_CHECKPOINT_LAST) return z - DD_TILE_TIME_CHECKPOINT_FIRST;
  return -1;
}

static int dd_col_is_front_time_checkpoint(const dd_collision *col, int index) {
  if (index < 0 || !col->front) return -1;
  int z = col->front[index];
  if (z >= DD_TILE_TIME_CHECKPOINT_FIRST && z <= DD_TILE_TIME_CHECKPOINT_LAST) return z - DD_TILE_TIME_CHECKPOINT_FIRST;
  return -1;
}

static int dd_col_mover_speed(const dd_collision *col, int x, int y, dd_vec2 *speed) {
  int nx = dd_clampi(x / 32, 0, col->width - 1);
  int ny = dd_clampi(y / 32, 0, col->height - 1);
  int index = col->tiles[ny * col->width + nx];
  if (index != DD_TILE_CP && index != DD_TILE_CP_F) return 0;
  dd_vec2 target;
  switch (col->tile_flags[ny * col->width + nx]) {
  case DD_ROTATION_0: target = dd_v2(0.0f, -4.0f); break;
  case DD_ROTATION_90: target = dd_v2(4.0f, 0.0f); break;
  case DD_ROTATION_180: target = dd_v2(0.0f, 4.0f); break;
  case DD_ROTATION_270: target = dd_v2(-4.0f, 0.0f); break;
  default: target = dd_v2(0.0f, 0.0f); break;
  }
  if (index == DD_TILE_CP_F) target = dd_v2_mul(target, 4.0f);
  *speed = target;
  return index;
}

static bool dd_col_has_hook_tele_ins(const dd_collision *col) {
  if (col->has_hook_tele_ins) return true;
  if (!col->old_teleport_hook) return false;
  for (int i = 0; i < 256; ++i)
    if (col->tele_ins[i].count) return true;
  return false;
}

/* ---- doors ---- */

static dd_door_tile dd_col_get_door_tile(const dd_collision *col, int index) {
  dd_door_tile none = {0, 0, 0};
  if (!col->doors || index < 0 || !col->doors[index].index) return none;
  return col->doors[index];
}

static void dd_col_door_changed(dd_collision *col, int index);

static void dd_col_set_door_collision_at(dd_collision *col, float x, float y, unsigned char type, unsigned char flags, unsigned char number) {
  if (!col->doors) return;
  int nx = dd_clampi(dd_round_to_int(x) / 32, 0, col->width - 1);
  int ny = dd_clampi(dd_round_to_int(y) / 32, 0, col->height - 1);
  col->doors[ny * col->width + nx].index = type;
  col->doors[ny * col->width + nx].flags = flags;
  col->doors[ny * col->width + nx].number = number;
  dd_col_door_changed(col, ny * col->width + nx);
}

/* ---- move restrictions (stoppers and doors) ---- */

enum { DD_MR_DIR_HERE = 0, DD_MR_DIR_RIGHT, DD_MR_DIR_DOWN, DD_MR_DIR_LEFT, DD_MR_DIR_UP, DD_NUM_MR_DIRS };

static int dd_move_restrictions_raw(int direction, int tile, int flags) {
  (void)direction;
  flags = flags & (DD_TILEFLAG_XFLIP | DD_TILEFLAG_YFLIP | DD_TILEFLAG_ROTATE);
  switch (tile) {
  case DD_TILE_STOP:
    switch (flags) {
    case DD_ROTATION_0: return DD_CANTMOVE_DOWN;
    case DD_ROTATION_90: return DD_CANTMOVE_LEFT;
    case DD_ROTATION_180: return DD_CANTMOVE_UP;
    case DD_ROTATION_270: return DD_CANTMOVE_RIGHT;
    case DD_TILEFLAG_YFLIP ^ DD_ROTATION_0: return DD_CANTMOVE_UP;
    case DD_TILEFLAG_YFLIP ^ DD_ROTATION_90: return DD_CANTMOVE_RIGHT;
    case DD_TILEFLAG_YFLIP ^ DD_ROTATION_180: return DD_CANTMOVE_DOWN;
    case DD_TILEFLAG_YFLIP ^ DD_ROTATION_270: return DD_CANTMOVE_LEFT;
    }
    break;
  case DD_TILE_STOPS:
    switch (flags) {
    case DD_ROTATION_0:
    case DD_ROTATION_180:
    case DD_TILEFLAG_YFLIP ^ DD_ROTATION_0:
    case DD_TILEFLAG_YFLIP ^ DD_ROTATION_180: return DD_CANTMOVE_DOWN | DD_CANTMOVE_UP;
    case DD_ROTATION_90:
    case DD_ROTATION_270:
    case DD_TILEFLAG_YFLIP ^ DD_ROTATION_90:
    case DD_TILEFLAG_YFLIP ^ DD_ROTATION_270: return DD_CANTMOVE_LEFT | DD_CANTMOVE_RIGHT;
    }
    break;
  case DD_TILE_STOPA: return DD_CANTMOVE_LEFT | DD_CANTMOVE_RIGHT | DD_CANTMOVE_UP | DD_CANTMOVE_DOWN;
  }
  return 0;
}

static int dd_move_restrictions_mask(int direction) {
  switch (direction) {
  case DD_MR_DIR_RIGHT: return DD_CANTMOVE_RIGHT;
  case DD_MR_DIR_DOWN: return DD_CANTMOVE_DOWN;
  case DD_MR_DIR_LEFT: return DD_CANTMOVE_LEFT;
  case DD_MR_DIR_UP: return DD_CANTMOVE_UP;
  default: return 0;
  }
}

static int dd_move_restrictions_tile(int direction, int tile, int flags) {
  int result = dd_move_restrictions_raw(direction, tile, flags);
  /* Stoppers only block moving onto them, except one-way blockers, which also
   * block while standing on them. */
  if (direction == DD_MR_DIR_HERE && tile == DD_TILE_STOP) return result;
  return result & dd_move_restrictions_mask(direction);
}

/* CCollision::GetMoveRestrictions; distance defaults to 18 in DDNet, and
 * override_center_tile_index to -1. */
static int dd_col_get_move_restrictions(const dd_collision *col, dd_switch_active_fn switch_active, void *user, dd_vec2 pos, float distance,
                                        int override_center_tile_index) {
  static const dd_vec2 directions[DD_NUM_MR_DIRS] = {{0, 0}, {1, 0}, {0, 1}, {-1, 0}, {0, -1}};
  int restrictions = 0;
  for (int d = 0; d < DD_NUM_MR_DIRS; d++) {
    dd_vec2 mod_pos = dd_v2_add(pos, dd_v2_mul(directions[d], distance));
    int mod_map_index = dd_col_get_pure_map_index(col, mod_pos.x, mod_pos.y);
    if (d == DD_MR_DIR_HERE && override_center_tile_index >= 0) mod_map_index = override_center_tile_index;
    if (col->mr_static) {
      if (mod_map_index >= 0) restrictions |= col->mr_static[mod_map_index * DD_NUM_MR_DIRS + d];
    } else {
      for (int front = 0; front < 2; front++) {
      int tile, flags;
      if (!front) {
        tile = dd_col_get_tile_index(col, mod_map_index);
        flags = dd_col_get_tile_flags(col, mod_map_index);
      } else {
        tile = dd_col_get_front_tile_index(col, mod_map_index);
        flags = dd_col_get_front_tile_flags(col, mod_map_index);
      }
      restrictions |= dd_move_restrictions_tile(d, tile, flags);
      }
    }
    if (switch_active) {
      dd_door_tile door = dd_col_get_door_tile(col, mod_map_index);
      if ((int)door.number <= col->highest_switch_number && switch_active(door.number, user))
        restrictions |= dd_move_restrictions_tile(d, door.index, door.flags);
    }
  }
  return restrictions;
}

/* ---- tiles crossed during a move (anti-skip) ---- */

static bool dd_col_tile_exists_next(const dd_collision *col, int index) {
  if (index < 0) return false;
  const int size = col->width * col->height;
  int left = (index - 1 > 0) ? index - 1 : index;
  int right = (index + 1 < size) ? index + 1 : index;
  int below = (index + col->width < size) ? index + col->width : index;
  int above = (index - col->width > 0) ? index - col->width : index;
  const uint8_t *t = col->tiles, *tf = col->tile_flags;

  if ((t[right] == DD_TILE_STOP && tf[right] == DD_ROTATION_270) || (t[left] == DD_TILE_STOP && tf[left] == DD_ROTATION_90)) return true;
  if ((t[below] == DD_TILE_STOP && tf[below] == DD_ROTATION_0) || (t[above] == DD_TILE_STOP && tf[above] == DD_ROTATION_180)) return true;
  if (t[right] == DD_TILE_STOPA || t[left] == DD_TILE_STOPA || ((t[right] == DD_TILE_STOPS || t[left] == DD_TILE_STOPS))) return true;
  /* DDNet's "Flags | ROTATION_180 | ROTATION_0" is always true; kept as is. */
  if (t[below] == DD_TILE_STOPA || t[above] == DD_TILE_STOPA || ((t[below] == DD_TILE_STOPS || t[above] == DD_TILE_STOPS) && (tf[below] | DD_ROTATION_180 | DD_ROTATION_0)))
    return true;
  if (col->front) {
    const uint8_t *f = col->front, *ff = col->front_flags;
    if (f[right] == DD_TILE_STOPA || f[left] == DD_TILE_STOPA || ((f[right] == DD_TILE_STOPS || f[left] == DD_TILE_STOPS))) return true;
    if (f[below] == DD_TILE_STOPA || f[above] == DD_TILE_STOPA || ((f[below] == DD_TILE_STOPS || f[above] == DD_TILE_STOPS) && (ff[below] | DD_ROTATION_180 | DD_ROTATION_0)))
      return true;
    if ((f[right] == DD_TILE_STOP && ff[right] == DD_ROTATION_270) || (f[left] == DD_TILE_STOP && ff[left] == DD_ROTATION_90)) return true;
    if ((f[below] == DD_TILE_STOP && ff[below] == DD_ROTATION_0) || (f[above] == DD_TILE_STOP && ff[above] == DD_ROTATION_180)) return true;
  }
  if (col->doors) {
    const dd_door_tile *d = col->doors;
    if (d[right].index == DD_TILE_STOPA || d[left].index == DD_TILE_STOPA || ((d[right].index == DD_TILE_STOPS || d[left].index == DD_TILE_STOPS)))
      return true;
    if (d[below].index == DD_TILE_STOPA || d[above].index == DD_TILE_STOPA ||
        ((d[below].index == DD_TILE_STOPS || d[above].index == DD_TILE_STOPS) && (d[below].flags | DD_ROTATION_180 | DD_ROTATION_0)))
      return true;
    if ((d[right].index == DD_TILE_STOP && d[right].flags == DD_ROTATION_270) || (d[left].index == DD_TILE_STOP && d[left].flags == DD_ROTATION_90))
      return true;
    if ((d[below].index == DD_TILE_STOP && d[below].flags == DD_ROTATION_0) || (d[above].index == DD_TILE_STOP && d[above].flags == DD_ROTATION_180))
      return true;
  }
  return false;
}

static bool dd_col_tile_exists_uncached(const dd_collision *col, int index) {
  if (index < 0) return false;
  const int t = col->tiles[index];
  if ((t >= DD_TILE_FREEZE && t <= DD_TILE_TELE_LASER_DISABLE) || (t >= DD_TILE_LFREEZE && t <= DD_TILE_LUNFREEZE)) return true;
  if (col->front) {
    const int f = col->front[index];
    if ((f >= DD_TILE_FREEZE && f <= DD_TILE_TELE_LASER_DISABLE) || (f >= DD_TILE_LFREEZE && f <= DD_TILE_LUNFREEZE)) return true;
  }
  if (col->tele_type) {
    const int tt = col->tele_type[index];
    if (tt == DD_TILE_TELEIN || tt == DD_TILE_TELEINEVIL || tt == DD_TILE_TELECHECKINEVIL || tt == DD_TILE_TELECHECK || tt == DD_TILE_TELECHECKIN)
      return true;
  }
  if (col->speedup_force && col->speedup_force[index] > 0) return true;
  if (col->doors && col->doors[index].index) return true;
  if (col->switch_type && col->switch_type[index]) return true;
  if (col->tune_type && col->tune_type[index]) return true;
  return dd_col_tile_exists_next(col, index);
}

static bool dd_col_tile_exists(const dd_collision *col, int index) {
  if (index < 0) return false;
  return col->exists ? col->exists[index] : dd_col_tile_exists_uncached(col, index);
}

static void dd_col_build_tables(dd_collision *col) {
  const size_t size = (size_t)col->width * (size_t)col->height;
  col->exists = (uint8_t *)malloc(size);
  col->mr_static = (uint8_t *)malloc(size * DD_NUM_MR_DIRS);
  col->hook_stop = (uint8_t *)malloc(size);
  if (!col->exists || !col->mr_static || !col->hook_stop) {
    free(col->exists);
    free(col->mr_static);
    free(col->hook_stop);
    col->exists = col->mr_static = col->hook_stop = NULL;
    return;
  }
  for (int i = 0; i < (int)size; ++i) {
    /* solid, a hook teleporter of either kind, or a hook blocker */
    const int t = col->tiles[i], f = col->front ? col->front[i] : 0;
    const int tele = col->tele_type && col->tele_number[i] ? col->tele_type[i] : 0;
    col->hook_stop[i] = t == DD_TILE_SOLID || t == DD_TILE_NOHOOK || tele == DD_TILE_TELEIN || tele == DD_TILE_TELEINHOOK ||
                        t == DD_TILE_THROUGH_ALL || t == DD_TILE_THROUGH_DIR || f == DD_TILE_THROUGH_ALL || f == DD_TILE_THROUGH_DIR;
    col->exists[i] = dd_col_tile_exists_uncached(col, i);
    for (int d = 0; d < DD_NUM_MR_DIRS; ++d)
      col->mr_static[i * DD_NUM_MR_DIRS + d] = (uint8_t)(dd_move_restrictions_tile(d, dd_col_get_tile_index(col, i), dd_col_get_tile_flags(col, i)) |
                                                         dd_move_restrictions_tile(d, dd_col_get_front_tile_index(col, i), dd_col_get_front_tile_flags(col, i)));
  }
}

/* A door tile changed: whether it and its neighbours exist may have too. */
static void dd_col_door_changed(dd_collision *col, int index) {
  if (!col->exists) return;
  const int size = col->width * col->height;
  const int around[5] = {index, index - 1, index + 1, index - col->width, index + col->width};
  for (int k = 0; k < 5; ++k)
    if (around[k] >= 0 && around[k] < size) col->exists[around[k]] = dd_col_tile_exists_uncached(col, around[k]);
}

static int dd_col_get_map_index(const dd_collision *col, dd_vec2 pos) {
  int nx = dd_clampi((int)pos.x / 32, 0, col->width - 1);
  int ny = dd_clampi((int)pos.y / 32, 0, col->height - 1);
  int index = ny * col->width + nx;
  return dd_col_tile_exists(col, index) ? index : -1;
}

/* CCollision::GetMapIndices into a caller buffer; returns the count. DDNet's
 * vector has no limit, but a tick's path cannot cross more than
 * DD_MAX_MAP_INDICES distinct tiles at the 6000 velocity cap. */
#define DD_MAX_MAP_INDICES 1024
static int dd_col_get_map_indices(const dd_collision *col, dd_vec2 prev_pos, dd_vec2 pos, int *out) {
  int count = 0;
  float d = dd_v2_distance(prev_pos, pos);
  int end = (int)(d + 1);
  if (d == 0.0f) {
    int nx = dd_clampi((int)pos.x / 32, 0, col->width - 1);
    int ny = dd_clampi((int)pos.y / 32, 0, col->height - 1);
    int index = ny * col->width + nx;
    if (dd_col_tile_exists(col, index)) out[count++] = index;
    return count;
  }
  int last_index = 0;
  for (int i = 0; i < end; i++) {
    float a = i / d;
    dd_vec2 tmp = dd_v2_mix(prev_pos, pos, a);
    int nx = dd_clampi((int)tmp.x / 32, 0, col->width - 1);
    int ny = dd_clampi((int)tmp.y / 32, 0, col->height - 1);
    int index = ny * col->width + nx;
    if (dd_col_tile_exists(col, index) && last_index != index) {
      if (count >= DD_MAX_MAP_INDICES) return count;
      out[count++] = index;
      last_index = index;
    }
  }
  return count;
}

/* ---- line and box tests ---- */

static void dd_through_offset(dd_vec2 pos0, dd_vec2 pos1, int *offset_x, int *offset_y) {
  float x = pos0.x - pos1.x;
  float y = pos0.y - pos1.y;
  if (dd_absf(x) > dd_absf(y)) {
    *offset_x = x < 0 ? -32 : 32;
    *offset_y = 0;
  } else {
    *offset_x = 0;
    *offset_y = y < 0 ? -32 : 32;
  }
}

static bool dd_col_is_through(const dd_collision *col, int x, int y, int offset_x, int offset_y, dd_vec2 pos0, dd_vec2 pos1) {
  const int index = dd_col_get_pure_map_index(col, (float)x, (float)y);
  if (col->front && (col->front[index] == DD_TILE_THROUGH_ALL || col->front[index] == DD_TILE_THROUGH_CUT)) return true;
  if (col->front && col->front[index] == DD_TILE_THROUGH_DIR &&
      ((col->front_flags[index] == DD_ROTATION_0 && pos0.y > pos1.y) || (col->front_flags[index] == DD_ROTATION_90 && pos0.x < pos1.x) ||
       (col->front_flags[index] == DD_ROTATION_180 && pos0.y < pos1.y) || (col->front_flags[index] == DD_ROTATION_270 && pos0.x > pos1.x)))
    return true;
  const int offset_index = dd_col_get_pure_map_index(col, (float)(x + offset_x), (float)(y + offset_y));
  return col->tiles[offset_index] == DD_TILE_THROUGH || (col->front && col->front[offset_index] == DD_TILE_THROUGH);
}

static bool dd_col_is_hook_blocker(const dd_collision *col, int x, int y, dd_vec2 pos0, dd_vec2 pos1) {
  const int index = dd_col_get_pure_map_index(col, (float)x, (float)y);
  if (col->tiles[index] == DD_TILE_THROUGH_ALL || (col->front && col->front[index] == DD_TILE_THROUGH_ALL)) return true;
  if (col->tiles[index] == DD_TILE_THROUGH_DIR &&
      ((col->tile_flags[index] == DD_ROTATION_0 && pos0.y < pos1.y) || (col->tile_flags[index] == DD_ROTATION_90 && pos0.x > pos1.x) ||
       (col->tile_flags[index] == DD_ROTATION_180 && pos0.y > pos1.y) || (col->tile_flags[index] == DD_ROTATION_270 && pos0.x < pos1.x)))
    return true;
  if (col->front && col->front[index] == DD_TILE_THROUGH_DIR &&
      ((col->front_flags[index] == DD_ROTATION_0 && pos0.y < pos1.y) || (col->front_flags[index] == DD_ROTATION_90 && pos0.x > pos1.x) ||
       (col->front_flags[index] == DD_ROTATION_180 && pos0.y > pos1.y) || (col->front_flags[index] == DD_ROTATION_270 && pos0.x < pos1.x)))
    return true;
  return false;
}

static int dd_col_intersect_line(const dd_collision *col, dd_vec2 pos0, dd_vec2 pos1, dd_vec2 *out_collision, dd_vec2 *out_before) {
  float distance = dd_v2_distance(pos0, pos1);
  int end = (int)(distance + 1);
  dd_vec2 last = pos0;
  for (int i = 0; i <= end; i++) {
    float a = i / (float)end;
    dd_vec2 pos = dd_v2_mix(pos0, pos1, a);
    int ix = dd_round_to_int(pos.x);
    int iy = dd_round_to_int(pos.y);
    if (dd_col_check_point(col, (float)ix, (float)iy)) {
      if (out_collision) *out_collision = pos;
      if (out_before) *out_before = last;
      return dd_col_get_collision_at(col, (float)ix, (float)iy);
    }
    last = pos;
  }
  if (out_collision) *out_collision = pos1;
  if (out_before) *out_before = pos1;
  return 0;
}

/* True when no tile the hook line from pos0 to pos1 passes can stop it. */
static bool dd_col_hook_line_clear(const dd_collision *col, dd_vec2 pos0, dd_vec2 pos1) {
  if (!col->hook_stop) return false;
  const float slack = 1.0f;
  const float x0 = fminf(pos0.x, pos1.x) - slack, x1 = fmaxf(pos0.x, pos1.x) + slack;
  const float y0 = fminf(pos0.y, pos1.y) - slack, y1 = fmaxf(pos0.y, pos1.y) + slack;
  if (!(x0 > -1e8f && y0 > -1e8f && x1 < 1e8f && y1 < 1e8f)) return false; /* also NaN */
  const int tx0 = dd_clampi(dd_round_to_int(x0) / 32, 0, col->width - 1), tx1 = dd_clampi(dd_round_to_int(x1) / 32, 0, col->width - 1);
  const int ty0 = dd_clampi(dd_round_to_int(y0) / 32, 0, col->height - 1), ty1 = dd_clampi(dd_round_to_int(y1) / 32, 0, col->height - 1);
  if ((tx1 - tx0 + 1) * (ty1 - ty0 + 1) > 64) return false;
  for (int ty = ty0; ty <= ty1; ++ty)
    for (int tx = tx0; tx <= tx1; ++tx)
      if (col->hook_stop[ty * col->width + tx]) return false;
  return true;
}

static int dd_col_intersect_line_tele_hook(const dd_collision *col, dd_vec2 pos0, dd_vec2 pos1, dd_vec2 *out_collision, dd_vec2 *out_before,
                                    int *tele_nr) {
  if (dd_col_hook_line_clear(col, pos0, pos1)) {
    if (tele_nr) *tele_nr = 0;
    if (out_collision) *out_collision = pos1;
    if (out_before) *out_before = pos1;
    return 0;
  }
  float distance = dd_v2_distance(pos0, pos1);
  int end = (int)(distance + 1);
  dd_vec2 last = pos0;
  int dx = 0, dy = 0;
  dd_through_offset(pos0, pos1, &dx, &dy);
  for (int i = 0; i <= end; i++) {
    float a = i / (float)end;
    dd_vec2 pos = dd_v2_mix(pos0, pos1, a);
    int ix = dd_round_to_int(pos.x);
    int iy = dd_round_to_int(pos.y);

    int index = dd_col_get_pure_map_index(col, pos.x, pos.y);
    if (tele_nr) *tele_nr = col->old_teleport_hook ? dd_col_is_teleport(col, index) : dd_col_is_teleport_hook(col, index);
    if (tele_nr && *tele_nr) {
      if (out_collision) *out_collision = pos;
      if (out_before) *out_before = last;
      return DD_TILE_TELEINHOOK;
    }

    int hit = 0;
    if (dd_col_check_point(col, (float)ix, (float)iy)) {
      if (!dd_col_is_through(col, ix, iy, dx, dy, pos0, pos1)) hit = dd_col_get_collision_at(col, (float)ix, (float)iy);
    } else if (dd_col_is_hook_blocker(col, ix, iy, pos0, pos1)) {
      hit = DD_TILE_NOHOOK;
    }
    if (hit) {
      if (out_collision) *out_collision = pos;
      if (out_before) *out_before = last;
      return hit;
    }
    last = pos;
  }
  if (out_collision) *out_collision = pos1;
  if (out_before) *out_before = pos1;
  return 0;
}

static int dd_col_intersect_line_tele_weapon(const dd_collision *col, dd_vec2 pos0, dd_vec2 pos1, dd_vec2 *out_collision, dd_vec2 *out_before,
                                             int *tele_nr) {
  float distance = dd_v2_distance(pos0, pos1);
  int end = (int)(distance + 1);
  dd_vec2 last = pos0;
  for (int i = 0; i <= end; i++) {
    float a = i / (float)end;
    dd_vec2 pos = dd_v2_mix(pos0, pos1, a);
    int ix = dd_round_to_int(pos.x);
    int iy = dd_round_to_int(pos.y);

    int index = dd_col_get_pure_map_index(col, pos.x, pos.y);
    if (tele_nr) *tele_nr = col->old_teleport_weapons ? dd_col_is_teleport(col, index) : dd_col_is_teleport_weapon(col, index);
    if (tele_nr && *tele_nr) {
      if (out_collision) *out_collision = pos;
      if (out_before) *out_before = last;
      return DD_TILE_TELEINWEAPON;
    }

    if (dd_col_check_point(col, (float)ix, (float)iy)) {
      if (out_collision) *out_collision = pos;
      if (out_before) *out_before = last;
      return dd_col_get_collision_at(col, (float)ix, (float)iy);
    }
    last = pos;
  }
  if (out_collision) *out_collision = pos1;
  if (out_before) *out_before = pos1;
  return 0;
}

static int dd_col_intersect_no_laser(const dd_collision *col, dd_vec2 pos0, dd_vec2 pos1, dd_vec2 *out_collision, dd_vec2 *out_before) {
  float distance = dd_v2_distance(pos0, pos1);
  dd_vec2 last = pos0;
  const int distance_rounded = (int)ceilf(distance);
  for (int i = 0; i < distance_rounded; i++) {
    float a = i / distance;
    dd_vec2 pos = dd_v2_mix(pos0, pos1, a);
    int nx = dd_clampi(dd_round_to_int(pos.x) / 32, 0, col->width - 1);
    int ny = dd_clampi(dd_round_to_int(pos.y) / 32, 0, col->height - 1);
    if (dd_col_get_index(col, nx, ny) == DD_TILE_SOLID || dd_col_get_index(col, nx, ny) == DD_TILE_NOHOOK ||
        dd_col_get_index(col, nx, ny) == DD_TILE_NOLASER || dd_col_get_front_index(col, nx, ny) == DD_TILE_NOLASER) {
      if (out_collision) *out_collision = pos;
      if (out_before) *out_before = last;
      if (dd_col_get_front_index(col, nx, ny) == DD_TILE_NOLASER) return dd_col_get_front_collision_at(col, pos.x, pos.y);
      return dd_col_get_collision_at(col, pos.x, pos.y);
    }
    last = pos;
  }
  if (out_collision) *out_collision = pos1;
  if (out_before) *out_before = pos1;
  return 0;
}

static int dd_col_intersect_no_laser_no_walls(const dd_collision *col, dd_vec2 pos0, dd_vec2 pos1, dd_vec2 *out_collision, dd_vec2 *out_before) {
  float distance = dd_v2_distance(pos0, pos1);
  dd_vec2 last = pos0;
  const int distance_rounded = (int)ceilf(distance);
  for (int i = 0; i < distance_rounded; i++) {
    float a = (float)i / distance;
    dd_vec2 pos = dd_v2_mix(pos0, pos1, a);
    const int ix = dd_round_to_int(pos.x), iy = dd_round_to_int(pos.y);
    if (dd_col_is_no_laser(col, ix, iy) || dd_col_is_front_no_laser(col, ix, iy)) {
      if (out_collision) *out_collision = pos;
      if (out_before) *out_before = last;
      if (dd_col_is_no_laser(col, ix, iy)) return dd_col_get_collision_at(col, pos.x, pos.y);
      return dd_col_get_front_collision_at(col, pos.x, pos.y);
    }
    last = pos;
  }
  if (out_collision) *out_collision = pos1;
  if (out_before) *out_before = pos1;
  return 0;
}

static int dd_col_intersect_air(const dd_collision *col, dd_vec2 pos0, dd_vec2 pos1, dd_vec2 *out_collision, dd_vec2 *out_before) {
  float distance = dd_v2_distance(pos0, pos1);
  dd_vec2 last = pos0;
  const int distance_rounded = (int)ceilf(distance);
  for (int i = 0; i < distance_rounded; i++) {
    float a = (float)i / distance;
    dd_vec2 pos = dd_v2_mix(pos0, pos1, a);
    const int ix = dd_round_to_int(pos.x), iy = dd_round_to_int(pos.y);
    if (dd_col_is_solid(col, ix, iy) || (!dd_col_get_tile(col, ix, iy) && !dd_col_get_front_tile(col, ix, iy))) {
      if (out_collision) *out_collision = pos;
      if (out_before) *out_before = last;
      if (!dd_col_get_tile(col, ix, iy) && !dd_col_get_front_tile(col, ix, iy)) return -1;
      else if (!dd_col_get_tile(col, ix, iy)) return dd_col_get_tile(col, ix, iy);
      else return dd_col_get_front_tile(col, ix, iy);
    }
    last = pos;
  }
  if (out_collision) *out_collision = pos1;
  if (out_before) *out_before = pos1;
  return 0;
}

static void dd_col_move_point(const dd_collision *col, dd_vec2 *inout_pos, dd_vec2 *inout_vel, float elasticity, int *bounces) {
  if (bounces) *bounces = 0;
  dd_vec2 pos = *inout_pos;
  dd_vec2 vel = *inout_vel;
  if (dd_col_check_point(col, pos.x + vel.x, pos.y + vel.y)) {
    int affected = 0;
    if (dd_col_check_point(col, pos.x + vel.x, pos.y)) {
      inout_vel->x *= -elasticity;
      if (bounces) (*bounces)++;
      affected++;
    }
    if (dd_col_check_point(col, pos.x, pos.y + vel.y)) {
      inout_vel->y *= -elasticity;
      if (bounces) (*bounces)++;
      affected++;
    }
    if (affected == 0) {
      inout_vel->x *= -elasticity;
      inout_vel->y *= -elasticity;
    }
  } else {
    *inout_pos = dd_v2_add(pos, vel);
  }
}

static bool dd_col_test_box(const dd_collision *col, dd_vec2 pos, dd_vec2 size) {
  size = dd_v2_mul(size, 0.5f);
  if (dd_col_check_point(col, pos.x - size.x, pos.y - size.y)) return true;
  if (dd_col_check_point(col, pos.x + size.x, pos.y - size.y)) return true;
  if (dd_col_check_point(col, pos.x - size.x, pos.y + size.y)) return true;
  if (dd_col_check_point(col, pos.x + size.x, pos.y + size.y)) return true;
  return false;
}

static bool dd_col_is_on_ground(const dd_collision *col, dd_vec2 pos, float size) {
  if (dd_col_check_point(col, pos.x + size / 2, pos.y + size / 2 + 5)) return true;
  if (dd_col_check_point(col, pos.x - size / 2, pos.y + size / 2 + 5)) return true;
  return false;
}

/* True when no tile that dd_col_check_point could look at for a point in
 * [x0, x1] x [y0, y1] is solid. Conservative: false for large areas. */
static bool dd_col_area_free(const dd_collision *col, float x0, float y0, float x1, float y1) {
  if (!col->tiles) return true;
  if (!(x0 > -1e8f && y0 > -1e8f && x1 < 1e8f && y1 < 1e8f)) return false; /* also NaN */
  const int tx0 = dd_clampi(dd_round_to_int(x0) / 32, 0, col->width - 1), tx1 = dd_clampi(dd_round_to_int(x1) / 32, 0, col->width - 1);
  const int ty0 = dd_clampi(dd_round_to_int(y0) / 32, 0, col->height - 1), ty1 = dd_clampi(dd_round_to_int(y1) / 32, 0, col->height - 1);
  if ((tx1 - tx0 + 1) * (ty1 - ty0 + 1) > 64) return false;
  for (int ty = ty0; ty <= ty1; ++ty)
    for (int tx = tx0; tx <= tx1; ++tx) {
      const int t = col->tiles[ty * col->width + tx];
      if (t == DD_TILE_SOLID || t == DD_TILE_NOHOOK) return false;
    }
  return true;
}

static void dd_col_move_box(const dd_collision *col, dd_vec2 *inout_pos, dd_vec2 *inout_vel, dd_vec2 size, dd_vec2 elasticity, bool *grounded) {
  dd_vec2 pos = *inout_pos;
  dd_vec2 vel = *inout_vel;

  float distance = dd_v2_length(vel);
  int max = (int)distance;

  if (distance > 0.00001f) {
    float fraction = 1.0f / (float)(max + 1);
    float elasticity_x = dd_clampf(elasticity.x, -1.0f, 1.0f);
    float elasticity_y = dd_clampf(elasticity.y, -1.0f, 1.0f);

    /* Every box tested below lies around the straight path from pos to
     * pos + vel, give or take the rounding of the steps. With nothing solid
     * there, no test can hit and the steps just add up. */
    const float slack = 1.0f + (float)(max + 1) * (fabsf(pos.x) + fabsf(pos.y) + distance) * 2.5e-7f;
    const float half_x = size.x * 0.5f + slack, half_y = size.y * 0.5f + slack;
    const bool clear = dd_col_area_free(col, fminf(pos.x, pos.x + vel.x) - half_x, fminf(pos.y, pos.y + vel.y) - half_y,
                                        fmaxf(pos.x, pos.x + vel.x) + half_x, fmaxf(pos.y, pos.y + vel.y) + half_y);

    for (int i = 0; i <= max; i++) {
      if (dd_v2_eq(vel, dd_v2(0, 0))) break;

      dd_vec2 new_pos = dd_v2_add(pos, dd_v2_mul(vel, fraction));
      if (dd_v2_eq(new_pos, pos)) break;

      if (!clear && dd_col_test_box(col, dd_v2(new_pos.x, new_pos.y), size)) {
        int hits = 0;

        if (dd_col_test_box(col, dd_v2(pos.x, new_pos.y), size)) {
          if (grounded && elasticity_y > 0 && vel.y > 0) *grounded = true;
          new_pos.y = pos.y;
          vel.y *= -elasticity_y;
          hits++;
        }

        if (dd_col_test_box(col, dd_v2(new_pos.x, pos.y), size)) {
          new_pos.x = pos.x;
          vel.x *= -elasticity_x;
          hits++;
        }

        /* neither of the tests got a collision: a real corner case */
        if (hits == 0) {
          if (grounded && elasticity_y > 0 && vel.y > 0) *grounded = true;
          new_pos.y = pos.y;
          vel.y *= -elasticity_y;
          new_pos.x = pos.x;
          vel.x *= -elasticity_x;
        }
      }
      pos = new_pos;
    }
  }

  *inout_pos = pos;
  *inout_vel = vel;
}

#endif /* DD_COLLISION_INTERNAL_H */
