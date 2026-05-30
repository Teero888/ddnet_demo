#include <stdio.h>
#include <assert.h>
#define DDNET_DEMO_IMPLEMENTATION
#include "ddnet_demo.h"

void test_open_invalid() {
    printf("Testing open invalid file...\n");
    FILE *f = fopen("nonexistent.demo", "rb");
    dd_demo_reader *dr = demo_r_create();
    assert(demo_r_open(dr, f) == false);
    demo_r_destroy(&dr);
    printf("  OK\n");
}

int main() {
    test_open_invalid();
    printf("All demo reader tests passed!\n");
    return 0;
}
