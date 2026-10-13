/* kernel.c */
#include "peach_api.h"

/* ========================================================
   API GLOBALS
   ======================================================== */
const int VGA_COLS = 80;
const int VGA_ROWS = 25;
volatile uint16_t* vga_buffer = (uint16_t*)0xB8000;
uint16_t back_buffer[80 * 25];

int term_col = 0;
int term_row = 0;
uint8_t term_color = 0x0F;

PFS_Inode fs[MAX_FILES];
uint16_t pfs_fat[PFS_MAX_BLOCKS];
uint8_t pfs_data[PFS_MAX_BLOCKS][PFS_BLOCK_SIZE];
int current_dir_id = -1; 

static uint32_t sys_seed = 9999;

/* ========================================================
   STRING & MATH UTILITIES
   ======================================================== */
void* memset(void* dest, int val, size_t count) {
    uint8_t* ptr = (uint8_t*)dest;
    for (size_t i = 0; i < count; i++) ptr[i] = (uint8_t)val;
    return dest;
}
void* memcpy(void* dest, const void* src, size_t count) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < count; i++) d[i] = s[i];
    return dest;
}
size_t strlen(const char* str) { size_t len=0; while(str[len]) len++; return len; }
int strcmp(const char* a, const char* b) {
    while (*a && (*a == *b)) { a++; b++; }
    return *(const unsigned char*)a - *(const unsigned char*)b;
}
int strncmp(const char* a, const char* b, size_t n) {
    while (n && *a && (*a == *b)) { a++; b++; n--; }
    if (n == 0) return 0;
    return *(const unsigned char*)a - *(const unsigned char*)b;
}
void strcpy(char* dest, const char* src) { while ((*dest++ = *src++)); }
void strcat(char* dest, const char* src) {
    while (*dest) dest++;
    while ((*dest++ = *src++));
}
int atoi(const char* str) {
    int res = 0, sign = 1, i = 0;
    if (str[0] == '-') { sign = -1; i++; }
    for (; str[i] != '\0' && str[i] >= '0' && str[i] <= '9'; ++i) res = res * 10 + str[i] - '0';
    return sign * res;
}
int hex_char_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
void reverse(char* str, int length) {
    int start = 0, end = length - 1;
    while (start < end) { char temp = str[start]; str[start] = str[end]; str[end] = temp; start++; end--; }
}
void itoa(int num, char* str, int base) {
    int i = 0, isNegative = 0;
    if (num == 0) { str[i++] = '0'; str[i] = '\0'; return; }
    if (num < 0 && base == 10) { isNegative = 1; num = -num; }
    while (num != 0) {
        int rem = num % base; str[i++] = (rem > 9) ? (rem - 10) + 'A' : rem + '0'; num = num / base;
    }
    if (isNegative) str[i++] = '-';
    str[i] = '\0'; reverse(str, i);
}
uint32_t sys_rand() { sys_seed = sys_seed * 1103515245 + 12345; return (sys_seed / 65536) % 32768; }

/* ========================================================
   HARDWARE I/O
   ======================================================== */
static inline uint16_t inw(uint16_t port) {
    uint16_t ret; __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port)); return ret;
}
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}
void update_cursor(int x, int y) {
    uint16_t pos = y * VGA_COLS + x;
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}
uint8_t read_rtc(int reg) { outb(0x70, reg); return inb(0x71); }
uint8_t bcd_to_bin(uint8_t bcd) { return ((bcd / 16) * 10) + (bcd & 0x0F); }

void vga_set_palette(uint8_t index, uint8_t r, uint8_t g, uint8_t b) { outb(0x3C8, index); outb(0x3C9, r); outb(0x3C9, g); outb(0x3C9, b); }
void restore_default_palette() {
    vga_set_palette(0, 0, 0, 0); vga_set_palette(1, 0, 0, 42); vga_set_palette(2, 0, 42, 0); vga_set_palette(3, 0, 42, 42);
    vga_set_palette(4, 42, 0, 0); vga_set_palette(5, 42, 0, 42); vga_set_palette(6, 42, 21, 0); vga_set_palette(7, 42, 42, 42);
    vga_set_palette(8, 21, 21, 21); vga_set_palette(9, 21, 21, 63); vga_set_palette(10, 21, 63, 21); vga_set_palette(11, 21, 63, 63);
    vga_set_palette(12, 63, 21, 21); vga_set_palette(13, 63, 21, 63); vga_set_palette(14, 63, 63, 21); vga_set_palette(15, 63, 63, 63);
}

/* ========================================================
   ATA PIO DISK DRIVER
   ======================================================== */
#define ATA_PRIMARY_DATA         0x1F0
#define ATA_PRIMARY_ERR_FEAT     0x1F1
#define ATA_PRIMARY_SEC_COUNT    0x1F2
#define ATA_PRIMARY_LBA_LO       0x1F3
#define ATA_PRIMARY_LBA_MID      0x1F4
#define ATA_PRIMARY_LBA_HI       0x1F5
#define ATA_PRIMARY_DRV_HEAD     0x1F6
#define ATA_PRIMARY_CMD_STS      0x1F7
#define ATA_PRIMARY_ALT_STS      0x3F6

void ata_io_wait() { inb(ATA_PRIMARY_ALT_STS); inb(ATA_PRIMARY_ALT_STS); inb(ATA_PRIMARY_ALT_STS); inb(ATA_PRIMARY_ALT_STS); }
void ata_wait_ready() { ata_io_wait(); while ((inb(ATA_PRIMARY_CMD_STS) & 0x80) == 0x80); while ((inb(ATA_PRIMARY_CMD_STS) & 0x40) == 0x00); }

int ata_identify(uint16_t* buffer) {
    outb(ATA_PRIMARY_DRV_HEAD, 0xA0); outb(ATA_PRIMARY_SEC_COUNT, 0); outb(ATA_PRIMARY_LBA_LO, 0);
    outb(ATA_PRIMARY_LBA_MID, 0); outb(ATA_PRIMARY_LBA_HI, 0); outb(ATA_PRIMARY_CMD_STS, 0xEC); 
    if (inb(ATA_PRIMARY_CMD_STS) == 0) return 0; 
    while ((inb(ATA_PRIMARY_CMD_STS) & 0x80) != 0); 
    if (inb(ATA_PRIMARY_LBA_MID) != 0 || inb(ATA_PRIMARY_LBA_HI) != 0) return 0; 
    while ((inb(ATA_PRIMARY_CMD_STS) & 0x08) == 0 && (inb(ATA_PRIMARY_CMD_STS) & 0x01) == 0);
    if (inb(ATA_PRIMARY_CMD_STS) & 0x01) return 0; 
    for (int i = 0; i < 256; i++) { uint16_t data = inw(ATA_PRIMARY_DATA); if (buffer) buffer[i] = data; }
    return 1;
}

void ata_read_sector(uint32_t lba, uint8_t* buffer) {
    ata_wait_ready();
    outb(ATA_PRIMARY_DRV_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    ata_io_wait();
    
    outb(ATA_PRIMARY_SEC_COUNT, 1);
    outb(ATA_PRIMARY_LBA_LO, (uint8_t)lba);
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_LBA_HI, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_CMD_STS, 0x20); // Read Sectors command
    
    ata_io_wait();
    while ((inb(ATA_PRIMARY_CMD_STS) & 0x80) != 0);
    while ((inb(ATA_PRIMARY_CMD_STS) & 0x08) == 0); 
    
    for (int i = 0; i < 256; i++) {
        uint16_t word = inw(ATA_PRIMARY_DATA);
        buffer[i * 2] = word & 0xFF;
        buffer[i * 2 + 1] = (word >> 8) & 0xFF;
    }
    
    ata_io_wait();
    ata_wait_ready();
}

void ata_write_sector(uint32_t lba, uint8_t* buffer) {
    ata_wait_ready();
    outb(ATA_PRIMARY_DRV_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    ata_io_wait();
    
    outb(ATA_PRIMARY_SEC_COUNT, 1); 
    outb(ATA_PRIMARY_LBA_LO, (uint8_t)lba);
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)(lba >> 8)); 
    outb(ATA_PRIMARY_LBA_HI, (uint8_t)(lba >> 16)); 
    outb(ATA_PRIMARY_CMD_STS, 0x30); // Write Sectors command
    
    ata_io_wait(); 
    while ((inb(ATA_PRIMARY_CMD_STS) & 0x80) != 0); 
    while ((inb(ATA_PRIMARY_CMD_STS) & 0x08) == 0); 
    
    for (int i = 0; i < 256; i++) {
        uint16_t word = buffer[i * 2] | ((uint16_t)buffer[i * 2 + 1] << 8);
        outw(ATA_PRIMARY_DATA, word);
    }
    
    ata_io_wait();
    ata_wait_ready(); 
}

/* ========================================================
   GRAPHICS API
   ======================================================== */
void vga_putc(int x, int y, char c, uint8_t color) { if (x >= 0 && x < VGA_COLS && y >= 0 && y < VGA_ROWS) back_buffer[y * VGA_COLS + x] = (uint16_t)c | ((uint16_t)color << 8); }
void vga_print_str(int x, int y, const char* str, uint8_t color) { int cx = x; while (*str && cx < VGA_COLS) vga_putc(cx++, y, *str++, color); }
void vga_print_str_ext(int* x, int y, const char* str, uint8_t color) { while (*str && *x < VGA_COLS) { vga_putc(*x, y, *str++, color); (*x)++; } }
void vga_fill_rect(int x, int y, int w, int h, char c, uint8_t color) { for (int iy = y; iy < y + h; iy++) for (int ix = x; ix < x + w; ix++) vga_putc(ix, iy, c, color); }
void vga_draw_3d_box(int x, int y, int w, int h, char c, uint8_t color) {
    for (int iy = y + 1; iy < y + h + 1; iy++) for (int ix = x + 1; ix < x + w + 1; ix++) vga_putc(ix, iy, 176, 0x03);
    vga_fill_rect(x, y, w, h, c, color);
}
void vga_swap() { for (int i = 0; i < VGA_COLS * VGA_ROWS; i++) if (vga_buffer[i] != back_buffer[i]) vga_buffer[i] = back_buffer[i]; }
void clear_screen() { vga_fill_rect(0, 0, VGA_COLS, VGA_ROWS, ' ', term_color); vga_swap(); term_col = 0; term_row = 0; update_cursor(0, 0); }

void print_char(char c) {
    if (c == '\n') { term_col = 0; term_row++; } else { vga_putc(term_col, term_row, c, term_color); term_col++; if (term_col >= VGA_COLS) { term_col = 0; term_row++; } }
    if (term_row >= VGA_ROWS) {
        for (int y = 1; y < VGA_ROWS; y++) for (int x = 0; x < VGA_COLS; x++) back_buffer[(y - 1) * VGA_COLS + x] = back_buffer[y * VGA_COLS + x];
        vga_fill_rect(0, VGA_ROWS - 1, VGA_COLS, 1, ' ', term_color); term_row = VGA_ROWS - 1;
    }
    vga_swap(); update_cursor(term_col, term_row);
}
void print(const char* str) { for (size_t i = 0; str[i] != '\0'; i++) print_char(str[i]); }
void print_int(int num) { char buf[32]; memset(buf, 0, 32); itoa(num, buf, 10); print(buf); }
void print_hex8(uint8_t val) { char hex[] = "0123456789ABCDEF"; print_char(hex[val >> 4]); print_char(hex[val & 0x0F]); }
void print_long(uint64_t num) { char buf[64]; memset(buf, 0, 64); int i = 0; if (num == 0) { print("0"); return; } while (num != 0) { buf[i++] = (num % 10) + '0'; num /= 10; } buf[i] = '\0'; reverse(buf, i); print(buf); }

/* ========================================================
   KEYBOARD & GUI
   ======================================================== */
const char kbd_US[128] = { 0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b', '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0, 'a','s','d','f','g','h','j','k','l',';','\'','`', 0, '\\','z','x','c','v','b','n','m',',','.','/',0, '*',0,' ',0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, '-',0,0,0,'+',0,0,0,0,0,0,0,0,0,0,0 };
const char kbd_US_shift[128] = { 0, 27, '!','@','#','$','%','^','&','*','(',')','_','+','\b', '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0, 'A','S','D','F','G','H','J','K','L',':','"','~', 0, '|','Z','X','C','V','B','N','M','<','>','?',0, '*',0,' ',0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, '_',0,0,0,'+',0,0,0,0,0,0,0,0,0,0,0 };

char get_key_blocking() {
    static int shift = 0;
    while(1) {
        while (!(inb(0x64) & 1)); uint8_t scancode = inb(0x60);
        if (scancode == 0xE0) continue; if (scancode == 0x2A || scancode == 0x36) { shift = 1; continue; } if (scancode == 0xAA || scancode == 0xB6) { shift = 0; continue; }
        if (!(scancode & 0x80)) { if (scancode == 0x48) return 17; if (scancode == 0x50) return 18; if (scancode == 0x4B) return 19; if (scancode == 0x4D) return 20; if (scancode == 0x01) return 27; return shift ? kbd_US_shift[scancode] : kbd_US[scancode]; }
    }
}
char get_key_nonblocking() {
    if (inb(0x64) & 1) {
        uint8_t scancode = inb(0x60);
        static int shift = 0;
        if (scancode == 0xE0) return 0;
        if (scancode == 0x2A || scancode == 0x36) { shift = 1; return 0; }
        if (scancode == 0xAA || scancode == 0xB6) { shift = 0; return 0; }
        if (!(scancode & 0x80)) { 
            if (scancode == 0x48) return 17; // Up Arrow
            if (scancode == 0x50) return 18; // Down Arrow
            if (scancode == 0x4B) return 19; // Left Arrow
            if (scancode == 0x4D) return 20; // Right Arrow
            if (scancode == 0x01) return 27; // ESC
            return shift ? kbd_US_shift[scancode] : kbd_US[scancode];
        }
    }
    return 0;
}
void get_input(const char* prompt_str, char* buffer, int max_len) {
    print(prompt_str); int pos = 0;
    while(1) {
        char c = get_key_blocking();
        if (c == '\n') { buffer[pos] = '\0'; print("\n"); break; } 
        else if (c == '\b') { if (pos > 0) { pos--; term_col--; if (term_col < 0) { if (term_row > 0) { term_row--; term_col = VGA_COLS - 1; } else { term_col = 0; } } vga_putc(term_col, term_row, ' ', term_color); vga_swap(); update_cursor(term_col, term_row); } } 
        else if (c >= 32 && c <= 126 && pos < max_len - 1) { buffer[pos++] = c; print_char(c); }
    }
}
int gui_menu(const char* title, const char* opts[], int num_opts, const char* status_bar) {
    int sel = 0;
    while(1) {
        int box_h = 4 + (num_opts * 2); 
        vga_draw_3d_box(26, 5, 30, box_h, ' ', 0x7F); 
        vga_print_str(30, 6, title, 0x7E);
        for(int i = 0; i < num_opts; i++) {
            uint8_t col = (i == sel) ? 0x0F : 0x7F;
            vga_fill_rect(28, 8 + (i * 2), 26, 1, ' ', col); vga_print_str(30, 8 + (i * 2), opts[i], col);
        }
        if (status_bar) { vga_fill_rect(0, 24, VGA_COLS, 1, ' ', 0x3F); vga_print_str(2, 24, status_bar, 0x3F); }
        vga_swap(); char k = get_key_blocking();
        if (k == 17 && sel > 0) sel--; if (k == 18 && sel < num_opts - 1) sel++; if (k == '\n') return sel; if (k == 27) return -1;
    }
}
void gui_msgbox(const char* title, const char* msg) {
    vga_draw_3d_box(18, 10, 44, 9, ' ', 0x7F); vga_print_str(20, 11, title, 0x7E); 
    int lx = 20, ly = 13;
    for (size_t i = 0; msg[i] != '\0'; i++) {
        if (msg[i] == '\n' || lx >= 60) { lx = 20; ly++; }
        if (msg[i] != '\n') { vga_putc(lx, ly, msg[i], 0x7F); lx++; }
    }
    vga_print_str(20, 17, "[Press Enter]", 0x7B); vga_swap(); while(get_key_blocking() != '\n');
}

/* ========================================================
   PEACH FILE SYSTEM (PFS v1.5)
   ======================================================== */
void fs_init() { 
    for (int i = 0; i < MAX_FILES; i++) { fs[i].id = i; fs[i].in_use = 0; fs[i].start_block = PFS_FREE; }
    for (int i = 0; i < PFS_MAX_BLOCKS; i++) pfs_fat[i] = PFS_FREE;
}

uint16_t pfs_alloc_block() {
    for (int i=0; i<PFS_MAX_BLOCKS; i++) { if (pfs_fat[i] == PFS_FREE) { pfs_fat[i] = PFS_EOF; return i; } }
    return PFS_FREE;
}

void pfs_free_chain(uint16_t start_block) {
    uint16_t current = start_block;
    while(current != PFS_EOF && current != PFS_FREE) { uint16_t next = pfs_fat[current]; pfs_fat[current] = PFS_FREE; current = next; }
}

PFS_Inode* fs_find_in(const char* name, int parent) { 
    for (int i = 0; i < MAX_FILES; i++) if (fs[i].in_use && fs[i].parent_id == parent && strcmp(fs[i].name, name) == 0) return &fs[i]; 
    return NULL; 
}

PFS_Inode* fs_create_in(const char* name, int is_dir, int parent) { 
    PFS_Inode* existing = fs_find_in(name, parent); if (existing) return existing; 
    for (int i = 0; i < MAX_FILES; i++) { 
        if (!fs[i].in_use) { 
            fs[i].in_use = 1; fs[i].is_dir = is_dir; fs[i].parent_id = parent; 
            strcpy(fs[i].name, name); fs[i].size = 0; fs[i].start_block = PFS_FREE;
            
            // Record Timestamp dynamically from RTC
            fs[i].c_d = bcd_to_bin(read_rtc(0x07)); fs[i].c_m = bcd_to_bin(read_rtc(0x08)); fs[i].c_y = bcd_to_bin(read_rtc(0x09));
            fs[i].c_h = bcd_to_bin(read_rtc(0x04)); fs[i].c_min = bcd_to_bin(read_rtc(0x02));
            fs[i].m_d = fs[i].c_d; fs[i].m_m = fs[i].c_m; fs[i].m_y = fs[i].c_y;
            fs[i].m_h = fs[i].c_h; fs[i].m_min = fs[i].c_min;
            return &fs[i]; 
        } 
    } 
    return NULL; 
}

void fs_delete(int id) { 
    if (id >= 0 && id < MAX_FILES && fs[id].in_use) { pfs_free_chain(fs[id].start_block); fs[id].in_use = 0; } 
}

PFS_Inode* fs_get(int id) { if (id >= 0 && id < MAX_FILES && fs[id].in_use) return &fs[id]; return NULL; }

int pfs_read_file(int inode_id, char* buffer, int max_len) {
    PFS_Inode* node = &fs[inode_id]; if (!node->in_use || node->is_dir) return 0;
    int bytes_read = 0; uint16_t current = node->start_block;
    while(current != PFS_EOF && current != PFS_FREE && bytes_read < node->size && bytes_read < max_len) {
        int to_copy = node->size - bytes_read; if (to_copy > PFS_BLOCK_SIZE) to_copy = PFS_BLOCK_SIZE;
        if (bytes_read + to_copy > max_len) to_copy = max_len - bytes_read;
        for(int i=0; i<to_copy; i++) buffer[bytes_read+i] = pfs_data[current][i];
        bytes_read += to_copy; current = pfs_fat[current];
    }
    return bytes_read;
}

int pfs_write_file(int inode_id, const char* buffer, int size) {
    PFS_Inode* node = &fs[inode_id]; if (!node->in_use || node->is_dir) return 0;
    pfs_free_chain(node->start_block); node->size = size;
    
    // Update Modification Timestamp
    node->m_d = bcd_to_bin(read_rtc(0x07)); node->m_m = bcd_to_bin(read_rtc(0x08)); node->m_y = bcd_to_bin(read_rtc(0x09));
    node->m_h = bcd_to_bin(read_rtc(0x04)); node->m_min = bcd_to_bin(read_rtc(0x02));
    
    if (size == 0) { node->start_block = PFS_FREE; return 1; }
    
    uint16_t first = pfs_alloc_block(); if (first == PFS_FREE) return 0;
    node->start_block = first; int written = 0; uint16_t current = first;
    
    while (written < size) {
        int to_copy = size - written; if (to_copy > PFS_BLOCK_SIZE) to_copy = PFS_BLOCK_SIZE;
        for(int i=0; i<to_copy; i++) pfs_data[current][i] = buffer[written+i];
        written += to_copy;
        if (written < size) {
            uint16_t next = pfs_alloc_block(); if (next == PFS_FREE) return 0;
            pfs_fat[current] = next; current = next;
        }
    }
    return 1;
}

void pfs_copy_file(int src_id, int dst_id) {
    PFS_Inode* src = &fs[src_id]; PFS_Inode* dst = &fs[dst_id];
    dst->size = 0; dst->start_block = PFS_FREE; if (src->size == 0) return;
    
    uint16_t src_blk = src->start_block; uint16_t prev_dst = PFS_FREE;
    while(src_blk != PFS_EOF && src_blk != PFS_FREE) {
        uint16_t new_blk = pfs_alloc_block(); if (new_blk == PFS_FREE) break; 
        for(int i=0; i<PFS_BLOCK_SIZE; i++) pfs_data[new_blk][i] = pfs_data[src_blk][i];
        if (prev_dst == PFS_FREE) dst->start_block = new_blk; else pfs_fat[prev_dst] = new_blk;
        prev_dst = new_blk; dst->size += PFS_BLOCK_SIZE; src_blk = pfs_fat[src_blk];
    }
    dst->size = src->size; 
}

/* ========================================================
   SYSTEM MANAGEMENT SERVICES
   ======================================================== */
void sys_diskinfo() {
    print("--- EXTENDED ATA DISK DIAGNOSTICS ---\n");
    uint16_t id_data[256];
    if (ata_identify(id_data)) {
        print("Status   : Primary Master Drive DETECTED\n");
        print("Model    : ");
        for (int i = 27; i <= 46; i++) {
            uint16_t w = id_data[i]; char c1 = (w >> 8) & 0xFF; char c2 = w & 0xFF;
            if (c1 >= 32 && c1 <= 126) print_char(c1); if (c2 >= 32 && c2 <= 126) print_char(c2);
        }
        print("\nSectors  : "); print_long(id_data[60] | ((uint32_t)id_data[61] << 16)); print("\n");
        print("PFS LBA  : "); print_int(FS_START_LBA); print("\n");
    } else { print("Status: No compatible ATA drive detected.\n"); }
}

void sys_fsinfo() {
    uint16_t id_data[256];
    if (!ata_identify(id_data)) {
        print("Disk Error: No QEMU/ATA drive detected.\n");
        return;
    }
    
    uint8_t magic_buf[512];
    memset(magic_buf, 0, 512);
    ata_read_sector(FS_START_LBA, magic_buf);
    
    uint32_t read_magic = ((uint32_t)magic_buf[3] << 24) | ((uint32_t)magic_buf[2] << 16) | ((uint32_t)magic_buf[1] << 8) | magic_buf[0];
    
    print("--- VOLUME INFORMATION ---\n");
    if (read_magic == PFS_MAGIC) {
        uint8_t tc = term_color; term_color = 0x0A; 
        print("File System : Peach File System (PFS v1.5)\n"); 
        term_color = tc;
        print("Status      : Initialized and Valid\n");
    } else {
        uint8_t tc = term_color; term_color = 0x0C; 
        print("File System : Unknown / Raw / Unallocated\n"); 
        term_color = tc;
        print("Status      : Uninitialized (Use 'install' or 'format')\n");
    }
    
    uint32_t sectors = id_data[60] | ((uint32_t)id_data[61] << 16);
    uint32_t size_kb = sectors / 2;
    uint32_t size_mb = size_kb / 1024;
    
    print("Capacity    : ");
    if (size_mb > 0) { print_int(size_mb); print(" MB ("); print_int(size_kb); print(" KB)\n"); }
    else { print_int(size_kb); print(" KB\n"); }
    print("Start LBA   : "); print_int(FS_START_LBA); print("\n");
}

void sys_savefs() {
    print("Serializing PFS Metadata to Disk (LBA "); print_int(FS_START_LBA); print(")...\n");
    uint32_t lba = FS_START_LBA;
    
    uint8_t magic_buf[512];
    memset(magic_buf, 0, 512);
    magic_buf[0] = (PFS_MAGIC & 0xFF); magic_buf[1] = (PFS_MAGIC >> 8) & 0xFF;
    magic_buf[2] = (PFS_MAGIC >> 16) & 0xFF; magic_buf[3] = (PFS_MAGIC >> 24) & 0xFF;
    ata_write_sector(lba++, magic_buf);
    
    int num_in_sec = (sizeof(fs) + 511) / 512; for(int i=0; i<num_in_sec; i++) ata_write_sector(lba++, ((uint8_t*)fs) + (i * 512));
    int num_fat_sec = (sizeof(pfs_fat) + 511) / 512; for(int i=0; i<num_fat_sec; i++) ata_write_sector(lba++, ((uint8_t*)pfs_fat) + (i * 512));
    
    for(int i=0; i<PFS_MAX_BLOCKS; i++) ata_write_sector(lba++, pfs_data[i]);
    print("PFS Saved Successfully.\n");

    ata_wait_ready(); outb(ATA_PRIMARY_CMD_STS, 0xE7); ata_wait_ready();
}

void sys_loadfs() {
    uint32_t lba = FS_START_LBA;
    uint8_t magic_buf[512]; 
    memset(magic_buf, 0, 512);
    ata_read_sector(lba++, magic_buf);
    
    uint32_t read_magic = ((uint32_t)magic_buf[3] << 24) | ((uint32_t)magic_buf[2] << 16) | ((uint32_t)magic_buf[1] << 8) | magic_buf[0];
    if (read_magic != PFS_MAGIC) return; // Silent fail for auto-mounting
    
    int num_in_sec = (sizeof(fs) + 511) / 512; for(int i=0; i<num_in_sec; i++) ata_read_sector(lba++, ((uint8_t*)fs) + (i * 512));
    int num_fat_sec = (sizeof(pfs_fat) + 511) / 512; for(int i=0; i<num_fat_sec; i++) ata_read_sector(lba++, ((uint8_t*)pfs_fat) + (i * 512));
    for(int i=0; i<PFS_MAX_BLOCKS; i++) ata_read_sector(lba++, pfs_data[i]);
}

void print_peachfetch() {
    uint8_t tc = term_color; print("\n");
    term_color = 0x0C; print("    ,--./,-.    "); term_color = tc; print(" OS      : Peach OS v0.1\n");
    term_color = 0x0C; print("   / #      \\   "); term_color = tc; print(" Kernel  : Isolated Architecture\n");
    term_color = 0x0C; print("  |          |  "); term_color = tc; print(" Shell   : PeachShell Double Bfr\n");
    term_color = 0x0C; print("   \\        /   "); term_color = tc; print(" FileSys : PFS v1.5 Automount\n");
    term_color = 0x0E; print("    `._,._,'    "); term_color = tc; print(" Apps    : BF Compiler, HexViewer\n\n");
}

/* ========================================================
   COMMAND PARSING & SHELL
   ======================================================== */
char* get_arg(char* cmd, int arg_num) {
    int current_arg = 0; char* ptr = cmd;
    while (*ptr) {
        if (*ptr != ' ' && (ptr == cmd || *(ptr - 1) == ' ' || *(ptr - 1) == '\0')) {
            if (current_arg == arg_num) return ptr;
            current_arg++;
        }
        ptr++;
    }
    return NULL;
}

void execute_command(char* cmd) {
    if (cmd[0] == '\0') return;
    char* base_cmd = get_arg(cmd, 0); if (!base_cmd) return;

    if (strncmp(base_cmd, "explorer", 8) == 0) app_explorer();
    else if (strncmp(base_cmd, "edit", 4) == 0) app_editor(get_arg(cmd, 1));
    else if (strncmp(base_cmd, "sysmon", 6) == 0 || strncmp(base_cmd, "top", 3) == 0) app_sysmon();
    else if (strncmp(base_cmd, "diskutil", 8) == 0) app_diskutil();
    else if (strncmp(base_cmd, "matrix", 6) == 0) app_matrix();
    else if (strncmp(base_cmd, "bf", 2) == 0) app_bf(get_arg(cmd, 1));
    else if (strncmp(base_cmd, "hexview", 7) == 0) app_hexview();
    else if (strncmp(base_cmd, "install", 7) == 0) app_installer();
    else if (strncmp(base_cmd, "diskinfo", 8) == 0) sys_diskinfo();
    else if (strncmp(base_cmd, "fsinfo", 6) == 0) sys_fsinfo();
    else if (strncmp(base_cmd, "savefs", 6) == 0) sys_savefs();
    else if (strncmp(base_cmd, "mkdir", 5) == 0) {
        char* dname = get_arg(cmd, 1);
        if (dname) { if (fs_create_in(dname, 1, current_dir_id)) print("Directory created.\n"); else print("PFS Inodes Full.\n"); } else print("Usage: mkdir <name>\n");
    }
    else if (strncmp(base_cmd, "cd", 2) == 0) {
        char* dname = get_arg(cmd, 1);
        if (!dname) print("Usage: cd <name> or cd ..\n");
        else if (strcmp(dname, "..") == 0) { if (current_dir_id != -1) current_dir_id = ((PFS_Inode*)fs_get(current_dir_id))->parent_id; } 
        else { PFS_Inode* d = fs_find_in(dname, current_dir_id); if (d && d->is_dir) current_dir_id = d->id; else print("Directory not found.\n"); }
    }
    else if (strncmp(base_cmd, "ls", 2) == 0) {
        print("Name            Type   Size   Date       Time\n-------------------------------------------------\n");
        int count = 0;
        for (int i = 0; i < MAX_FILES; i++) {
            if (fs[i].in_use && fs[i].parent_id == current_dir_id) {
                print(fs[i].name); for(int p = 0; p < 16 - (int)strlen(fs[i].name); p++) print(" ");
                if (fs[i].is_dir) print("<DIR>  -      "); else { 
                    print("FILE   "); print_int(fs[i].size); 
                    for(int p=0; p < 7 - (fs[i].size > 9 ? (fs[i].size > 99 ? 3 : 2) : 1); p++) print(" "); 
                }
                if (fs[i].m_d < 10) print("0"); print_int(fs[i].m_d); print("/");
                if (fs[i].m_m < 10) print("0"); print_int(fs[i].m_m); print("/");
                print_int(fs[i].m_y); print("   ");
                if (fs[i].m_h < 10) print("0"); print_int(fs[i].m_h); print(":");
                if (fs[i].m_min < 10) print("0"); print_int(fs[i].m_min); print("\n");
                count++;
            }
        }
        if (count == 0) print("Folder empty.\n");
    }
    else if (strncmp(base_cmd, "rm", 2) == 0) {
        char* filename = get_arg(cmd, 1);
        if (filename) { PFS_Inode* f = fs_find_in(filename, current_dir_id); if (f) { fs_delete(f->id); print("Removed.\n"); } else print("File not found.\n"); } 
        else print("Usage: rm <filename>\n");
    }
    else if (strncmp(base_cmd, "peachfetch", 10) == 0 || strncmp(base_cmd, "sysinfo", 7) == 0) print_peachfetch();
    else if (strncmp(base_cmd, "echo", 4) == 0) { char* text = cmd + 5; if (*text) { print(text); print("\n"); } }
    else if (strncmp(base_cmd, "clear", 5) == 0) clear_screen();
    else if (strncmp(base_cmd, "reboot", 6) == 0) { outb(0x64, 0xFE); while(1) { __asm__ volatile ("hlt"); } }
    else if (strncmp(base_cmd, "help", 4) == 0) {
        print("System : help, clear, reboot, sysmon, diskutil, peachfetch, startgui\n");
        print("Disk   : diskinfo, fsinfo, savefs, hexview, install\n");
        print("Files  : explorer, edit <file>, ls, mkdir, cd, rm\n");
        print("Apps   : matrix, bf <file>, echo\n");
    }
    else if (strncmp(base_cmd, "startgui", 8) == 0) {
        term_color = 0x0A;
        print("Initializing Peach OS GUI Subsystem...\n");
        term_color = 0x0C;
        print("Error: VGA Mode 13h not active! Please wait for Phase 2 Bootloader upgrade.\n");
        term_color = 0x0F;
    }
    else { print("Command not found. Type 'help'.\n"); }
}

void kernel_main() {
    __asm__ volatile ("cld"); 
    restore_default_palette(); 
    fs_init();
    clear_screen();
    
    // Auto-Mounting Check - Launch Setup if uninitialized
    uint16_t id_data[256];
    if (ata_identify(id_data)) {
        uint8_t magic_buf[512];
        memset(magic_buf, 0, 512);
        ata_read_sector(FS_START_LBA, magic_buf);
        uint32_t read_magic = ((uint32_t)magic_buf[3] << 24) | ((uint32_t)magic_buf[2] << 16) | ((uint32_t)magic_buf[1] << 8) | magic_buf[0];
        
        if (read_magic == PFS_MAGIC) {
            sys_loadfs();
            print("Welcome to Peach OS v0.1\n");
            print("PFS v1.5 File System Mounted Automatically.\n\n");
        } else {
            // DISK UNINITIALIZED -> LAUNCH INSTALLER AUTOMATICALLY
            app_installer();
        }
    } else {
        print("Welcome to Peach OS v0.1\n");
        term_color = 0x0C;
        print("Notice: No hard drive detected! OS is running in RAM-only mode.\n\n");
        term_color = 0x0F;
    }
    
    char buffer[256];
    char prompt[64];
    while (1) {
        if (current_dir_id == -1) strcpy(prompt, "PeachOS:/> ");
        else { 
            strcpy(prompt, "PeachOS:/"); 
            char* dname = fs_get(current_dir_id)->name;
            int len = strlen(prompt); int dlen = strlen(dname);
            if(dlen > 10) { 
                for(int i=0; i<8; i++) prompt[len+i] = dname[i];
                prompt[len+8] = '.'; prompt[len+9] = '.'; len += 10;
            } else { strcpy(prompt + len, dname); len += dlen; }
            strcpy(prompt + len, "> ");
        }
        
        get_input(prompt, buffer, 256);
        char parsed_buffer[256]; strcpy(parsed_buffer, buffer);
        for(int i=0; parsed_buffer[i] != '\0'; i++) if (parsed_buffer[i] == ' ') parsed_buffer[i] = '\0';
        execute_command(buffer);
    }
}