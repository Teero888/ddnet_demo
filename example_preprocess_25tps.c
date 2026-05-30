#include <stdio.h>
#define DDNET_DEMO_IMPLEMENTATION
#include "ddnet_demo.h"

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: %s <input_demo> <output_demo>\n", argv[0]);
        return 1;
    }

    /* Open input demo */
    FILE *f_in = fopen(argv[1], "rb");
    if (!f_in) {
        printf("Failed to open input file: %s\n", argv[1]);
        return 1;
    }

    dd_demo_reader *dr = demo_r_create();
    if (!demo_r_open(dr, f_in)) {
        printf("Failed to open input demo\n");
        return 1;
    }

    /* Enable automatic physics tracking (loads internal map automatically) */
    if (!demo_r_enable_phys(dr, NULL, NULL)) {
        printf("Failed to enable physics (is this a DDNet demo with embedded map?)\n");
        return 1;
    }

    /* Open output demo for writing */
    FILE *f_out = fopen(argv[2], "wb");
    if (!f_out) {
        printf("Failed to open output file: %s\n", argv[2]);
        return 1;
    }

    dd_demo_writer *dw = demo_w_create();
    dd_demo_info *info = demo_r_get_info(dr);
    demo_w_begin(dw, f_out, info->header.map_name, info->map_crc, info->header.type);

    /* Process and normalize to 25 TPS */
    dd_demo_chunk chunk;
    uint8_t unpacked_snap[DD_SNAPSHOT_MAX_SIZE];
    uint8_t phys_snap[DD_SNAPSHOT_MAX_SIZE];

    int last_out_tick = -1;

    while (demo_r_next_chunk(dr, &chunk)) {
        /* Unpack delta snapshots so they update physics state */
        if (chunk.type == DD_CHUNK_SNAP_DELTA) {
            demo_r_unpack_delta(dr, chunk.data, unpacked_snap);
        }

        /* Every 2 ticks (assuming 50 TPS source), emit a 25 TPS frame */
        if (chunk.tick % 2 == 0 && chunk.tick != last_out_tick) {
            /* Generate a snapshot containing ALL currently active characters
               at their dead-reckoned positions for THIS tick. */
            int snap_size = demo_r_get_phys_snap(dr, phys_snap);

            /* Write to new demo at 25 TPS (tick / 2) */
            demo_w_write_snap(dw, chunk.tick / 2, phys_snap, snap_size);
            last_out_tick = chunk.tick;
        }
    }

    demo_w_finish(dw);
    demo_r_destroy(&dr);
    demo_w_destroy(&dw);
    fclose(f_in);
    fclose(f_out);

    printf("Preprocessing complete. Output: %s\n", argv[2]);
    return 0;
}
