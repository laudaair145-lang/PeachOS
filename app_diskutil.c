/* app_diskutil.c */
#include "peach_api.h"

/* Local ATA definitions for raw disk access */
#define ATA_PRIMARY_CMD_STS 0x1F7
extern void ata_wait_ready();

void app_diskutil() {
    uint8_t old_color = term_color;
    
    while(1) {
        clear_screen();
        term_color = 0x0B;
        print("==================================================\n");
        print("          PEACH TEXT DISK UTILITY v2.0            \n");
        print("==================================================\n\n");
        term_color = 0x0F;
        
        uint16_t id_data[256];
        uint32_t sectors = 0;
        
        /* Fetch and calculate size and capacity */
        if (ata_identify(id_data)) {
            sectors = id_data[60] | ((uint32_t)id_data[61] << 16);
            uint32_t size_kb = sectors / 2; // 512 bytes per sector = 1/2 KB
            uint32_t size_mb = size_kb / 1024;
            
            term_color = 0x0A;
            print("Status   : Drive Detected\n");
            term_color = 0x0F;
            print("Capacity : ");
            if (size_mb > 0) { print_int(size_mb); print(" MB ("); print_int(size_kb); print(" KB)\n"); }
            else { print_int(size_kb); print(" KB\n"); }
            print("Sectors  : "); print_long(sectors); print("\n\n");
        } else {
            term_color = 0x0C;
            print("Status   : NO ATA DRIVE DETECTED\n\n");
            term_color = 0x0F;
        }
        
        print("Options:\n");
        print("  1. View Detailed Disk Diagnostics\n");
        print("  2. Initialize Disk (Write MBR)\n");
        print("  3. Full Format (Zero-Fill & Initialize PFS)\n");
        print("  4. Secure Erase (Zero-Fill & Destroy Data)\n");
        print("  5. Exit Disk Utility\n\n");
        
        char input[16];
        get_input("Select an operation (1-5): ", input, 16);
        
        if (input[0] == '1') {
            print("\n");
            sys_diskinfo();
            print("\nPress Enter to return...");
            get_input("", input, 2);
        }
        else if (input[0] == '2') {
            if (sectors == 0) { print("\nError: No drive.\nPress Enter..."); get_input("", input, 2); continue; }
            print("\nWriting Master Boot Record (MBR)...\n");
            
            uint8_t mbr[512];
            memset(mbr, 0, 512);
            mbr[510] = 0x55; mbr[511] = 0xAA;
            ata_write_sector(0, mbr);
            
            /* Verification Routine */
            uint8_t verify_buf[512];
            memset(verify_buf, 0, 512);
            ata_read_sector(0, verify_buf);
            if (verify_buf[510] == 0x55 && verify_buf[511] == 0xAA) {
                term_color = 0x0A; print("-> VERIFIED: MBR Signature (0x55AA) written to LBA 0 successfully.\n"); term_color = 0x0F;
            } else {
                term_color = 0x0C; print("-> ERROR: MBR Verification Failed.\n"); term_color = 0x0F;
            }
            print("\nPress Enter to return..."); get_input("", input, 2);
        }
        else if (input[0] == '3') {
            if (sectors == 0) { print("\nError: No drive.\nPress Enter..."); get_input("", input, 2); continue; }
            char conf[16];
            
            term_color = 0x0C;
            get_input("\nWARNING: Full Format wipes all data. Type 'YES' to confirm: ", conf, 16);
            term_color = 0x0F;
            
            if (strcmp(conf, "YES") == 0) {
                print("\nStep 1: Zero-filling drive sectors...\n");
                uint8_t zeros[512];
                memset(zeros, 0, 512); // Now completely immune to stack ghosting
                
                for (int i = 0; i < 300; i++) {
                    ata_write_sector(FS_START_LBA + i, zeros);
                }
                
                print("Step 2: Initializing Peach File System...\n");
                fs_init();
                sys_savefs(); /* Internal function prints its own progress messages */
                
                /* Verification Routine (Bypassing Strict Aliasing) */
                print("Verifying PFS Magic Signature...\n");
                
                uint8_t verify_buf[512]; 
                memset(verify_buf, 0, 512);
                ata_read_sector(FS_START_LBA, verify_buf);
                
                // Safely reconstruct the 32-bit integer byte by byte
                uint32_t read_magic = ((uint32_t)verify_buf[3] << 24) | ((uint32_t)verify_buf[2] << 16) | ((uint32_t)verify_buf[1] << 8) | verify_buf[0];
                
                if (read_magic == PFS_MAGIC) {
                    term_color = 0x0A; print("-> VERIFIED: Full Format completed successfully.\n"); term_color = 0x0F;
                } else {
                    term_color = 0x0C; print("-> ERROR: PFS Verification Failed.\n"); 
                    print("   Expected: 0x50465331\n   Read    : 0x");
                    print_hex8(verify_buf[3]); print_hex8(verify_buf[2]); print_hex8(verify_buf[1]); print_hex8(verify_buf[0]); print("\n");
                    term_color = 0x0F;
                }
            } else {
                print("Operation aborted.\n");
            }
            print("\nPress Enter to return..."); get_input("", input, 2);
        }
        else if (input[0] == '4') {
            if (sectors == 0) { print("\nError: No drive.\nPress Enter..."); get_input("", input, 2); continue; }
            char conf[16];
            
            term_color = 0x0C;
            get_input("\nWARNING: This wipes the filesystem. Type 'YES' to confirm: ", conf, 16);
            term_color = 0x0F;
            
            if (strcmp(conf, "YES") == 0) {
                print("Zero-filling 300 sectors starting at LBA 100...\n");
                uint8_t zeros[512];
                memset(zeros, 0, 512);
                for (int i = 0; i < 300; i++) {
                    ata_write_sector(FS_START_LBA + i, zeros);
                }
                
                // Final cache flush at the end of the batch
                ata_wait_ready(); outb(ATA_PRIMARY_CMD_STS, 0xE7); ata_wait_ready();
                
                fs_init(); // Reset kernel filesystem state to match the wiped disk
                
                /* Verification Routine */
                print("Verifying erasure...\n");
                uint8_t verify_buf[512];
                memset(verify_buf, 0, 512);
                ata_read_sector(FS_START_LBA, verify_buf);
                int ok = 1;
                for (int i = 0; i < 512; i++) {
                    if (verify_buf[i] != 0) ok = 0;
                }
                
                if (ok) {
                    term_color = 0x0A; print("-> VERIFIED: Sectors zeroed out successfully.\n"); term_color = 0x0F;
                } else {
                    term_color = 0x0C; print("-> ERROR: Sector data is not empty.\n"); term_color = 0x0F;
                }
            } else {
                print("Operation aborted.\n");
            }
            print("\nPress Enter to return..."); get_input("", input, 2);
        }
        else if (input[0] == '5') {
            break;
        }
    }
    
    term_color = old_color; clear_screen();
}