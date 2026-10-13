/* peach_api.h */
#ifndef PEACH_API_H
#define PEACH_API_H

#include <stdint.h>
#include <stddef.h>

/* --- VGA & UI GLOBALS --- */
extern const int VGA_COLS;
extern const int VGA_ROWS;
extern uint8_t term_color;

/* --- PFS DEFINITIONS --- */
#define PFS_MAGIC 0x50465331
#define MAX_FILES 64
#define PFS_MAX_BLOCKS 256
#define PFS_BLOCK_SIZE 512
#define PFS_EOF 0xFFFF
#define PFS_FREE 0x0000
#define FS_START_LBA 100

typedef struct __attribute__((packed)) {
    int id;
    uint8_t in_use;
    uint8_t is_dir;
    int parent_id;
    char name[20];
    int size;
    uint16_t start_block;
    uint8_t c_d, c_m, c_y; // Creation Date
    uint8_t c_h, c_min;    // Creation Time
    uint8_t m_d, m_m, m_y; // Modified Date
    uint8_t m_h, m_min;    // Modified Time
} PFS_Inode;

extern PFS_Inode fs[MAX_FILES];
extern uint16_t pfs_fat[PFS_MAX_BLOCKS];
extern uint8_t pfs_data[PFS_MAX_BLOCKS][PFS_BLOCK_SIZE];
extern int current_dir_id;

/* --- HARDWARE I/O --- */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret; __asm__ volatile ( "inb %1, %0" : "=a"(ret) : "Nd"(port) ); return ret;
}
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}
static inline uint64_t rdtsc() {
    uint32_t lo, hi; __asm__ volatile ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

/* --- UTILITIES --- */
void* memset(void* dest, int val, size_t count);
void* memcpy(void* dest, const void* src, size_t count);
size_t strlen(const char* str);
int strcmp(const char* a, const char* b);
void strcpy(char* dest, const char* src);
int atoi(const char* str);
void itoa(int num, char* str, int base);
void reverse(char* str, int length);
uint32_t sys_rand();
uint8_t read_rtc(int reg);
uint8_t bcd_to_bin(uint8_t bcd);

/* --- VGA FUNCTIONS --- */
void vga_putc(int x, int y, char c, uint8_t color);
void vga_print_str(int x, int y, const char* str, uint8_t color);
void vga_print_str_ext(int* x, int y, const char* str, uint8_t color);
void vga_fill_rect(int x, int y, int w, int h, char c, uint8_t color);
void vga_draw_3d_box(int x, int y, int w, int h, char c, uint8_t color);
void vga_swap();
void clear_screen();
void update_cursor(int x, int y);
void print(const char* str);
void print_char(char c);
void print_int(int num);
void print_long(uint64_t num);
void print_hex8(uint8_t val);

/* --- INPUT & GUI --- */
char get_key_blocking();
char get_key_nonblocking();
void get_input(const char* prompt_str, char* buffer, int max_len);
int gui_menu(const char* title, const char* opts[], int num_opts, const char* status_bar);
void gui_msgbox(const char* title, const char* msg);

/* --- PFS EXPORTS --- */
void fs_init();
PFS_Inode* fs_find_in(const char* name, int parent);
PFS_Inode* fs_create_in(const char* name, int is_dir, int parent);
void fs_delete(int id);
PFS_Inode* fs_get(int id);
int pfs_read_file(int inode_id, char* buffer, int max_len);
int pfs_write_file(int inode_id, const char* buffer, int size);
void pfs_copy_file(int src_id, int dst_id);

/* --- ATA DISK & SYSTEM EXPORTS --- */
int ata_identify(uint16_t* buffer);
void ata_read_sector(uint32_t lba, uint8_t* buffer);
void ata_write_sector(uint32_t lba, uint8_t* buffer);
void sys_diskinfo();
void sys_fsinfo();
void sys_savefs();

/* --- APP EXPORTS --- */
void app_explorer();
void app_editor(const char* filename);
void app_sysmon();
void app_matrix();
void app_diskutil();
void app_bf(const char* filename);
void app_hexview();
void app_installer();

#endif