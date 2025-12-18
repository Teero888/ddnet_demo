#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DDNET_DEMO_IMPLEMENTATION
#include "ddnet_demo.h"

// Helper to print bytes for debugging
void print_bytes(const uint8_t *data, int size) {
  for (int i = 0; i < size; i++) {
    printf("%02x ", data[i]);
  }
  printf("\n");
}

void test_variable_int() {
  printf("Testing Variable Int packing/unpacking...\n");

  int test_values[] = {0, 1, -1, 63, -64, 64, -65, 127, -128, 12345, -12345, 1000000, -1000000, INT_MAX, INT_MIN + 1};
  uint8_t buffer[16];

  for (size_t i = 0; i < sizeof(test_values) / sizeof(test_values[0]); i++) {
    int val = test_values[i];

    memset(buffer, 0, sizeof(buffer));
    uint8_t *end = dd_variable_int_pack(buffer, val, sizeof(buffer));
    assert(end != NULL);

    int unpacked_val;
    const uint8_t *next = dd_variable_int_unpack(buffer, &unpacked_val, (int)(end - buffer));
    assert(next == end);
    assert(val == unpacked_val);
  }
  printf("Variable Int tests passed.\n");
}

void test_huffman() {
  printf("Testing Huffman compression/decompression...\n");

  dd_huffman_state state;
  dd_huffman_init(&state);

  const char *input_str = "Hello World! This is a test string for Huffman compression. It should handle various symbols.";
  int input_len = strlen(input_str) + 1;
  uint8_t compressed[1024];
  uint8_t decompressed[1024];

  int comp_size = dd_huffman_compress(&state, input_str, input_len, compressed, sizeof(compressed));
  assert(comp_size > 0);

  int decomp_size = dd_huffman_decompress(&state, compressed, comp_size, decompressed, sizeof(decompressed));
  assert(decomp_size == input_len);
  assert(memcmp(input_str, decompressed, input_len) == 0);

  printf("Huffman tests passed.\n");
}

void test_snapshot_builder() {
  printf("Testing Snapshot Builder...\n");

  dd_snapshot_builder *sb = demo_sb_create();
  assert(sb != NULL);

  // Add simple item
  dd_netobj_player_input *input = demo_sb_add_item(sb, DD_NETOBJTYPE_PLAYERINPUT, 0, sizeof(dd_netobj_player_input));
  assert(input != NULL);
  input->m_Direction = 1;
  input->m_TargetX = 100;

  // Add extended item (UUID)
  // This will implicitly add a DD_NETOBJTYPE_EX item before it to define the type
  dd_netobj_ddnet_player *ddplayer = demo_sb_add_item(sb, DD_NETOBJTYPE_DDNETPLAYER, 0, sizeof(dd_netobj_ddnet_player));
  assert(ddplayer != NULL);
  ddplayer->m_AuthLevel = 1;

  uint8_t snap_data[DD_SNAPSHOT_MAX_SIZE];
  int snap_size = demo_sb_finish(sb, snap_data);
  assert(snap_size > 0);

  dd_snapshot *snap = (dd_snapshot *)snap_data;
  // We expect 3 items:
  // 1. PLAYERINPUT (0)
  // 2. EX (Definition for DDNETPLAYER)
  // 3. DDNETPLAYER (The actual item)
  assert(snap->num_items == 3);

  const dd_snap_item *i0 = dd_snap_get_item(snap, 0);
  assert(dd_snap_item_type(i0) == DD_NETOBJTYPE_PLAYERINPUT);

  const dd_snap_item *i1 = dd_snap_get_item(snap, 1);
  assert(dd_snap_item_type(i1) == DD_NETOBJTYPE_EX);

  const dd_snap_item *i2 = dd_snap_get_item(snap, 2);
  // The type should not be DD_NETOBJTYPE_DDNETPLAYER (which is a UUID offset), but a dynamically assigned ID
  assert(dd_snap_item_type(i2) != DD_NETOBJTYPE_DDNETPLAYER);

  demo_sb_destroy(&sb);
  printf("Snapshot Builder tests passed.\n");
}

void test_messages(dd_demo_writer *dw, int tick) {
  assert(demo_w_write_msg_sv_broadcast(dw, "Broadcast message"));
  assert(demo_w_write_msg_sv_chat(dw, 1, 0, "Chat message"));
  assert(demo_w_write_msg_sv_killmsg(dw, 0, 1, DD_WEAPON_HAMMER, 0));
  assert(demo_w_write_msg_sv_sound_global(dw, DD_SOUND_CTF_CAPTURE));
  assert(demo_w_write_msg_sv_emoticon(dw, 0, DD_EMOTE_HAPPY));

  assert(demo_w_write_msg_sv_vote_set(dw, 30, "Vote Desc", "Reason"));
  assert(demo_w_write_msg_sv_vote_status(dw, 5, 2, 0, 7));
  assert(demo_w_write_msg_sv_ddrace_time_legacy(dw, 1000, 1, 0));
  assert(demo_w_write_msg_sv_record_legacy(dw, 900, 950));
}

void test_complex_deltas() {
  printf("Testing Complex Deltas...\n");
  const char *filename = "test_delta.demo";
  FILE *f = fopen(filename, "wb");
  assert(f != NULL);

  dd_demo_writer *dw = demo_w_create();
  assert(dw != NULL);
  assert(demo_w_begin(dw, f, "delta_map", 0, "test"));

  dd_snapshot_builder *sb = demo_sb_create();
  uint8_t snap_buf[DD_SNAPSHOT_MAX_SIZE];

  // Tick 10: Items [A, B]
  demo_sb_clear(sb);
  dd_netobj_player_input *inpA = demo_sb_add_item(sb, DD_NETOBJTYPE_PLAYERINPUT, 0, sizeof(dd_netobj_player_input));
  inpA->m_Direction = 10;
  dd_netobj_player_input *inpB = demo_sb_add_item(sb, DD_NETOBJTYPE_PLAYERINPUT, 1, sizeof(dd_netobj_player_input));
  inpB->m_Direction = 20;
  int size = demo_sb_finish(sb, snap_buf);
  assert(demo_w_write_snap(dw, 10, snap_buf, size));

  // Tick 11: Items [B(changed), C] (A deleted, B updated, C added)
  demo_sb_clear(sb);
  inpB = demo_sb_add_item(sb, DD_NETOBJTYPE_PLAYERINPUT, 1, sizeof(dd_netobj_player_input));
  inpB->m_Direction = 25; // Changed
  dd_netobj_player_input *inpC = demo_sb_add_item(sb, DD_NETOBJTYPE_PLAYERINPUT, 2, sizeof(dd_netobj_player_input));
  inpC->m_Direction = 30;
  size = demo_sb_finish(sb, snap_buf);
  assert(demo_w_write_snap(dw, 11, snap_buf, size));

  demo_w_finish(dw);
  demo_w_destroy(&dw);
  demo_sb_destroy(&sb);
  fclose(f);

  // Read and verify
  f = fopen(filename, "rb");
  assert(f != NULL);
  dd_demo_reader *dr = demo_r_create();
  assert(demo_r_open(dr, f));

  dd_demo_chunk chunk;
  uint8_t unpacked[DD_SNAPSHOT_MAX_SIZE];
  bool checked_tick_11 = false;

  while (demo_r_next_chunk(dr, &chunk)) {
    if (chunk.type == DD_CHUNK_SNAP_DELTA && chunk.tick == 11) {
      int u_size = demo_r_unpack_delta(dr, chunk.data, unpacked);
      assert(u_size > 0);
      dd_snapshot *snap = (dd_snapshot *)unpacked;

      // Should contain B and C
      assert(snap->num_items == 2);

      const dd_snap_item *itemB = dd_snap_find_item(snap, DD_NETOBJTYPE_PLAYERINPUT, 1);
      assert(itemB != NULL);
      const dd_netobj_player_input *valB = (const dd_netobj_player_input *)dd_snap_item_data(itemB);
      assert(valB->m_Direction == 25);

      const dd_snap_item *itemC = dd_snap_find_item(snap, DD_NETOBJTYPE_PLAYERINPUT, 2);
      assert(itemC != NULL);
      const dd_netobj_player_input *valC = (const dd_netobj_player_input *)dd_snap_item_data(itemC);
      assert(valC->m_Direction == 30);

      const dd_snap_item *itemA = dd_snap_find_item(snap, DD_NETOBJTYPE_PLAYERINPUT, 0);
      assert(itemA == NULL);

      checked_tick_11 = true;
    }
  }

  assert(checked_tick_11);

  demo_r_destroy(&dr);
  fclose(f);
  remove(filename);
  printf("Complex Delta tests passed.\n");
}

void test_full_workflow() {
  printf("Testing Full Writer/Reader Workflow...\n");

  const char *filename = "test_full.demo";
  FILE *f = fopen(filename, "wb");
  assert(f != NULL);

  dd_demo_writer *dw = demo_w_create();
  assert(demo_w_begin(dw, f, "test_map", 0x1234, "test"));

  // Write Map
  uint8_t map_sha[32] = {0};
  uint8_t map_data[] = {1, 2, 3, 4};
  assert(demo_w_write_map(dw, map_sha, map_data, sizeof(map_data)));

  // Write Messages
  test_messages(dw, 1);

  // Finish
  demo_w_finish(dw);
  demo_w_destroy(&dw);
  fclose(f);

  // Verify reading
  f = fopen(filename, "rb");
  assert(f != NULL);
  dd_demo_reader *dr = demo_r_create();
  assert(demo_r_open(dr, f));

  dd_demo_chunk chunk;
  int msg_count = 0;
  while (demo_r_next_chunk(dr, &chunk)) {
    if (chunk.type == DD_CHUNK_MSG) {
      msg_count++;
    }
  }
  // We wrote 9 messages in test_messages
  assert(msg_count == 9);

  demo_r_destroy(&dr);
  fclose(f);
  remove(filename);
  printf("Full Workflow tests passed.\n");
}

int main() {
  test_variable_int();
  test_huffman();
  test_snapshot_builder();
  test_complex_deltas();
  test_full_workflow();

  printf("All unit tests passed successfully!\n");
  return 0;
}
