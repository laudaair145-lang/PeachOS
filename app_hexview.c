/* app_hexview.c */
#include "peach_api.h"

void app_hexview() {
    int lba = 0;
    uint8_t sector[512];
    
    while(1) {
        clear_screen();
        term_color = 0x0B;
        print("PEACH INTERACTIVE SECTOR VIEWER  -  LBA: "); print_int(lba); print("\n");
        term_color = 0x0F;
        ata_read_sector(lba, sector);
        
        for(int i=0; i<512; i+=16) {
            print_hex8(i >> 8); print_hex8(i & 0xFF); print(": ");
            for(int j=0; j<16; j++) { 
                print_hex8(sector[i+j]); print(" "); 
            }
            print(" | ");
            for(int j=0; j<16; j++) { 
                char c = sector[i+j]; 
                if(c>=32 && c<=126) print_char(c); else print_char('.');
            }
            print("\n");
        }
        
        term_color = 0x0E;
        print("\n< Prev LBA  |  > Next LBA  |  J: Jump to LBA  |  ESC: Exit\n");
        term_color = 0x0F;
        
        char k = get_key_blocking();
        if (k == 27) break;
        if (k == 19 && lba > 0) lba--; // Left Arrow
        if (k == 20) lba++; // Right Arrow
        if (k == 'j' || k == 'J') {
            char buf[16]; get_input("Jump to LBA: ", buf, 16);
            lba = atoi(buf);
        }
    }
    clear_screen();
}