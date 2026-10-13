/* app_editor.c */
#include "peach_api.h"

void app_editor(const char* filename) {
    char edit_buf[4096]; int edit_sz = 0; uint8_t old_color = term_color;
    int cursor_pos = 0, cur_x = 0, cur_y = 1, view_offset_y = 0;
    
    PFS_Inode* f = NULL;
    if (filename && filename[0] != '\0') {
        f = fs_find_in(filename, current_dir_id);
        if (f && !f->is_dir) { edit_sz = pfs_read_file(f->id, edit_buf, 4096); cursor_pos = edit_sz; }
    }

    while(1) {
        vga_fill_rect(0, 1, VGA_COLS, 23, ' ', 0x1F);
        int logical_x = 0, logical_y = 0, cur_logical_x = 0, cur_logical_y = 0;
        for (int i = 0; i <= edit_sz; i++) {
            if (i == cursor_pos) { cur_logical_x = logical_x; cur_logical_y = logical_y; }
            if (i == edit_sz) break;
            if (edit_buf[i] == '\n') { logical_x = 0; logical_y++; }
            else { logical_x++; if (logical_x >= VGA_COLS) { logical_x = 0; logical_y++; } }
        }
        if (cur_logical_y < view_offset_y) view_offset_y = cur_logical_y;
        if (cur_logical_y >= view_offset_y + 23) view_offset_y = cur_logical_y - 22;
        logical_x = 0; logical_y = 0;
        for (int i = 0; i < edit_sz; i++) {
            if (logical_y >= view_offset_y && logical_y < view_offset_y + 23) {
                if (edit_buf[i] != '\n') vga_putc(logical_x, logical_y - view_offset_y + 1, edit_buf[i], 0x1F);
            }
            if (edit_buf[i] == '\n') { logical_x = 0; logical_y++; } else { logical_x++; if (logical_x >= VGA_COLS) { logical_x = 0; logical_y++; } }
        }
        cur_x = cur_logical_x; cur_y = cur_logical_y - view_offset_y + 1;
        vga_fill_rect(0, 0, VGA_COLS, 1, ' ', 0x70); 
        
        int cx = 1; vga_print_str_ext(&cx, 0, "PEACH EDIT  | ", 0x70); 
        if (filename && filename[0] != '\0') vga_print_str_ext(&cx, 0, filename, 0x70); else vga_print_str_ext(&cx, 0, "Untitled", 0x70);
        
        vga_fill_rect(0, 24, VGA_COLS, 1, ' ', 0x70); vga_print_str(1, 24, "[ESC] Exit & Save", 0x70);
        vga_swap(); if (cur_y >= 1 && cur_y <= 23) update_cursor(cur_x, cur_y);
        
        char c = get_key_blocking();
        if (c == 27) break;
        else if (c == 19 && cursor_pos > 0) cursor_pos--; else if (c == 20 && cursor_pos < edit_sz) cursor_pos++; 
        else if (c == 21 && cursor_pos < edit_sz) { for (int i = cursor_pos; i < edit_sz - 1; i++) edit_buf[i] = edit_buf[i+1]; edit_sz--; }
        else if (c == '\b' && cursor_pos > 0) { for (int i = cursor_pos - 1; i < edit_sz - 1; i++) edit_buf[i] = edit_buf[i+1]; edit_sz--; cursor_pos--; }
        else if (c >= 32 || c == '\n') {
            if (edit_sz < 4096) {
                for (int i = edit_sz; i > cursor_pos; i--) edit_buf[i] = edit_buf[i-1];
                edit_buf[cursor_pos++] = c; edit_sz++;
            }
        }
    }
    
    if (filename && filename[0] != '\0') {
        if (!f) f = fs_create_in(filename, 0, current_dir_id);
        if (f) { pfs_write_file(f->id, edit_buf, edit_sz); print("File saved.\n"); } else print("Error saving.\n");
    } else {
        char input_name[32]; vga_fill_rect(0, 24, VGA_COLS, 1, ' ', 0x70); vga_swap();
        get_input(" Save as (blank cancels): ", input_name, 16);
        if (input_name[0] != '\0') {
            PFS_Inode* saved_f = fs_create_in(input_name, 0, current_dir_id);
            if (saved_f) { pfs_write_file(saved_f->id, edit_buf, edit_sz); print("File saved.\n"); }
        }
    }
    term_color = old_color; clear_screen();
}