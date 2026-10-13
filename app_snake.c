/* app_snake.c */
#include "peach_api.h"

void app_snake() {
    int snake_x[100], snake_y[100];
    int snake_len = 3;
    snake_x[0] = 40; snake_y[0] = 12;
    snake_x[1] = 39; snake_y[1] = 12;
    snake_x[2] = 38; snake_y[2] = 12;
    
    int dir_x = 1, dir_y = 0;
    int apple_x = 20, apple_y = 10;
    int score = 0;
    
    while(1) {
        char k = get_key_nonblocking();
        if (k == 17 && dir_y == 0) { dir_x = 0; dir_y = -1; }
        if (k == 18 && dir_y == 0) { dir_x = 0; dir_y = 1; }
        if (k == 19 && dir_x == 0) { dir_x = -1; dir_y = 0; }
        if (k == 20 && dir_x == 0) { dir_x = 1; dir_y = 0; }
        if (k == 27) break; // ESC to exit
        
        // Move Body
        for(int i = snake_len - 1; i > 0; i--) {
            snake_x[i] = snake_x[i-1];
            snake_y[i] = snake_y[i-1];
        }
        snake_x[0] += dir_x;
        snake_y[0] += dir_y;
        
        // Wall Collision
        if (snake_x[0] < 0 || snake_x[0] >= VGA_COLS || snake_y[0] < 1 || snake_y[0] >= VGA_ROWS) break;
        
        // Self Collision
        int collision = 0;
        for(int i=1; i<snake_len; i++) if (snake_x[0] == snake_x[i] && snake_y[0] == snake_y[i]) collision = 1;
        if (collision) break;
        
        // Apple Collision
        if (snake_x[0] == apple_x && snake_y[0] == apple_y) {
            if (snake_len < 99) snake_len++;
            apple_x = sys_rand() % VGA_COLS;
            apple_y = (sys_rand() % (VGA_ROWS - 1)) + 1; // Keep apple off the score bar
            score += 10;
        }
        
        // Render Frame
        clear_screen();
        vga_fill_rect(0, 0, VGA_COLS, 1, ' ', 0x1F);
        vga_print_str(2, 0, "PEACH SNAKE  -  Score: ", 0x1F);
        
        char sc_buf[16]; memset(sc_buf, 0, 16); itoa(score, sc_buf, 10);
        vga_print_str(25, 0, sc_buf, 0x1F);
        
        vga_putc(apple_x, apple_y, 'O', 0x0C); // Apple
        
        for(int i=0; i<snake_len; i++) {
            vga_putc(snake_x[i], snake_y[i], i == 0 ? '@' : '*', 0x0A);
        }
        vga_swap();
        
        // Delay CPU to control speed
        for(volatile int i=0; i<900000; i++);
    }
    
    clear_screen();
    print("GAME OVER.\n\nFinal Score: "); print_int(score); print("\n\nPress Enter to exit.\n");
    while(get_key_blocking() != '\n');
}