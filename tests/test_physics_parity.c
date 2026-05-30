#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#define DDNET_DEMO_IMPLEMENTATION
#include "ddnet_demo.h"

void test_basic_integration() {
    printf("Testing basic integration parity...\n");
    map_data_t map = {0};
    dd_phys_tuning tuning;
    dd_phys_init_tuning(&tuning);

    dd_phys_core core = {0};
    core.pos = (dd_vec2){100.0f, 100.0f};
    core.vel = (dd_vec2){10.0f, -5.0f};
    core.direction = 1;

    for (int i = 0; i < 100; i++) {
        dd_phys_tick(&core, &tuning, &map);
    }

    dd_netobj_character_core obj;
    dd_phys_core_write(&core, &obj);

    assert(obj.m_X == 581);
    assert(obj.m_Y == 2125);
    assert(obj.m_VelX == 1280);
    assert(obj.m_VelY == 11520);
    printf("  OK\n");
}

void test_movebox_actual() {
    printf("Testing ACTUAL MoveBox parity...\n");
    
    /* Create a minimal map with a floor */
    map_data_t map = {0};
    map.width = 10;
    map.height = 10;
    map.game_layer.data = (unsigned char *)calloc(100, 1);
    map.game_layer.flags = (unsigned char *)calloc(100, 1);
    
    /* Floor at Y=200 -> tile Y=6 (6*32=192, 7*32=224) */
    for(int x=0; x<10; x++) map.game_layer.data[7*10 + x] = TILE_SOLID;

    dd_vec2 pos = {100.0f, 190.0f};
    dd_vec2 vel = {5.0f, 20.0f};
    dd_vec2 size = {28.0f, 28.0f};
    dd_vec2 elasticity = {0.5f, 0.5f};
    bool grounded = false;

    /* Use the library's actual function */
    dd_col_move_box(&map, &pos, &vel, size, elasticity, &grounded);

    /* Verify bit-accurate results */
    unsigned int h_pos_x, h_pos_y, h_vel_x, h_vel_y;
    memcpy(&h_pos_x, &pos.x, 4);
    memcpy(&h_pos_y, &pos.y, 4);
    memcpy(&h_vel_x, &vel.x, 4);
    memcpy(&h_vel_y, &vel.y, 4);

    assert(h_pos_x == 0x42d20008); 
    assert(h_pos_y == 0x43510c2c);
    assert(h_vel_x == 0x40a00000);
    assert(h_vel_y == 0xc1200000);
    assert(grounded == true);

    free(map.game_layer.data);
    free(map.game_layer.flags);
    printf("  OK\n");
}

int main() {
    test_basic_integration();
    test_movebox_actual();
    printf("All physics parity tests passed!\n");
    return 0;
}
