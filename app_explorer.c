/* app_explorer.c */
#include "peach_api.h"

static int copied_file_id = -1;

void app_view(const char* filename) {
    PFS_Inode* f = fs_find_in(filename, current_dir_id);
    if (!f || f->is_dir) { print("Error: File not found.\n"); return; }
    
    char view_buf[4096]; int f_sz = pfs_read_file(f->id, view_buf, 4096);
    
    uint8_t old_color = term_color; int view_offset_y = 0;
    while(1) {
        vga_fill_rect(0, 0, VGA_COLS, VGA_ROWS, ' ', 0x1F); vga_fill_rect(0, 0, VGA_COLS, 1, ' ', 0x70);
        int cx = 1; vga_print_str_ext(&cx, 0, "PEACH VIEW  |  Viewing: ", 0x70); vga_print_str_ext(&cx, 0, filename, 0x70);
        vga_fill_rect(0, 24, VGA_COLS, 1, ' ', 0x70); vga_print_str(1, 24, "[ESC] Exit   [Up/Down] Scroll", 0x70);
        int logical_x = 0, logical_y = 0;
        for (int i = 0; i < f_sz; i++) {
            if (logical_y >= view_offset_y && logical_y < view_offset_y + 23) {
                if (view_buf[i] != '\n') vga_putc(logical_x, logical_y - view_offset_y + 1, view_buf[i], 0x1F);
            }
            if (view_buf[i] == '\n') { logical_x = 0; logical_y++; } else { logical_x++; if (logical_x >= VGA_COLS) { logical_x = 0; logical_y++; } }
        }
        update_cursor(80, 25); vga_swap(); char c = get_key_blocking();
        if (c == 27) break; else if (c == 17 && view_offset_y > 0) view_offset_y--; else if (c == 18 && view_offset_y < logical_y) view_offset_y++; 
    }
    term_color = old_color; clear_screen();
}

void app_explorer() {
    uint8_t old_color = term_color; int selected = 0; int cur_dir = current_dir_id;
    while(1) {
        int active_idxs[MAX_FILES + 1]; int num_files = 0;
        if (cur_dir != -1) active_idxs[num_files++] = -2; 
        for(int i=0; i<MAX_FILES; i++) if (fs[i].in_use && fs[i].parent_id == cur_dir) active_idxs[num_files++] = i;
        if (num_files > 0 && selected >= num_files) selected = num_files - 1;
        if (num_files == 0) selected = 0;
        
        vga_fill_rect(0, 0, VGA_COLS, VGA_ROWS, ' ', 0x1F); vga_fill_rect(0, 0, VGA_COLS, 1, ' ', 0x70);
        vga_print_str(1, 0, "PEACH FILE EXPLORER (PFS)", 0x70); vga_fill_rect(0, 24, VGA_COLS, 1, ' ', 0x70);
        vga_print_str(1, 24, "[ESC] Exit [Arrows] Nav [Enter] Open [C] Copy [P] Paste [Del] Delete", 0x70);
        vga_fill_rect(1, 2, 28, 21, ' ', 0x3F); 
        
        char breadcrumb[32] = "DIR: ";
        if (cur_dir == -1) strcpy(breadcrumb + 5, "ROOT"); else { PFS_Inode* df = fs_get(cur_dir); strcpy(breadcrumb + 5, df->name); }
        vga_print_str(2, 2, breadcrumb, 0x3F);
        
        for(int i=0; i<num_files; i++) {
            uint8_t col = (i == selected) ? 0x70 : 0x3F;
            vga_fill_rect(2, 4 + i, 26, 1, ' ', col);
            if (active_idxs[i] == -2) vga_print_str(3, 4 + i, "[..] UP", col);
            else {
                PFS_Inode* f = &fs[active_idxs[i]];
                if (f->is_dir) { vga_print_str(3, 4+i, "[DIR] ", col); vga_print_str(9, 4+i, f->name, col); }
                else vga_print_str(3, 4+i, f->name, col);
            }
        }
        if (num_files == 0) vga_print_str(3, 4, "(Empty Folder)", 0x3F);
        
        vga_fill_rect(31, 2, 48, 21, ' ', 0x0F); vga_print_str(32, 2, "PREVIEW", 0x0F);
        if (num_files > 0 && active_idxs[selected] >= 0) {
            PFS_Inode* sel_f = &fs[active_idxs[selected]];
            if (sel_f->is_dir) { vga_print_str(32, 4, "<Directory>", 0x08); }
            else if (sel_f->start_block != PFS_EOF && sel_f->start_block != PFS_FREE) {
                int lx = 32, ly = 4;
                int p_sz = sel_f->size > 512 ? 512 : sel_f->size;
                for(int i=0; i < p_sz && ly < 22; i++) {
                    char c = pfs_data[sel_f->start_block][i];
                    if (c == '\n') { lx = 32; ly++; }
                    else { vga_putc(lx, ly, c, 0x0F); lx++; if (lx >= 78) { lx = 32; ly++; } }
                }
            }
        }
        update_cursor(80, 25); vga_swap();
        char k = get_key_blocking();
        if (k == 27) break;
        else if (k == 17 && selected > 0) selected--; else if (k == 18 && selected < num_files - 1) selected++;
        else if (k == '\n' && num_files > 0) {
            if (active_idxs[selected] == -2) cur_dir = fs_get(cur_dir)->parent_id;
            else { PFS_Inode* f = &fs[active_idxs[selected]]; if (f->is_dir) { cur_dir = f->id; selected = 0; } else app_view(f->name); }
        }
        else if ((k == 'c' || k == 'C') && num_files > 0 && active_idxs[selected] >= 0) copied_file_id = active_idxs[selected]; 
        else if ((k == 'p' || k == 'P') && copied_file_id >= 0) {
            PFS_Inode* src = fs_get(copied_file_id);
            if (src && !src->is_dir) {
                char new_name[20]; strcpy(new_name, "cpy_"); 
                int len = 4; char* sn = src->name; while(*sn && len < 19) new_name[len++] = *sn++; new_name[len] = '\0';
                PFS_Inode* dst = fs_create_in(new_name, 0, cur_dir);
                if (dst) pfs_copy_file(src->id, dst->id);
            }
        }
        else if (k == 21 && num_files > 0 && active_idxs[selected] >= 0) fs_delete(active_idxs[selected]);
    }
    term_color = old_color; clear_screen();
}