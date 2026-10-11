/*
 * Clients above 63 come through like any other: DDNet servers have up to 128
 * players, and a server demo keeps their ids. Writes a demo with clients 3 and
 * 127, snapshots on even ticks like a real server, and checks that both are in
 * every tick, with their names and their recorded positions.
 *
 * Usage: test_clients <map>   (writes test_clients.demo in the working directory)
 */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DDNET_DEMO_IMPLEMENTATION
#include "ddnet_demo.h"
#define DDNET_DEMO_STATE_IMPLEMENTATION
#include "ddnet_demo_state.h"

#define NUM_TICKS 100
static const int clients[] = {3, 127};
#define NUM_CLIENTS (int)(sizeof(clients) / sizeof(clients[0]))

/* StrToInts */
static void str_to_ints(int *ints, int num, const char *str) {
  const int len = (int)strlen(str);
  for (int i = 0; i < num; i++) {
    unsigned bytes[4] = {0, 0, 0, 0};
    for (int j = 0; j < 4 && i * 4 + j < len; j++)
      bytes[j] = (unsigned char)str[i * 4 + j];
    ints[i] = (int)(((bytes[0] + 128) & 0xff) << 24 | ((bytes[1] + 128) & 0xff) << 16 | ((bytes[2] + 128) & 0xff) << 8 | ((bytes[3] + 128) & 0xff));
  }
  ints[num - 1] &= ~0xff; /* the last byte is always zero */
}

/* in the air above Aip-Gores' start, one tee apart */
static int x_of(int c) { return 160 * 32 + c * 64; }
static int y_of(int c) { return 15 * 32 + c * 64; }

static void write_demo(const char *path, const uint8_t *map, long map_size) {
  FILE *f = fopen(path, "wb");
  assert(f);
  dd_demo_writer *writer = demo_w_create();
  dd_snapshot_builder *sb = demo_sb_create();
  assert(writer && sb);
  const uint8_t sha256[32] = {0};
  assert(demo_w_begin(writer, f, "Aip-Gores", 0, "server"));
  assert(demo_w_write_map(writer, sha256, map, (uint32_t)map_size));
  static uint8_t snap[DD_SNAPSHOT_MAX_SIZE];
  for (int tick = 0; tick < NUM_TICKS; tick += 2) {
    demo_sb_clear(sb);
    dd_netobj_game_info *game_info = demo_sb_add_item(sb, DD_NETOBJTYPE_GAMEINFO, 0, sizeof(*game_info));
    assert(game_info);
    game_info->m_GameStateFlags = DD_GAMESTATEFLAG_RACETIME;
    for (int c = 0; c < NUM_CLIENTS; c++) {
      const int cid = clients[c];
      char name[16];
      snprintf(name, sizeof(name), "client %d", cid);
      dd_netobj_client_info *cinfo = demo_sb_add_item(sb, DD_NETOBJTYPE_CLIENTINFO, cid, sizeof(*cinfo));
      dd_netobj_player_info *pinfo = demo_sb_add_item(sb, DD_NETOBJTYPE_PLAYERINFO, cid, sizeof(*pinfo));
      dd_netobj_character *character = demo_sb_add_item(sb, DD_NETOBJTYPE_CHARACTER, cid, sizeof(*character));
      assert(cinfo && pinfo && character);
      str_to_ints(cinfo->m_aName, 4, name);
      str_to_ints(cinfo->m_aSkin, 6, "default");
      pinfo->m_ClientId = cid;
      pinfo->m_Latency = -1;
      character->core.m_Tick = tick;
      character->core.m_X = x_of(c);
      character->core.m_Y = y_of(c);
      character->m_Health = 10;
      character->m_Weapon = DD_WEAPON_GUN;
    }
    const int size = demo_sb_finish(sb, snap);
    assert(size > 0);
    assert(demo_w_write_snap(writer, tick, snap, size));
  }
  assert(demo_w_finish(writer));
  demo_sb_destroy(&sb);
  demo_w_destroy(&writer);
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <map>\n", argv[0]);
    return 2;
  }
  FILE *f = fopen(argv[1], "rb");
  assert(f);
  fseek(f, 0, SEEK_END);
  const long map_size = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *map = malloc((size_t)map_size);
  assert(map && fread(map, 1, (size_t)map_size, f) == (size_t)map_size);
  fclose(f);

  const char *path = "test_clients.demo";
  write_demo(path, map, map_size);
  free(map);

  char error[256] = "";
  dd_demo_state *st = dd_demo_state_load(path, error, sizeof(error));
  if (!st) {
    printf("%s: %s\n", path, error);
    return 1;
  }
  static dd_state_tick tick;
  for (int t = dd_demo_state_first_tick(st); t <= dd_demo_state_last_tick(st); t++) {
    assert(dd_demo_state_get(st, t, &tick));
    for (int c = 0; c < NUM_CLIENTS; c++) {
      const int cid = clients[c];
      char name[16];
      snprintf(name, sizeof(name), "client %d", cid);
      assert(tick.players[cid].connected);
      assert(strcmp(tick.players[cid].name, name) == 0);
      assert(tick.characters[cid].quality != DD_QUALITY_NONE);
      if (t % 2 == 0) {
        assert(tick.characters[cid].quality == DD_QUALITY_RECORDED);
        assert(tick.characters[cid].x == (float)x_of(c) && tick.characters[cid].y == (float)y_of(c));
      }
    }
  }
  printf("clients 3 and 127 in all %d ticks\n", dd_demo_state_last_tick(st) - dd_demo_state_first_tick(st) + 1);
  dd_demo_state_free(st);
  remove(path);
  return 0;
}
