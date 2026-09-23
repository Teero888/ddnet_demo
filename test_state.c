/*
 * ddnet_demo_state.h against real demos: loads each one, reports how every
 * character tick was rebuilt, and checks the API's invariants:
 *   - a tick the server recorded directly comes back exactly as recorded
 *   - every tick of the demo can be queried
 *
 * Usage: test_state <demo> [<demo> ...]
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#define DDNET_DEMO_IMPLEMENTATION
#include "ddnet_demo.h"
#define DD_RC_DIAGNOSTICS
#define DDNET_DEMO_STATE_IMPLEMENTATION
#include "ddnet_demo_state.h"

static double now_seconds(void) { return (double)clock() / CLOCKS_PER_SEC; }

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <demo> [<demo> ...]\n", argv[0]);
    return 2;
  }
  int failures = 0;
  for (int arg = 1; arg < argc; ++arg) {
    char error[256];
    double start = now_seconds();
    dd_demo_state *st = dd_demo_state_load(argv[arg], error, sizeof(error));
    double load = now_seconds() - start;
    if (!st) {
      printf("%s: %s\n", argv[arg], error);
      failures++;
      continue;
    }
    dd_state_stats stats;
    dd_demo_state_stats(st, &stats);
    const double all = stats.character_ticks ? (double)stats.character_ticks : 1.0;
    printf("%s (map %s, ticks %d..%d, loaded in %.2fs)\n", argv[arg], dd_demo_state_map_name(st), dd_demo_state_first_tick(st),
           dd_demo_state_last_tick(st), load);
    printf("  %ld character ticks: recorded %.1f%%, exact %.1f%%, verified %.1f%%, approximated %.1f%%\n", stats.character_ticks,
           100.0 * stats.recorded / all, 100.0 * stats.exact / all, 100.0 * stats.verified / all, 100.0 * stats.approximated / all);

    /* every tick queries, and recorded ticks match their anchors */
    long mismatches = 0, queried = 0, messages = 0;
    start = now_seconds();
    dd_state_tick tick;
    for (int t = dd_demo_state_first_tick(st); t <= dd_demo_state_last_tick(st); ++t) {
      if (!dd_demo_state_get(st, t, &tick)) {
        mismatches++;
        continue;
      }
      queried++;
      messages += tick.num_messages;
    }
    const double query = now_seconds() - start;
    for (int cid = 0; cid < DD_STATE_MAX_CLIENTS; ++cid) {
      const dd_rc_track *track = &st->tracks[cid];
      for (int k = 0; k < track->count; ++k) {
        const dd_rc_anchor *a = &track->anchors[k];
        if (a->chr.core.m_Tick != a->snap_tick) continue;
        dd_demo_state_get(st, a->snap_tick, &tick);
        const dd_state_character *c = &tick.characters[cid];
        if (c->quality != DD_QUALITY_RECORDED || c->x != (float)a->chr.core.m_X || c->y != (float)a->chr.core.m_Y ||
            c->vel_x != a->chr.core.m_VelX / 256.0f || c->vel_y != a->chr.core.m_VelY / 256.0f)
          mismatches++;
      }
    }
    printf("  gaps: %ld by the bare physics, %ld more by the server world, %ld as two re-syncs, %ld solved jointly, %ld approximated\n"
           "  server world agrees with the bare physics on %ld / %ld gaps\n",
           dd_rc_diag.core_ok, dd_rc_diag.server_ok, dd_rc_diag.two_resyncs, dd_rc_diag.joint_ok, dd_rc_diag.approximated, dd_rc_diag.cross_agreed,
           dd_rc_diag.cross_checked);
    memset(&dd_rc_diag, 0, sizeof(dd_rc_diag));
    printf("  queried %ld ticks in %.2fs, %ld messages; %ld invariant failures\n", queried, query, messages, mismatches);
    if (mismatches) failures++;
    dd_demo_state_free(st);
  }
  return failures ? 1 : 0;
}
