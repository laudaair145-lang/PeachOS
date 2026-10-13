/* app_installer.c */
#include "peach_api.h"

extern void ata_wait_ready();
#define ATA_PRIMARY_CMD_STS 0x1F7

void draw_setup_bg() {
    vga_fill_rect(0, 0, VGA_COLS, VGA_ROWS, ' ', 0x1F); // White on Blue
    vga_fill_rect(0, 0, VGA_COLS, 1, ' ', 0x70); // Black on Light Gray header
    vga_print_str(2, 0, "PeachOS Setup v0.1", 0x70);
    vga_fill_rect(0, VGA_ROWS - 1, VGA_COLS, 1, ' ', 0x70);
    vga_print_str(2, VGA_ROWS - 1, "ENTER=Continue  ESC=Cancel/Reboot", 0x70);
}

void app_installer() {
    term_color = 0x1F;
    clear_screen();
    
    // WELCOME SCREEN
    draw_setup_bg();
    vga_draw_3d_box(15, 6, 50, 10, ' ', 0x7F);
    vga_print_str(17, 7, "Welcome to PeachOS Setup", 0x7E);
    vga_print_str(17, 9,  "This wizard will install PeachOS v0.1", 0x7F);
    vga_print_str(17, 10, "onto your primary master hard drive.", 0x7F);
    vga_print_str(17, 12, "WARNING: All data on the drive will", 0x7C); // Red text
    vga_print_str(17, 13, "be permanently destroyed.", 0x7C);
    vga_swap();
    
    char k = get_key_blocking();
    if (k == 27) { outb(0x64, 0xFE); while(1) __asm__ volatile ("hlt"); } // Reboot
    
    // HARDWARE CHECK
    draw_setup_bg();
    vga_draw_3d_box(15, 6, 50, 10, ' ', 0x7F);
    vga_print_str(17, 7, "Detecting Hardware...", 0x7E);
    vga_swap();
    
    uint16_t id_data[256];
    if (!ata_identify(id_data)) {
        vga_print_str(17, 9, "ERROR: No compatible IDE drive found.", 0x7C);
        vga_print_str(17, 11, "Setup cannot continue. Press ENTER.", 0x7F);
        vga_swap();
        while(get_key_blocking() != '\n');
        outb(0x64, 0xFE); while(1) __asm__ volatile ("hlt");
    }
    
    uint32_t sectors = id_data[60] | ((uint32_t)id_data[61] << 16);
    uint32_t size_mb = (sectors / 2) / 1024;
    
    char size_str[16]; memset(size_str, 0, 16); itoa(size_mb, size_str, 10);
    vga_print_str(17, 9, "Drive found: ", 0x7F);
    vga_print_str(30, 9, size_str, 0x7A);
    vga_print_str(35, 9, "MB Capacity", 0x7A);
    vga_print_str(17, 14, "Press ENTER to begin format...", 0x7F);
    vga_swap();
    
    if (get_key_blocking() == 27) { outb(0x64, 0xFE); while(1) __asm__ volatile ("hlt"); }
    
    // FORMATTING
    draw_setup_bg();
    vga_draw_3d_box(15, 6, 50, 10, ' ', 0x7F);
    vga_print_str(17, 7, "Installing PeachOS v0.1...", 0x7E);
    
    vga_print_str(17, 9, "[1/3] Writing Master Boot Record...", 0x7F);
    vga_swap();
    uint8_t mbr[512]; memset(mbr, 0, 512);
    mbr[510] = 0x55; mbr[511] = 0xAA;
    ata_write_sector(0, mbr);
    for(volatile int i=0; i<5000000; i++); // Fake delay for UX
    
    vga_print_str(17, 10, "[2/3] Zero-filling partition...", 0x7F);
    vga_swap();
    uint8_t zeros[512]; memset(zeros, 0, 512);
    for (int i = 0; i < 300; i++) ata_write_sector(FS_START_LBA + i, zeros);
    ata_wait_ready(); outb(ATA_PRIMARY_CMD_STS, 0xE7); ata_wait_ready(); // Flush
    
    vga_print_str(17, 11, "[3/3] Initializing File System...", 0x7F);
    vga_swap();
    fs_init();
    sys_savefs(); // Actually writes PFS_MAGIC to disk
    
    // DONE
    draw_setup_bg();
    vga_draw_3d_box(15, 6, 50, 8, ' ', 0x7F);
    vga_print_str(17, 7, "Installation Complete!", 0x7A);
    vga_print_str(17, 9, "PeachOS v0.1 has been installed.", 0x7F);
    vga_print_str(17, 11, "Press ENTER to reboot system.", 0x7F);
    vga_swap();
    
    while(get_key_blocking() != '\n');
    outb(0x64, 0xFE); while(1) __asm__ volatile ("hlt");
}