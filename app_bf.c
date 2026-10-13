/* app_bf.c */
#include "peach_api.h"

uint8_t bf_mem[30000];

void app_bf(const char* filename) {
    if (!filename) { print("Usage: bf <filename>\n"); return; }
    PFS_Inode* f = fs_find_in(filename, current_dir_id);
    if (!f || f->is_dir) { print("File not found.\n"); return; }
    
    char code[4096];
    int sz = pfs_read_file(f->id, code, 4096);
    
    memset(bf_mem, 0, 30000);
    int ptr = 0;
    int pc = 0;
    
    print("Executing script...\n\n");
    
    while (pc < sz) {
        char c = code[pc];
        if (c == '>') { if (ptr < 29999) ptr++; }
        else if (c == '<') { if (ptr > 0) ptr--; }
        else if (c == '+') bf_mem[ptr]++;
        else if (c == '-') bf_mem[ptr]--;
        else if (c == '.') print_char((char)bf_mem[ptr]);
        else if (c == ',') bf_mem[ptr] = get_key_blocking();
        else if (c == '[') {
            if (bf_mem[ptr] == 0) {
                int loop = 1;
                while (loop > 0 && pc < sz - 1) {
                    pc++;
                    if (code[pc] == '[') loop++;
                    else if (code[pc] == ']') loop--;
                }
            }
        } else if (c == ']') {
            if (bf_mem[ptr] != 0) {
                int loop = 1;
                while (loop > 0 && pc > 0) {
                    pc--;
                    if (code[pc] == ']') loop++;
                    else if (code[pc] == '[') loop--;
                }
            }
        }
        pc++;
    }
    print("\n\nExecution finished.\n");
}