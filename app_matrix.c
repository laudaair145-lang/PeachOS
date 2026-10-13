/* app_matrix.c */
#include "peach_api.h"

void app_matrix() {
    uint8_t old_color = term_color; update_cursor(80, 25);
    int tail[80]; for(int x = 0; x < 80; x++) tail[x] = -(sys_rand() % 40);
    while(1) {
        for(volatile int i=0; i<500000; i++) { if (inb(0x64) & 1) { inb(0x60); goto end_matrix; } }
        for(int x = 0; x < 80; x++) {
            int y = tail[x];
            if (y >= 0 && y < 25) vga_putc(x, y, (sys_rand() % 94) + 33, 0x0A);
            if (y - 1 >= 0 && y - 1 < 25) vga_putc(x, y - 1, (sys_rand() % 94) + 33, 0x02);
            if (y - 6 >= 0 && y - 6 < 25) vga_putc(x, y - 6, ' ', 0x00);
            tail[x]++; if (tail[x] - 6 > 25 + (sys_rand() % 10)) tail[x] = -(sys_rand() % 20);
        }
        vga_swap();
    }
end_matrix:
    term_color = old_color; clear_screen();
}