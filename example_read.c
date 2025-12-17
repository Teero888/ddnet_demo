#include <stdio.h>

#define DDNET_DEMO_IMPLEMENTATION
#include "ddnet_demo.h"

void process_snapshot(const dd_snapshot *snap, int tick) {
  printf("  Items in snapshot at tick %d:\n", tick);
  for (int i = 0; i < snap->num_items; i++) {
    const dd_snap_item *item = dd_snap_get_item(snap, i);
    if (dd_snap_item_type(item) == DD_NETOBJTYPE_CHARACTER) {
      const dd_netobj_character *character = (const dd_netobj_character *)dd_snap_item_data(item);
      printf("    Character ID %d at (%d, %d)\n", dd_snap_item_id(item), character->core.m_X, character->core.m_Y);
    }
  }
}

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage: %s <demo_file>\n", argv[0]);
    return 1;
  }

  FILE *f = fopen(argv[1], "rb");
  if (!f) {
    printf("Failed to open file: %s\n", argv[1]);
    return 1;
  }

  dd_demo_reader *dr = demo_r_create();
  if (!demo_r_open(dr, f)) {
    printf("Failed to open demo file.\n");
    demo_r_destroy(&dr);
    fclose(f);
    return 1;
  }

  const dd_demo_info *info = demo_r_get_info(dr);
  printf("Version: %d\n", info->header.version);
  printf("Net version: %s\n", info->header.net_version);
  printf("Map name: %s\n", info->header.map_name);
  printf("Map size: %u\n", info->map_size);
  printf("Map crc: %u\n", info->map_crc);
  printf("Type: %s\n", info->header.type);
  printf("Length: %d\n", info->length);
  printf("Timestamp: %s\n", info->header.timestamp);
  printf("Num timeline markers: %d\n", info->num_markers);

  dd_demo_chunk chunk;
  uint8_t unpacked_snap[DD_SNAPSHOT_MAX_SIZE];

  while (demo_r_next_chunk(dr, &chunk)) {
    switch (chunk.type) {
    case DD_CHUNK_TICK_MARKER:
      printf("Tick: %d (is_keyframe: %d)\n", chunk.tick, chunk.is_keyframe);
      break;
    case DD_CHUNK_SNAP:
      printf("Snapshot at tick %d, size %d\n", chunk.tick, chunk.size);
      process_snapshot((const dd_snapshot *)chunk.data, chunk.tick);
      break;
    case DD_CHUNK_SNAP_DELTA:
      printf("Delta snapshot at tick %d, size %d\n", chunk.tick, chunk.size);
      int unpacked_size = demo_r_unpack_delta(dr, chunk.data, unpacked_snap);
      if (unpacked_size > 0) {
        printf("  -> unpacked to %d bytes\n", unpacked_size);
        process_snapshot((const dd_snapshot *)unpacked_snap, chunk.tick);
      } else {
        printf("  -> failed to unpack delta\n");
      }
      break;
    case DD_CHUNK_MSG:
      printf("Message at tick %d, size %d\n", chunk.tick, chunk.size);
      break;
    }
  }

  demo_r_destroy(&dr);
  fclose(f);

  return 0;
}
