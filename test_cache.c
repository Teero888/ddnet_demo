/*
 * dd_demo_state_load_ex against real demos:
 *   - progress only grows and ends at 1, and cancelling stops the load
 *   - a load from the reconstruction cache gives exactly the rebuilt state
 *   - a broken cache is rebuilt instead of trusted
 *   - dd_demo_state_characters agrees with dd_demo_state_get
 *   - the tuning lookup answers for every tick
 *
 * Usage: test_cache <demo> [<demo> ...]   (writes test_cache.ddrc in the working directory)
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define DDNET_DEMO_IMPLEMENTATION
#include "ddnet_demo.h"
#define DDNET_DEMO_STATE_IMPLEMENTATION
#include "ddnet_demo_state.h"

#define CACHE_PATH "test_cache.ddrc"

typedef struct {
  int calls, cancel_after;
  float last;
  bool went_back;
} progress_log;

static bool on_progress(void *user, float progress) {
  progress_log *log = (progress_log *)user;
  if (progress < log->last) log->went_back = true;
  log->last = progress;
  log->calls++;
  return log->cancel_after <= 0 || log->calls < log->cancel_after;
}

static double now_seconds(void) { return (double)clock() / CLOCKS_PER_SEC; }

static dd_demo_state *load(const char *path, const char *cache, progress_log *log, double *seconds, char *error, size_t error_size) {
  dd_state_load_options options = {on_progress, log, cache};
  const double start = now_seconds();
  dd_demo_state *st = dd_demo_state_load_ex(path, &options, error, error_size);
  if (seconds) *seconds = now_seconds() - start;
  return st;
}

/* Every tick's characters of both states are identical. */
static long compare_states(dd_demo_state *a, dd_demo_state *b) {
  long differences = 0;
  static dd_state_character ca[DD_STATE_MAX_CLIENTS], cb[DD_STATE_MAX_CLIENTS];
  for (int t = dd_demo_state_first_tick(a); t <= dd_demo_state_last_tick(a); ++t) {
    dd_demo_state_characters(a, t, ca);
    dd_demo_state_characters(b, t, cb);
    if (memcmp(ca, cb, sizeof(ca))) differences++;
  }
  return differences;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <demo> [<demo> ...]\n", argv[0]);
    return 2;
  }
  int failures = 0;
  for (int arg = 1; arg < argc; ++arg) {
    const char *path = argv[arg];
    char error[256];
    printf("%s\n", path);

    /* cancelling */
    progress_log cancel = {0, 3, 0.0f, false};
    dd_demo_state *cancelled = load(path, NULL, &cancel, NULL, error, sizeof(error));
    if (cancelled || strcmp(error, "cancelled") != 0) {
      printf("  FAIL: cancelling gave %s\n", cancelled ? "a state" : error);
      failures++;
      dd_demo_state_free(cancelled);
    }

    /* a rebuild writes the cache */
    remove(CACHE_PATH);
    progress_log built_log = {0, 0, 0.0f, false};
    double built_time = 0;
    dd_demo_state *built = load(path, CACHE_PATH, &built_log, &built_time, error, sizeof(error));
    if (!built) {
      printf("  FAIL: %s\n", error);
      failures++;
      continue;
    }
    if (built_log.went_back || built_log.last != 1.0f || built_log.calls < 3) {
      printf("  FAIL: progress went back %d, ended at %.3f after %d calls\n", built_log.went_back, built_log.last, built_log.calls);
      failures++;
    }
    FILE *f = fopen(CACHE_PATH, "rb");
    long cache_size = 0;
    if (f) {
      fseek(f, 0, SEEK_END);
      cache_size = ftell(f);
      fclose(f);
    } else {
      printf("  FAIL: no cache written\n");
      failures++;
    }

    /* a load from it matches */
    progress_log cached_log = {0, 0, 0.0f, false};
    double cached_time = 0;
    dd_demo_state *cached = load(path, CACHE_PATH, &cached_log, &cached_time, error, sizeof(error));
    if (!cached) {
      printf("  FAIL: from cache: %s\n", error);
      failures++;
    } else {
      const long differences = compare_states(built, cached);
      dd_state_stats sa, sb;
      dd_demo_state_stats(built, &sa);
      dd_demo_state_stats(cached, &sb);
      if (differences || memcmp(&sa, &sb, sizeof(sa))) {
        printf("  FAIL: cached state differs on %ld ticks\n", differences);
        failures++;
      }
      printf("  rebuilt in %.2fs, from a %.1f MB cache in %.2fs\n", built_time, cache_size / 1048576.0, cached_time);
      dd_demo_state_free(cached);
    }

    /* a truncated cache is rebuilt, and the rebuild rewrites it */
    f = fopen(CACHE_PATH, "r+b");
    if (f && cache_size > 64) {
      fclose(f);
      f = fopen(CACHE_PATH, "rb");
      static unsigned char head[64];
      const size_t n = fread(head, 1, sizeof(head), f);
      fclose(f);
      f = fopen(CACHE_PATH, "wb");
      fwrite(head, 1, n, f);
      fclose(f);
      dd_demo_state *rebuilt = load(path, CACHE_PATH, &cached_log, NULL, error, sizeof(error));
      if (!rebuilt || compare_states(built, rebuilt)) {
        printf("  FAIL: broken cache was not rebuilt\n");
        failures++;
      }
      dd_demo_state_free(rebuilt);
    } else if (f) {
      fclose(f);
    }

    /* characters agree with get, and tuning answers */
    long disagreements = 0;
    static dd_state_tick tick;
    static dd_state_character chars[DD_STATE_MAX_CLIENTS];
    float tuning[64];
    assert(dd_state_tuning_count() <= 64);
    for (int t = dd_demo_state_first_tick(built); t <= dd_demo_state_last_tick(built); ++t) {
      dd_demo_state_get(built, t, &tick);
      dd_demo_state_characters(built, t, chars);
      if (memcmp(tick.characters, chars, sizeof(chars))) disagreements++;
      if (!dd_demo_state_tuning(built, t, 0, tuning) || !(tuning[0] > 0.0f)) disagreements++;
    }
    if (disagreements) {
      printf("  FAIL: characters or tuning disagree on %ld ticks\n", disagreements);
      failures++;
    }
    if (strcmp(dd_state_tuning_name(0), "ground_control_speed") != 0 || dd_state_tuning_name(dd_state_tuning_count()) != NULL) {
      printf("  FAIL: tuning names\n");
      failures++;
    }
    dd_demo_state_free(built);
  }
  remove(CACHE_PATH);
  printf("%s\n", failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
