/*
 * Reconstruction coverage: how many ticks of each character a demo lets us
 * rebuild exactly, and how many need simulation.
 *
 * Between two snapshots A (tick Ta) and B (tick Tb) of a character, B carries
 * the server's core at its last re-sync R (Ta < R <= Tb):
 *   - ticks R..Tb are exact: the server did not re-sync in between, so its
 *     input-free prediction from R is the real state.
 *   - ticks Ta+1..R-1 are exact only if R was the only re-sync in the gap.
 *     That is checked by evolving A to R-1 and stepping one tick with input:
 *     B tells the direction and the aim, leaving jump and hook to try. If one
 *     of those lands exactly on B's core, the gap is "verified". If none does,
 *     something the core cannot see happened (tiles, other players, freeze,
 *     tuning, several re-syncs) and the gap needs simulation.
 * Ticks where the character is absent, spawns, or the server sent an
 * unreckoned core are "unknown".
 *
 * Usage: test_coverage <demo> [<demo> ...]
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DDNET_DEMO_IMPLEMENTATION
#include "ddnet_demo.h"
#include "internal/dd_physics.h"

#define MAX_GAP_BUCKET 16

typedef struct {
  long exact;               /* ticks R..Tb, and gaps that needed no re-sync guess */
  long verified;            /* ticks in gaps confirmed by a one-tick input step */
  long unverified;          /* ticks in gaps no input explains */
  long unknown;             /* absent, spawned, unreckoned */
  long gaps_verified, gaps_unverified;
  long unverified_by_len[MAX_GAP_BUCKET + 1]; /* gap length histogram, last = longer */
  long cause[5];                              /* unverified gaps by likely cause, see cause_names */
} coverage_t;

/* Likely cause of an unverified gap, first match wins. */
enum { CAUSE_PLAYERS, CAUSE_FROZEN, CAUSE_WEAPON, CAUSE_TILES, CAUSE_OTHER, NUM_CAUSES };
static const char *const cause_names[] = {"another tee close or hooked", "frozen", "fired a weapon", "special tile nearby", "other"};

/* True when a tile the server handles outside the core (anything but air,
 * solid and unhookable in the game/front layers, or any tele, speedup, switch
 * or tune tile) lies within a tile of the tee. */
static bool special_tile_near(const map_data_t *map, int x, int y) {
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      const int tx = x / 32 + dx, ty = y / 32 + dy;
      if (tx < 0 || ty < 0 || tx >= map->width || ty >= map->height) continue;
      const int i = ty * map->width + tx;
      const int game = map->game_layer.data[i];
      if (game != 0 && game != 1 && game != 3) return true;
      if (map->front_layer.data && map->front_layer.data[i] != 0 && map->front_layer.data[i] != 1 && map->front_layer.data[i] != 3) return true;
      if (map->tele_layer.type && map->tele_layer.type[i]) return true;
      if (map->speedup_layer.type && map->speedup_layer.force && map->speedup_layer.force[i]) return true;
      if (map->switch_layer.type && map->switch_layer.type[i]) return true;
      if (map->tune_layer.number && map->tune_layer.number[i]) return true;
    }
  }
  return false;
}

typedef struct {
  bool present;
  int snap_tick;
  dd_netobj_character character;
} last_snap_t;

static bool core_equal(const dd_netobj_character_core *a, const dd_netobj_character_core *b) {
  return memcmp((const int *)a + 1, (const int *)b + 1, sizeof(*a) - sizeof(int)) == 0;
}

/* Tries the inputs B's core leaves open for the tick R-1 -> R. */
static bool one_tick_explains(const dd_collision *col, const dd_netobj_character_core *before, const dd_netobj_character_core *after) {
  /* Aim candidates: the hook direction (exact if the hook was launched this
   * tick) and the net angle. */
  const float angle = after->m_Angle / 256.0f;
  const int targets[2][2] = {{after->m_HookDx, after->m_HookDy},
                             {(int)lroundf(cosf(angle) * 1024.0f), (int)lroundf(sinf(angle) * 1024.0f)}};
  for (int t = 0; t < 2; ++t) {
    if (targets[t][0] == 0 && targets[t][1] == 0) continue;
    for (int jump = 0; jump <= 1; ++jump) {
      for (int hook = 0; hook <= 1; ++hook) {
        dd_world_core world;
        memset(&world, 0, sizeof(world));
        dd_teams_core teams;
        dd_teams_reset(&teams);
        dd_character_core core;
        dd_core_init(&core, &world, col, &teams);
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
        out.m_Angle = after->m_Angle; /* set by the aim, which is only approximated here */
        if (core_equal(&out, after)) return true;
      }
    }
  }
  return false;
}

static void add_gap(coverage_t *c, int len, bool verified, int cause) {
  if (len <= 0) return;
  if (verified) {
    c->verified += len;
    c->gaps_verified++;
  } else {
    c->unverified += len;
    c->gaps_unverified++;
    c->unverified_by_len[len < MAX_GAP_BUCKET ? len : MAX_GAP_BUCKET]++;
    c->cause[cause]++;
  }
}

static bool run_demo(const char *path, coverage_t *total) {
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  dd_demo_reader *dr = demo_r_create();
  if (!demo_r_open(dr, f)) {
    demo_r_destroy(&dr);
    fclose(f);
    return false;
  }
  const dd_demo_info *info = demo_r_get_info(dr);
  uint8_t *map_bytes = (uint8_t *)malloc(info->map_size);
  if (!map_bytes || !demo_r_read_map(dr, map_bytes, info->map_size)) {
    free(map_bytes);
    demo_r_destroy(&dr);
    fclose(f);
    return false;
  }
  map_data_t map = load_map_from_memory(map_bytes, info->map_size);
  dd_collision col;
  if (!dd_collision_init(&col, &map)) {
    free_map_data(&map);
    demo_r_destroy(&dr);
    fclose(f);
    return false;
  }

  static uint8_t unpacked[DD_SNAPSHOT_MAX_SIZE];
  static last_snap_t last[DD_PHYS_MAX_CLIENTS];
  memset(last, 0, sizeof(last));
  coverage_t c;
  memset(&c, 0, sizeof(c));

  dd_demo_chunk chunk;
  while (demo_r_next_chunk(dr, &chunk)) {
    const dd_snapshot *snap = NULL;
    if (chunk.type == DD_CHUNK_SNAP) {
      snap = (const dd_snapshot *)chunk.data;
    } else if (chunk.type == DD_CHUNK_SNAP_DELTA) {
      if (demo_r_unpack_delta(dr, chunk.data, unpacked) < 0) continue;
      snap = (const dd_snapshot *)unpacked;
    } else {
      continue;
    }
    const int tb = chunk.tick;

    /* Everyone's position and DDNet extras in this snapshot, for the causes. */
    bool has_pos[DD_PHYS_MAX_CLIENTS] = {0};
    int pos_x[DD_PHYS_MAX_CLIENTS], pos_y[DD_PHYS_MAX_CLIENTS];
    const dd_netobj_ddnet_character *extra[DD_PHYS_MAX_CLIENTS] = {0};
    for (int i = 0; i < snap->num_items; ++i) {
      const dd_snap_item *item = dd_snap_get_item(snap, i);
      const int id = dd_snap_item_id(item);
      if (id < 0 || id >= DD_PHYS_MAX_CLIENTS) continue;
      if (dd_snap_item_type(item) == DD_NETOBJTYPE_CHARACTER) {
        const dd_netobj_character *ch = (const dd_netobj_character *)dd_snap_item_data(item);
        has_pos[id] = true;
        pos_x[id] = ch->core.m_X;
        pos_y[id] = ch->core.m_Y;
      } else if (dd_snap_item_type(item) == DD_NETOBJTYPE_DDNETCHARACTER) {
        extra[id] = (const dd_netobj_ddnet_character *)dd_snap_item_data(item);
      }
    }

    bool seen[DD_PHYS_MAX_CLIENTS] = {0};
    for (int i = 0; i < snap->num_items; ++i) {
      const dd_snap_item *item = dd_snap_get_item(snap, i);
      if (dd_snap_item_type(item) != DD_NETOBJTYPE_CHARACTER) continue;
      const int id = dd_snap_item_id(item);
      if (id < 0 || id >= DD_PHYS_MAX_CLIENTS) continue;
      seen[id] = true;
      const dd_netobj_character *now = (const dd_netobj_character *)dd_snap_item_data(item);
      last_snap_t *prev = &last[id];
      const int ta = prev->snap_tick, ra = prev->character.core.m_Tick, rb = now->core.m_Tick;

      if (!prev->present || ra <= 0 || rb <= 0 || tb <= ta) {
        c.unknown += prev->present && tb > ta ? tb - ta : 1;
      } else if (rb == ra) {
        c.exact += tb - ta; /* no re-sync: A's prediction covers the whole gap */
      } else if (rb > ta && rb <= tb) {
        c.exact += tb - rb + 1;
        const int gap = rb - ta - 1;
        if (gap > 0) {
          dd_netobj_character chain = prev->character;
          dd_evolve_character(&col, &chain, rb - 1);
          const bool verified = one_tick_explains(&col, &chain.core, &now->core);
          int cause = CAUSE_OTHER;
          if (!verified) {
            bool near = now->core.m_HookedPlayer != -1 || prev->character.core.m_HookedPlayer != -1;
            for (int o = 0; o < DD_PHYS_MAX_CLIENTS && !near; ++o)
              if (o != id && has_pos[o] && abs(pos_x[o] - now->core.m_X) < 100 && abs(pos_y[o] - now->core.m_Y) < 100) near = true;
            const bool frozen = extra[id] && (extra[id]->m_FreezeEnd != 0 || (extra[id]->m_Flags & DD_CHARACTERFLAG_IN_FREEZE));
            const bool fired = now->m_AttackTick > ta || now->m_Weapon == DD_WEAPON_NINJA;
            const bool tiles = special_tile_near(&map, now->core.m_X, now->core.m_Y) ||
                               special_tile_near(&map, prev->character.core.m_X, prev->character.core.m_Y);
            cause = near ? CAUSE_PLAYERS : frozen ? CAUSE_FROZEN : fired ? CAUSE_WEAPON : tiles ? CAUSE_TILES : CAUSE_OTHER;
          }
          add_gap(&c, gap, verified, cause);
        }
      } else {
        c.unknown += tb - ta; /* re-sync before A's tick without A seeing it: respawn or similar */
      }
      prev->present = true;
      prev->snap_tick = tb;
      prev->character = *now;
    }
    for (int id = 0; id < DD_PHYS_MAX_CLIENTS; ++id)
      if (!seen[id]) last[id].present = false;
  }

  const long all = c.exact + c.verified + c.unverified + c.unknown;
  printf("%s\n", path);
  printf("  character ticks %ld: exact %.1f%%, verified %.1f%%, needs simulation %.1f%%, unknown %.1f%%\n", all,
         100.0 * (double)c.exact / (double)all, 100.0 * (double)c.verified / (double)all, 100.0 * (double)c.unverified / (double)all,
         100.0 * (double)c.unknown / (double)all);
  printf("  gaps: %ld verified, %ld need simulation; by length:", c.gaps_verified, c.gaps_unverified);
  for (int l = 1; l <= MAX_GAP_BUCKET; ++l)
    if (c.unverified_by_len[l]) printf(" %s%d:%ld", l == MAX_GAP_BUCKET ? ">=" : "", l, c.unverified_by_len[l]);
  printf("\n  unverified gaps by likely cause:");
  for (int k = 0; k < NUM_CAUSES; ++k)
    printf(" %s %ld%s", cause_names[k], c.cause[k], k < NUM_CAUSES - 1 ? "," : "");
  printf("\n");

  total->exact += c.exact;
  total->verified += c.verified;
  total->unverified += c.unverified;
  total->unknown += c.unknown;
  total->gaps_verified += c.gaps_verified;
  total->gaps_unverified += c.gaps_unverified;
  for (int l = 0; l <= MAX_GAP_BUCKET; ++l)
    total->unverified_by_len[l] += c.unverified_by_len[l];
  for (int k = 0; k < NUM_CAUSES; ++k)
    total->cause[k] += c.cause[k];

  dd_collision_free(&col);
  free_map_data(&map);
  demo_r_destroy(&dr);
  fclose(f);
  return true;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <demo> [<demo> ...]\n", argv[0]);
    return 2;
  }
  coverage_t total;
  memset(&total, 0, sizeof(total));
  bool ok = true;
  for (int i = 1; i < argc; ++i)
    ok &= run_demo(argv[i], &total);
  const long all = total.exact + total.verified + total.unverified + total.unknown;
  printf("total %ld character ticks: exact %.1f%%, verified %.1f%%, needs simulation %.1f%%, unknown %.1f%%\n", all,
         100.0 * (double)total.exact / (double)all, 100.0 * (double)total.verified / (double)all,
         100.0 * (double)total.unverified / (double)all, 100.0 * (double)total.unknown / (double)all);
  printf("unverified gaps by likely cause:");
  for (int k = 0; k < NUM_CAUSES; ++k)
    printf(" %s %ld%s", cause_names[k], total.cause[k], k < NUM_CAUSES - 1 ? "," : "");
  printf("\n");
  return ok ? 0 : 1;
}
