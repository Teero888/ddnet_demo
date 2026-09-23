/*
 * Dead reckoning conformance test against real DDNet client demos.
 *
 * A snapped character is the server's core at its last re-sync tick R. The
 * server re-syncs when its own input-free prediction (exactly what
 * dd_evolve_character does) stops matching the real core, or after 3 seconds
 * even if it still matches. So for two consecutive re-syncs R1 < R2:
 *   - a 3-second re-sync (R2 == R1 + 151) happened while the prediction still
 *     matched up to R2 - 1, so evolving R1's core to R2 must equal R2's core
 *     exactly, unless a real divergence happened on R2 itself. Such a
 *     coincidence shows up as a one-tick event: only input fields differ, or
 *     the position is off by no more than one tick of the velocity difference
 *     (a hammer, a collision). Anything else is a bug in the physics port.
 *   - any other re-sync may or may not follow a real divergence (new input,
 *     tiles the core does not handle, another player), so those are only
 *     reported as a match rate.
 *
 * Usage: test_reckoning <demo> [<demo> ...]
 * Exits non-zero if any 3-second re-sync differs in a way a single tick cannot explain.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DDNET_DEMO_IMPLEMENTATION
#include "ddnet_demo.h"
#include "internal/dd_physics.h"

#define RECKONING_CAP (DD_PHYS_SERVER_TICK_SPEED * 3 + 1)

typedef struct {
  bool present;
  dd_netobj_character character;
} last_snap_t;

typedef struct {
  long cap_total, cap_match, cap_explained;
  long other_total, other_match;
} counts_t;

static const char *const field_names[] = {"tick", "x", "y", "vel_x", "vel_y", "angle", "direction", "jumped",
                                          "hooked_player", "hook_state", "hook_tick", "hook_x", "hook_y", "hook_dx", "hook_dy"};

/* Compares every core field but the tick; returns the first differing field or -1. */
static int core_diff(const dd_netobj_character_core *a, const dd_netobj_character_core *b) {
  const int *ia = (const int *)a, *ib = (const int *)b;
  for (int i = 1; i < (int)(sizeof(*a) / sizeof(int)); ++i)
    if (ia[i] != ib[i]) return i;
  return -1;
}

/* True when a mismatch fits a real divergence on the re-sync tick itself:
 * only fields that input changes directly differ, or the position moved at
 * most one tick's worth of the velocity difference. */
static bool single_tick_event(const dd_netobj_character_core *e, const dd_netobj_character_core *n) {
  const bool physics_differs = e->m_X != n->m_X || e->m_Y != n->m_Y || e->m_VelX != n->m_VelX || e->m_VelY != n->m_VelY;
  if (!physics_differs) return true;
  const int dvx = abs(e->m_VelX - n->m_VelX), dvy = abs(e->m_VelY - n->m_VelY);
  return abs(e->m_X - n->m_X) <= dvx / 256 + 1 && abs(e->m_Y - n->m_Y) <= dvy / 256 + 1;
}

static bool run_demo(const char *path, counts_t *total) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    printf("%s: cannot open\n", path);
    return false;
  }
  dd_demo_reader *dr = demo_r_create();
  if (!demo_r_open(dr, f)) {
    printf("%s: not a demo\n", path);
    demo_r_destroy(&dr);
    fclose(f);
    return false;
  }

  const dd_demo_info *info = demo_r_get_info(dr);
  uint8_t *map_bytes = (uint8_t *)malloc(info->map_size);
  if (!map_bytes || !demo_r_read_map(dr, map_bytes, info->map_size)) {
    printf("%s: no embedded map\n", path);
    free(map_bytes);
    demo_r_destroy(&dr);
    fclose(f);
    return false;
  }
  /* load_map_from_memory takes ownership of the buffer. */
  map_data_t map = load_map_from_memory(map_bytes, info->map_size);
  dd_collision col;
  if (!dd_collision_init(&col, &map)) {
    printf("%s: map has no game layer\n", path);
    free_map_data(&map);
    demo_r_destroy(&dr);
    fclose(f);
    return false;
  }

  static uint8_t unpacked[DD_SNAPSHOT_MAX_SIZE];
  static last_snap_t last[DD_PHYS_MAX_CLIENTS];
  memset(last, 0, sizeof(last));
  counts_t counts = {0};
  int reported = 0;

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

    bool seen[DD_PHYS_MAX_CLIENTS] = {0};
    for (int i = 0; i < snap->num_items; ++i) {
      const dd_snap_item *item = dd_snap_get_item(snap, i);
      if (dd_snap_item_type(item) != DD_NETOBJTYPE_CHARACTER) continue;
      const int id = dd_snap_item_id(item);
      if (id < 0 || id >= DD_PHYS_MAX_CLIENTS) continue;
      const dd_netobj_character *now = (const dd_netobj_character *)dd_snap_item_data(item);
      seen[id] = true;

      last_snap_t *prev = &last[id];
      const int r1 = prev->character.core.m_Tick, r2 = now->core.m_Tick;
      /* Tick 0 means the server sent its live core without reckoning. */
      if (prev->present && r1 > 0 && r2 > r1 && r2 - r1 <= RECKONING_CAP) {
        dd_netobj_character evolved = prev->character;
        dd_evolve_character(&col, &evolved, r2);
        const int diff = core_diff(&evolved.core, &now->core);
        const bool cap = r2 - r1 == RECKONING_CAP;
        const bool explained = diff >= 0 && single_tick_event(&evolved.core, &now->core);
        if (cap) {
          counts.cap_total++;
          if (diff < 0) counts.cap_match++;
          if (explained) counts.cap_explained++;
        } else {
          counts.other_total++;
          if (diff < 0) counts.other_match++;
        }
        if (cap && diff >= 0 && reported < 10) {
          reported++;
          const int *e = (const int *)&evolved.core, *n = (const int *)&now->core;
          printf("  %s client %d, ticks %d -> %d:", explained ? "one-tick event" : "MISMATCH", id, r1, r2);
          for (int k = 1; k < (int)(sizeof(evolved.core) / sizeof(int)); ++k)
            if (e[k] != n[k]) printf(" %s %d/%d", field_names[k], e[k], n[k]);
          printf("  (evolved/demo)\n");
        }
      }
      prev->present = true;
      prev->character = *now;
    }
    for (int id = 0; id < DD_PHYS_MAX_CLIENTS; ++id)
      if (!seen[id]) last[id].present = false;
  }

  printf("%s\n  3-second re-syncs: %ld / %ld exact, %ld one-tick events, %ld unexplained\n"
         "  other re-syncs:    %ld / %ld predicted (info only)\n",
         path, counts.cap_match, counts.cap_total, counts.cap_explained, counts.cap_total - counts.cap_match - counts.cap_explained,
         counts.other_match, counts.other_total);
  total->cap_total += counts.cap_total;
  total->cap_match += counts.cap_match;
  total->cap_explained += counts.cap_explained;
  total->other_total += counts.other_total;
  total->other_match += counts.other_match;

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
  counts_t total = {0};
  bool ok = true;
  for (int i = 1; i < argc; ++i)
    ok &= run_demo(argv[i], &total);
  const long unexplained = total.cap_total - total.cap_match - total.cap_explained;
  printf("total: 3-second re-syncs %ld / %ld exact, %ld one-tick events, %ld unexplained; other re-syncs %ld / %ld predicted\n",
         total.cap_match, total.cap_total, total.cap_explained, unexplained, total.other_match, total.other_total);
  if (total.cap_total == 0) {
    printf("FAIL: no 3-second re-syncs found to test against\n");
    return 1;
  }
  return ok && unexplained == 0 ? 0 : 1;
}
