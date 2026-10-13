/* app_sysmon.c */
#include "peach_api.h"

void app_sysmon() {
    uint8_t old_color = term_color;
    int update_tick = 0;

    while(1) {
        if (inb(0x64) & 1) { uint8_t k = inb(0x60); if (!(k & 0x80) && k == 0x01) break; }
        
        vga_fill_rect(0, 0, VGA_COLS, VGA_ROWS, ' ', 0x1F);
        vga_fill_rect(0, 0, VGA_COLS, 1, ' ', 0x70);
        vga_print_str(1, 0, "PEACH SYSTEM MONITOR (Live)", 0x70);
        vga_fill_rect(0, 24, VGA_COLS, 1, ' ', 0x70);
        vga_print_str(1, 24, "[ESC] Exit", 0x70);
        
        // Panel 1: CPU & System Time
        vga_draw_3d_box(2, 2, 36, 9, ' ', 0x7F);
        vga_print_str(14, 3, "CPU / SYS", 0x7C);
        
        char buf[64]; int i=0; uint64_t num = rdtsc();
        if (num==0){buf[0]='0';buf[1]='\0';} else {while(num!=0){buf[i++]=(num%10)+'0';num/=10;} buf[i]='\0'; reverse(buf,i);}
        vga_print_str(4, 5, "CYCLES:", 0x70); vga_print_str(12, 5, buf, 0x70);
        
        uint8_t h = bcd_to_bin(read_rtc(0x04)); uint8_t m = bcd_to_bin(read_rtc(0x02)); uint8_t s = bcd_to_bin(read_rtc(0x00));
        vga_print_str(4, 7, "UPTIME:", 0x70);
        itoa(h, buf, 10); vga_print_str(12, 7, buf, 0x70); vga_print_str(14, 7, ":", 0x70);
        itoa(m, buf, 10); if(m<10) { vga_print_str(15, 7, "0", 0x70); vga_print_str(16, 7, buf, 0x70); } else vga_print_str(15, 7, buf, 0x70);
        vga_print_str(17, 7, ":", 0x70);
        itoa(s, buf, 10); if(s<10) { vga_print_str(18, 7, "0", 0x70); vga_print_str(19, 7, buf, 0x70); } else vga_print_str(18, 7, buf, 0x70);

        vga_print_str(4, 9, "LOAD:", 0x70);
        int load = (sys_rand() % 15) + 5; // Simulated fluctuating bare-metal load
        for(int j=0; j<20; j++) { if (j < load) vga_putc(10+j, 9, 219, 0x72); else vga_putc(10+j, 9, 176, 0x78); }
        
        // Panel 2: Memory & FS
        vga_draw_3d_box(40, 2, 38, 9, ' ', 0x7F);
        vga_print_str(54, 3, "MEMORY / PFS", 0x7C);
        
        int used_fs = 0, files_alloc = 0, used_blks = 0;
        for (int j=0; j<MAX_FILES; j++) if (fs[j].in_use) { used_fs+=fs[j].size; files_alloc++; }
        for (int j=0; j<PFS_MAX_BLOCKS; j++) if (pfs_fat[j] != PFS_FREE) used_blks++;

        vga_print_str(42, 5, "INODES ALLOC:", 0x70);
        itoa(files_alloc, buf, 10); vga_print_str(57, 5, buf, 0x70); vga_print_str(60, 5, "/ 64", 0x70);
        
        vga_print_str(42, 7, "FAT BLOCKS  :", 0x70);
        itoa(used_blks, buf, 10); vga_print_str(57, 7, buf, 0x70); vga_print_str(60, 7, "/ 256", 0x70);
        
        vga_print_str(42, 9, "PAYLOAD SIZE:", 0x70);
        itoa(used_fs, buf, 10); vga_print_str(57, 9, buf, 0x70); vga_print_str(63, 9, "B", 0x70);

        // Panel 3: Disk Map
        vga_draw_3d_box(2, 13, 76, 8, ' ', 0x7F);
        vga_print_str(32, 14, "DISK ALLOCATION MAP", 0x7C);
        
        for(int j=0; j<256; j++) {
            int bx = 4 + (j % 72); int by = 16 + (j / 72);
            if (pfs_fat[j] != PFS_FREE) vga_putc(bx, by, 254, 0x72); // Green block in use
            else vga_putc(bx, by, 250, 0x78); // Grey dot free
        }

        vga_swap();
        for(volatile int x=0; x<300000; x++); // Delay for rendering frame (~5-10fps depending on CPU)
        update_tick++;
    }
    term_color = old_color; clear_screen();
}