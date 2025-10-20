#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DDNET_DEMO_IMPLEMENTATION
#include "ddnet_demo.h"

// SHA-256 Implementation
// Necessary for creating a valid demo header
typedef struct {
  uint8_t data[64];
  uint32_t datalen;
  uint64_t bitlen;
  uint32_t state[8];
} SHA256_CTX;

#define DBL_INT_ADD(a, b, c)                                                                                                                                   \
  if (a > 0xffffffff - (c)) ++b;                                                                                                                               \
  a += c;
#define ROTLEFT(a, b) (((a) << (b)) | ((a) >> (32 - (b))))
#define ROTRIGHT(a, b) (((a) >> (b)) | ((a) << (32 - (b))))

#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x, 2) ^ ROTRIGHT(x, 13) ^ ROTRIGHT(x, 22))
#define EP1(x) (ROTRIGHT(x, 6) ^ ROTRIGHT(x, 11) ^ ROTRIGHT(x, 25))
#define SIG0(x) (ROTRIGHT(x, 7) ^ ROTRIGHT(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x, 17) ^ ROTRIGHT(x, 19) ^ ((x) >> 10))

static const uint32_t k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74,
    0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d,
    0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e,
    0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

void sha256_transform(SHA256_CTX *ctx, const uint8_t data[]) {
  uint32_t a, b, c, d, e, f, g, h, i, j, t1, t2, m[64];
  for (i = 0, j = 0; i < 16; ++i, j += 4)
    m[i] = (data[j] << 24) | (data[j + 1] << 16) | (data[j + 2] << 8) | (data[j + 3]);
  for (; i < 64; ++i)
    m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];
  a = ctx->state[0];
  b = ctx->state[1];
  c = ctx->state[2];
  d = ctx->state[3];
  e = ctx->state[4];
  f = ctx->state[5];
  g = ctx->state[6];
  h = ctx->state[7];
  for (i = 0; i < 64; ++i) {
    t1 = h + EP1(e) + CH(e, f, g) + k[i] + m[i];
    t2 = EP0(a) + MAJ(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
  ctx->state[5] += f;
  ctx->state[6] += g;
  ctx->state[7] += h;
}

void sha256_init(SHA256_CTX *ctx) {
  ctx->datalen = 0;
  ctx->bitlen = 0;
  ctx->state[0] = 0x6a09e667;
  ctx->state[1] = 0xbb67ae85;
  ctx->state[2] = 0x3c6ef372;
  ctx->state[3] = 0xa54ff53a;
  ctx->state[4] = 0x510e527f;
  ctx->state[5] = 0x9b05688c;
  ctx->state[6] = 0x1f83d9ab;
  ctx->state[7] = 0x5be0cd19;
}

void sha256_update(SHA256_CTX *ctx, const uint8_t data[], size_t len) {
  for (size_t i = 0; i < len; ++i) {
    ctx->data[ctx->datalen] = data[i];
    ctx->datalen++;
    if (ctx->datalen == 64) {
      sha256_transform(ctx, ctx->data);
      DBL_INT_ADD(ctx->bitlen, ctx->bitlen, 512);
      ctx->datalen = 0;
    }
  }
}

void sha256_final(SHA256_CTX *ctx, uint8_t hash[]) {
  uint32_t i = ctx->datalen;
  if (ctx->datalen < 56) {
    ctx->data[i++] = 0x80;
    while (i < 56)
      ctx->data[i++] = 0x00;
  } else {
    ctx->data[i++] = 0x80;
    while (i < 64)
      ctx->data[i++] = 0x00;
    sha256_transform(ctx, ctx->data);
    memset(ctx->data, 0, 56);
  }
  DBL_INT_ADD(ctx->bitlen, ctx->bitlen, ctx->datalen * 8);
  ctx->data[63] = ctx->bitlen;
  ctx->data[62] = ctx->bitlen >> 8;
  ctx->data[61] = ctx->bitlen >> 16;
  ctx->data[60] = ctx->bitlen >> 24;
  ctx->data[59] = ctx->bitlen >> 32;
  ctx->data[58] = ctx->bitlen >> 40;
  ctx->data[57] = ctx->bitlen >> 48;
  ctx->data[56] = ctx->bitlen >> 56;
  sha256_transform(ctx, ctx->data);
  for (i = 0; i < 4; ++i) {
    hash[i] = (ctx->state[0] >> (24 - i * 8)) & 0x000000ff;
    hash[i + 4] = (ctx->state[1] >> (24 - i * 8)) & 0x000000ff;
    hash[i + 8] = (ctx->state[2] >> (24 - i * 8)) & 0x000000ff;
    hash[i + 12] = (ctx->state[3] >> (24 - i * 8)) & 0x000000ff;
    hash[i + 16] = (ctx->state[4] >> (24 - i * 8)) & 0x000000ff;
    hash[i + 20] = (ctx->state[5] >> (24 - i * 8)) & 0x000000ff;
    hash[i + 24] = (ctx->state[6] >> (24 - i * 8)) & 0x000000ff;
    hash[i + 28] = (ctx->state[7] >> (24 - i * 8)) & 0x000000ff;
  }
}

// CRC32 Implementation
uint32_t crc32_for_byte(uint32_t r) {
  for (int j = 0; j < 8; ++j)
    r = (r & 1 ? 0 : (uint32_t)0xEDB88320L) ^ r >> 1;
  return r ^ (uint32_t)0xFF000000L;
}

uint32_t crc32(const void *data, size_t n_bytes) {
  static uint32_t table[0x100];
  if (!*table)
    for (size_t i = 0; i < 0x100; ++i)
      table[i] = crc32_for_byte(i);
  uint32_t crc = 0;
  for (size_t i = 0; i < n_bytes; ++i)
    crc = table[(uint8_t)crc ^ ((uint8_t *)data)[i]] ^ crc >> 8;
  return crc;
}

// helper to get map name from path
void get_map_name_from_path(const char *filepath, char *map_name_buffer, size_t buffer_size) {
  const char *start = filepath;
  const char *end = filepath + strlen(filepath);
  const char *slash = strrchr(filepath, '/');
  const char *backslash = strrchr(filepath, '\\');
  if (slash && slash > start) start = slash + 1;
  if (backslash && backslash > start) start = backslash + 1;
  const char *dot = strrchr(start, '.');
  if (dot) {
    end = dot;
  }
  size_t len = end - start;
  if (len >= buffer_size) {
    len = buffer_size - 1;
  }
  memcpy(map_name_buffer, start, len);
  map_name_buffer[len] = '\0';
}

// Helper to convert string to Teeworlds' integer-based string format
void str_to_ints(int *pInts, int num_ints, const char *pStr) {
  int len = strlen(pStr);
  for (int i = 0; i < num_ints; i++) {
    pInts[i] = 0;
    for (int j = 0; j < 4; j++) {
      int char_index = i * 4 + j;
      if (char_index < len) {
        pInts[i] |= ((int)pStr[char_index]) << (j * 8);
      }
    }
  }
}

void send_chat_message(dd_demo_writer *writer, int tick, int client_id, int team, const char *message) {
  uint8_t msg_buffer[1024];
  int *p_data = (int *)msg_buffer;

  // The message payload must start with the message type ID
  *p_data++ = DD_NETMSGTYPE_SV_CHAT;
  *p_data++ = team;
  *p_data++ = client_id;

  // Copy the string right after the integers
  strcpy((char *)p_data, message);

  // Calculate total size: 3 ints + string length + null terminator
  int size = (sizeof(int) * 3) + strlen(message) + 1;

  demo_w_write_msg(writer, tick, msg_buffer, size);
}

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage: %s <mapfile.map>\n", argv[0]);
    return 1;
  }

  const char *map_filepath = argv[1];
  const char *demo_filename = "generated_demo_full.demo";
  const int num_players = 4;
  const int demo_duration_ticks = 1000; // 20 seconds

  // Load map file
  FILE *f_map = fopen(map_filepath, "rb");
  if (!f_map) {
    perror("Error opening map file");
    return 1;
  }

  fseek(f_map, 0, SEEK_END);
  long map_size = ftell(f_map);
  fseek(f_map, 0, SEEK_SET);

  uint8_t *map_data = (uint8_t *)malloc(map_size);
  if (!map_data) {
    fprintf(stderr, "Failed to allocate memory for map data (%ld bytes)\n", map_size);
    fclose(f_map);
    return 1;
  }
  if (fread(map_data, 1, map_size, f_map) != (size_t)map_size) {
    fprintf(stderr, "Failed to read map file\n");
    fclose(f_map);
    free(map_data);
    return 1;
  }
  fclose(f_map);
  printf("Loaded map '%s' (%ld bytes)\n", map_filepath, map_size);

  // Calculate map checksums
  char map_name[128];
  get_map_name_from_path(map_filepath, map_name, sizeof(map_name));

  uint32_t map_crc = crc32(map_data, map_size);
  uint8_t map_sha256[32];
  SHA256_CTX ctx;
  sha256_init(&ctx);
  sha256_update(&ctx, map_data, map_size);
  sha256_final(&ctx, map_sha256);

  printf("Calculated Map Name: %s\n", map_name);
  printf("Calculated Map CRC32: 0x%08x\n", map_crc);
  printf("Calculated Map SHA256: ");
  for (int i = 0; i < 32; ++i)
    printf("%02x", map_sha256[i]);
  printf("\n\n");

  // Create and begin writing the demo
  dd_demo_writer *writer = demo_w_create();
  FILE *f_demo = fopen(demo_filename, "wb");
  if (!writer || !f_demo) {
    fprintf(stderr, "Error: Could not create demo writer or open output file.\n");
    free(map_data);
    return 1;
  }

  printf("Starting demo creation: %s\n", demo_filename);

  demo_w_begin(writer, f_demo, map_name, map_crc, "Race");
  demo_w_write_map(writer, map_sha256, map_data, map_size);
  free(map_data);

  dd_snapshot_builder *sb = demo_sb_create();
  uint8_t snap_buf[DD_MAX_SNAPSHOT_SIZE];

  for (int tick = 0; tick < demo_duration_ticks; ++tick) {
    demo_sb_clear(sb);
    int next_item_id = num_players;

    // chat message examples
    if (tick == 0) {
      // server broadcast message at the start
      send_chat_message(writer, tick, -1, 0, "*** Welcome to the demo! ***");
    }
    if (tick > 0 && tick % (DD_SERVER_TICK_SPEED * 5) == 0) {
      // player 0 says something to everyone
      send_chat_message(writer, tick, 0, 0, "Hello everyone!");
    }
    if (tick > 0 && tick % (DD_SERVER_TICK_SPEED * 8) == 0) {
      // player 2 says something to their team (team 0)
      send_chat_message(writer, tick, 2, 1, "Team message!");
    }

    // Game Info (including extended version)
    dd_netobj_game_info *game_info = demo_sb_add_item(sb, DD_NETOBJTYPE_GAMEINFO, 0, sizeof(dd_netobj_game_info));
    if (game_info) {
      game_info->m_RoundStartTick = 0;
      game_info->m_GameStateFlags = DD_GAMESTATEFLAG_RACETIME;
    }

    // Adding an Ex object. The library now handles this automatically.
    dd_netobj_game_info_ex *game_info_ex = demo_sb_add_item(sb, DD_NETOBJTYPE_GAMEINFOEX, 0, sizeof(dd_netobj_game_info_ex));
    if (game_info_ex) {
      game_info_ex->m_Version = 10;
      game_info_ex->m_Flags = DD_GAMEINFOFLAG_GAMETYPE_DDNET | DD_GAMEINFOFLAG_UNLIMITED_AMMO;
    }

    // Players
    for (int i = 0; i < num_players; ++i) {
      int start_x = 160 * 32;
      int start_y = 15 * 32;

      // ClientInfo
      dd_netobj_client_info *cinfo = demo_sb_add_item(sb, DD_NETOBJTYPE_CLIENTINFO, i, sizeof(dd_netobj_client_info));
      if (cinfo) {
        char player_name[16];
        snprintf(player_name, sizeof(player_name), "Player %d", i);
        str_to_ints(cinfo->m_aName, 4, player_name);
        str_to_ints(cinfo->m_aClan, 3, "Demo");
        str_to_ints(cinfo->m_aSkin, 6, "default");
        cinfo->m_UseCustomColor = 0;
      }

      // PlayerInfo
      dd_netobj_player_info *pinfo = demo_sb_add_item(sb, DD_NETOBJTYPE_PLAYERINFO, i, sizeof(dd_netobj_player_info));
      if (pinfo) {
        pinfo->m_Local = (i == 0);
        pinfo->m_ClientId = i;
        pinfo->m_Team = 0;
        pinfo->m_Score = tick;
        pinfo->m_Latency = -1;
      }

      // DDNetPlayer (Ex object)
      dd_netobj_ddnet_player *ddpinfo = demo_sb_add_item(sb, DD_NETOBJTYPE_DDNETPLAYER, i, sizeof(dd_netobj_ddnet_player));
      if (ddpinfo) {
        ddpinfo->m_Flags = 0;
        ddpinfo->m_AuthLevel = 0;
      }

      // Character
      dd_netobj_character *character = demo_sb_add_item(sb, DD_NETOBJTYPE_CHARACTER, i, sizeof(dd_netobj_character));
      float angle = (tick / 50.0f) + ((float)i / num_players) * (2.0f * 3.14159265f);
      character->core.m_X = start_x + (int)(cosf(angle) * 150.0f);
      character->core.m_Y = start_y + (int)(sinf(angle) * 150.0f);
      character->core.m_Tick = tick;
      character->m_Health = 10;
      character->m_Weapon = DD_WEAPON_GUN;

      // Add a laser from each player every 50 ticks (Ex object)
      if (true) {
        dd_netobj_ddnet_laser *laser = demo_sb_add_item(sb, DD_NETOBJTYPE_DDNETLASER, next_item_id++, sizeof(dd_netobj_ddnet_laser));
        if (laser) {
          laser->m_FromX = character->core.m_X;
          laser->m_FromY = character->core.m_Y;
          laser->m_ToX = start_x; // Target the center
          laser->m_ToY = start_y;
          laser->m_StartTick = tick;
          laser->m_Owner = i;
          laser->m_Type = DD_LASERTYPE_RIFLE;
        }
      }

      // Add a projectile every 10 ticks (Ex object)
      for (int p = 0; p < 5; ++p) {
        dd_netobj_ddnet_projectile *proj = demo_sb_add_item(sb, DD_NETOBJTYPE_DDNETPROJECTILE, next_item_id++, sizeof(dd_netobj_ddnet_projectile));
        if (proj) {
          float angle = (tick / 50.0f) + ((float)p / 5.f) * (2.0f * 3.14159265f);
          proj->m_X = character->core.m_X + (int)(cosf(angle) * 32.0f);
          proj->m_Y = character->core.m_Y + (int)(sinf(angle) * 32.0f);
          proj->m_X *= 100.f;
          proj->m_Y *= 100.f;
          proj->m_VelX = 0; //(int)(cosf(proj_angle) * 1000.0f);
          proj->m_VelY = 0; //(int)(sinf(proj_angle) * 1000.0f);
          proj->m_Type = DD_WEAPON_GRENADE;
          proj->m_StartTick = tick - 1;
          proj->m_Owner = i;
          proj->m_Flags = DD_PROJECTILEFLAG_EXPLOSIVE;
          proj->m_SwitchNumber = 0;
          proj->m_TuneZone = 0;
        }
      }
    }
    if (tick % 100 == 0) {
    }

    int snap_size = demo_sb_finish(sb, snap_buf);
    if (snap_size > 0) {
      demo_w_write_snap(writer, tick, snap_buf, snap_size);
    }
  }

  printf("Wrote %d ticks of simulation.\n", demo_duration_ticks);

  demo_w_finish(writer);
  printf("Demo file finalized.\n");

  demo_w_destroy(&writer);
  demo_sb_destroy(&sb);

  printf("\nSuccessfully created '%s'.\n", demo_filename);

  return 0;
}