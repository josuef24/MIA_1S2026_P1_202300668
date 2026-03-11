#ifndef STRUCTS_H
#define STRUCTS_H

#include <cstring>
#include <ctime>

// ==================== DISK STRUCTURES ====================

struct Partition {
    char part_status;       // Mounted or not
    char part_type;         // P = Primary, E = Extended
    char part_fit;          // B = Best, F = First, W = Worst
    int  part_start;        // Byte where partition starts
    int  part_s;            // Total size in bytes
    char part_name[16];     // Partition name
    int  part_correlative;  // Correlative number (-1 until mounted)
    char part_id[4];        // ID generated on mount

    Partition() {
        part_status = '0';
        part_type = 'P';
        part_fit = 'W';
        part_start = -1;
        part_s = 0;
        memset(part_name, 0, 16);
        part_correlative = -1;
        memset(part_id, 0, 4);
    }
} __attribute__((packed));

struct MBR {
    int  mbr_tamano;            // Total disk size in bytes
    time_t mbr_fecha_creacion;  // Creation date
    int  mbr_dsk_signature;     // Random unique disk identifier
    char dsk_fit;               // Fit type: B, F, W
    Partition mbr_partitions[4]; // 4 partitions

    MBR() {
        mbr_tamano = 0;
        mbr_fecha_creacion = 0;
        mbr_dsk_signature = 0;
        dsk_fit = 'F';
    }
} __attribute__((packed));

struct EBR {
    char part_mount;    // Mounted or not
    char part_fit;      // B, F, W
    int  part_start;    // Byte where logical partition starts
    int  part_s;        // Total size in bytes
    int  part_next;     // Byte of next EBR, -1 if none
    char part_name[16]; // Partition name

    EBR() {
        part_mount = '0';
        part_fit = 'W';
        part_start = -1;
        part_s = 0;
        part_next = -1;
        memset(part_name, 0, 16);
    }
} __attribute__((packed));

// ==================== EXT2 STRUCTURES ====================

struct SuperBlock {
    int  s_filesystem_type;    // 2 for EXT2
    int  s_inodes_count;       // Total inodes
    int  s_blocks_count;       // Total blocks
    int  s_free_blocks_count;  // Free blocks
    int  s_free_inodes_count;  // Free inodes
    time_t s_mtime;            // Last mount time
    time_t s_umtime;           // Last unmount time
    int  s_mnt_count;          // Mount count
    int  s_magic;              // 0xEF53
    int  s_inode_s;            // Inode size
    int  s_block_s;            // Block size
    int  s_firts_ino;          // First free inode
    int  s_first_blo;          // First free block
    int  s_bm_inode_start;     // Bitmap inodes start
    int  s_bm_block_start;     // Bitmap blocks start
    int  s_inode_start;        // Inode table start
    int  s_block_start;        // Block table start

    SuperBlock() {
        s_filesystem_type = 2;
        s_inodes_count = 0;
        s_blocks_count = 0;
        s_free_blocks_count = 0;
        s_free_inodes_count = 0;
        s_mtime = 0;
        s_umtime = 0;
        s_mnt_count = 0;
        s_magic = 0xEF53;
        s_inode_s = sizeof(SuperBlock); // will be set properly
        s_block_s = 64;
        s_firts_ino = -1;
        s_first_blo = -1;
        s_bm_inode_start = 0;
        s_bm_block_start = 0;
        s_inode_start = 0;
        s_block_start = 0;
    }
} __attribute__((packed));

struct Inode {
    int    i_uid;        // User ID owner
    int    i_gid;        // Group ID
    int    i_s;          // File size in bytes
    time_t i_atime;      // Last access time
    time_t i_ctime;      // Creation time
    time_t i_mtime;      // Last modification time
    int    i_block[15];  // 12 direct + 1 single indirect + 1 double indirect + 1 triple indirect
    char   i_type;       // '0' = folder, '1' = file
    char   i_perm[3];    // UGO permissions (e.g., "664")

    Inode() {
        i_uid = 0;
        i_gid = 0;
        i_s = 0;
        i_atime = 0;
        i_ctime = 0;
        i_mtime = 0;
        for (int i = 0; i < 15; i++) i_block[i] = -1;
        i_type = '0';
        i_perm[0] = '6';
        i_perm[1] = '6';
        i_perm[2] = '4';
    }
} __attribute__((packed));

struct Content {
    char b_name[12];   // File/folder name
    int  b_inodo;      // Inode pointer

    Content() {
        memset(b_name, 0, 12);
        b_inodo = -1;
    }
} __attribute__((packed));

struct FolderBlock {
    Content b_content[4]; // 4 entries per folder block

    FolderBlock() {}
} __attribute__((packed));

struct FileBlock {
    char b_content[64]; // File content

    FileBlock() {
        memset(b_content, 0, 64);
    }
} __attribute__((packed));

struct PointerBlock {
    int b_pointers[16]; // Pointers to other blocks

    PointerBlock() {
        for (int i = 0; i < 16; i++) b_pointers[i] = -1;
    }
} __attribute__((packed));

#endif // STRUCTS_H
