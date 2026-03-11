#include "parser.h"
#include "../Structures/structs.h"
#include "../Utils/globals.h"
#include <fstream>
#include <cstring>
#include <cmath>
#include <ctime>

// Helper to find a mounted partition by id
static MountedPartition* findMounted(const std::string& id) {
    for (auto& mp : Globals::mounted_partitions) {
        if (mp.id == id) return &mp;
    }
    return nullptr;
}

std::string cmd_mkfs(const ParsedCommand& cmd) {
    if (cmd.params.find("id") == cmd.params.end()) {
        return "ERROR mkfs: Parametro -id es obligatorio.";
    }

    std::string id = cmd.params.at("id");

    // Find mounted partition
    MountedPartition* mp = findMounted(id);
    if (!mp) {
        return "ERROR mkfs: No se encontro la particion montada con id " + id + ".";
    }

    // Parse type (default full)
    // Only "full" is supported
    if (cmd.params.find("type") != cmd.params.end()) {
        std::string type = cmd.params.at("type");
        // Accept "full" (only option per spec)
    }

    std::string path = mp->path;
    int partStart = mp->part_start;
    int partSize = mp->part_size;

    // Calculate n (number of structures)
    // partition_size = sizeof(SuperBlock) + n + 3*n + n*sizeof(Inode) + 3*n*sizeof(FileBlock)
    // partition_size = sizeof(SuperBlock) + n*(1 + 3 + sizeof(Inode) + 3*sizeof(FileBlock))
    int sbSize = sizeof(SuperBlock);
    int inodeSize = sizeof(Inode);
    int blockSize = sizeof(FileBlock); // All blocks are 64 bytes

    double numerator = (double)(partSize - sbSize);
    double denominator = 1.0 + 3.0 + (double)inodeSize + 3.0 * (double)blockSize;
    int n = (int)std::floor(numerator / denominator);

    if (n <= 0) {
        return "ERROR mkfs: La particion es demasiado pequeña para formatear.";
    }

    int blocksCount = 3 * n;

    // Open disk for writing
    std::fstream disk(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!disk.is_open()) {
        return "ERROR mkfs: No se pudo abrir el disco.";
    }

    // Clear partition space with zeros
    char buffer[1024];
    memset(buffer, 0, 1024);
    int bytesToClear = partSize;
    disk.seekp(partStart);
    while (bytesToClear > 0) {
        int toWrite = (bytesToClear > 1024) ? 1024 : bytesToClear;
        disk.write(buffer, toWrite);
        bytesToClear -= toWrite;
    }

    // Calculate positions
    int bmInodeStart = partStart + sbSize;
    int bmBlockStart = bmInodeStart + n;
    int inodeTableStart = bmBlockStart + blocksCount;
    int blockTableStart = inodeTableStart + n * inodeSize;

    // Create SuperBlock
    SuperBlock sb;
    sb.s_filesystem_type = 2;
    sb.s_inodes_count = n;
    sb.s_blocks_count = blocksCount;
    sb.s_free_blocks_count = blocksCount - 2; // root folder block + users.txt file block
    sb.s_free_inodes_count = n - 2; // root inode + users.txt inode
    sb.s_mtime = time(nullptr);
    sb.s_umtime = 0;
    sb.s_mnt_count = 1;
    sb.s_magic = 0xEF53;
    sb.s_inode_s = inodeSize;
    sb.s_block_s = blockSize;
    sb.s_firts_ino = 2; // First free inode (0=root, 1=users.txt)
    sb.s_first_blo = 2; // First free block (0=root folder block, 1=users.txt file block)
    sb.s_bm_inode_start = bmInodeStart;
    sb.s_bm_block_start = bmBlockStart;
    sb.s_inode_start = inodeTableStart;
    sb.s_block_start = blockTableStart;

    // Write SuperBlock
    disk.seekp(partStart);
    disk.write(reinterpret_cast<char*>(&sb), sizeof(SuperBlock));

    // Initialize bitmaps to 0 (already zeroed from clear)
    // Mark inode 0 and 1 as used
    char one = '1';
    disk.seekp(bmInodeStart);
    disk.write(&one, 1); // inode 0 = root folder
    disk.seekp(bmInodeStart + 1);
    disk.write(&one, 1); // inode 1 = users.txt

    // Mark blocks 0 and 1 as used
    disk.seekp(bmBlockStart);
    disk.write(&one, 1); // block 0 = root folder block
    disk.seekp(bmBlockStart + 1);
    disk.write(&one, 1); // block 1 = users.txt file block

    // Create root inode (inode 0) - folder type
    Inode rootInode;
    rootInode.i_uid = 1;
    rootInode.i_gid = 1;
    rootInode.i_s = 0;
    rootInode.i_atime = time(nullptr);
    rootInode.i_ctime = time(nullptr);
    rootInode.i_mtime = time(nullptr);
    rootInode.i_type = '0'; // folder
    rootInode.i_perm[0] = '7';
    rootInode.i_perm[1] = '7';
    rootInode.i_perm[2] = '7';
    rootInode.i_block[0] = 0; // Points to folder block 0
    // Rest are -1 (default)

    disk.seekp(inodeTableStart);
    disk.write(reinterpret_cast<char*>(&rootInode), sizeof(Inode));

    // Create root folder block (block 0)
    FolderBlock rootBlock;
    // Entry 0: "." pointing to inode 0
    strcpy(rootBlock.b_content[0].b_name, ".");
    rootBlock.b_content[0].b_inodo = 0;
    // Entry 1: ".." pointing to inode 0 (root's parent is itself)
    strcpy(rootBlock.b_content[1].b_name, "..");
    rootBlock.b_content[1].b_inodo = 0;
    // Entry 2: users.txt pointing to inode 1
    strcpy(rootBlock.b_content[2].b_name, "users.txt");
    rootBlock.b_content[2].b_inodo = 1;
    // Entry 3: empty
    rootBlock.b_content[3].b_inodo = -1;

    disk.seekp(blockTableStart);
    disk.write(reinterpret_cast<char*>(&rootBlock), sizeof(FolderBlock));

    // Create users.txt content
    std::string usersContent = "1,G,root\n1,U,root,root,123\n";

    // Create users.txt inode (inode 1) - file type
    Inode usersInode;
    usersInode.i_uid = 1;
    usersInode.i_gid = 1;
    usersInode.i_s = usersContent.size();
    usersInode.i_atime = time(nullptr);
    usersInode.i_ctime = time(nullptr);
    usersInode.i_mtime = time(nullptr);
    usersInode.i_type = '1'; // file
    usersInode.i_perm[0] = '7';
    usersInode.i_perm[1] = '7';
    usersInode.i_perm[2] = '7';
    usersInode.i_block[0] = 1; // Points to file block 1

    disk.seekp(inodeTableStart + inodeSize);
    disk.write(reinterpret_cast<char*>(&usersInode), sizeof(Inode));

    // Create users.txt file block (block 1)
    FileBlock usersBlock;
    memset(usersBlock.b_content, 0, 64);
    strncpy(usersBlock.b_content, usersContent.c_str(), 64);

    disk.seekp(blockTableStart + blockSize);
    disk.write(reinterpret_cast<char*>(&usersBlock), sizeof(FileBlock));

    disk.close();

    return "MKFS: Particion " + id + " formateada como EXT2 exitosamente. (n=" + std::to_string(n) + " inodos, " + std::to_string(blocksCount) + " bloques)";
}
