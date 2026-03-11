#include "parser.h"
#include "../Structures/structs.h"
#include "../Utils/globals.h"
#include <fstream>
#include <cstring>
#include <sstream>
#include <vector>
#include <ctime>

// ==================== HELPERS ====================

// Allocate a new inode, returns index
static int allocateInode(std::fstream& disk, SuperBlock& sb) {
    // Search bitmap for free inode
    for (int i = 0; i < sb.s_inodes_count; i++) {
        char bit;
        disk.seekg(sb.s_bm_inode_start + i);
        disk.read(&bit, 1);
        if (bit == '\0' || bit == '0') {
            char one = '1';
            disk.seekp(sb.s_bm_inode_start + i);
            disk.write(&one, 1);
            sb.s_free_inodes_count--;
            if (i == sb.s_firts_ino) {
                // Update first free inode
                sb.s_firts_ino = i + 1;
                while (sb.s_firts_ino < sb.s_inodes_count) {
                    disk.seekg(sb.s_bm_inode_start + sb.s_firts_ino);
                    disk.read(&bit, 1);
                    if (bit == '\0' || bit == '0') break;
                    sb.s_firts_ino++;
                }
            }
            return i;
        }
    }
    return -1;
}

// Allocate a new block, returns index
static int allocateBlock(std::fstream& disk, SuperBlock& sb) {
    for (int i = 0; i < sb.s_blocks_count; i++) {
        char bit;
        disk.seekg(sb.s_bm_block_start + i);
        disk.read(&bit, 1);
        if (bit == '\0' || bit == '0') {
            char one = '1';
            disk.seekp(sb.s_bm_block_start + i);
            disk.write(&one, 1);
            sb.s_free_blocks_count--;
            if (i == sb.s_first_blo) {
                sb.s_first_blo = i + 1;
                while (sb.s_first_blo < sb.s_blocks_count) {
                    disk.seekg(sb.s_bm_block_start + sb.s_first_blo);
                    disk.read(&bit, 1);
                    if (bit == '\0' || bit == '0') break;
                    sb.s_first_blo++;
                }
            }
            return i;
        }
    }
    return -1;
}

// Find inode by path, traversing from root
static int findInodeByPath2(std::fstream& disk, const SuperBlock& sb, const std::string& path) {
    if (path == "/") return 0;

    std::vector<std::string> parts;
    std::istringstream ss(path);
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (!part.empty()) parts.push_back(part);
    }

    int currentInode = 0;

    for (const auto& name : parts) {
        Inode inode;
        disk.seekg(sb.s_inode_start + currentInode * sizeof(Inode));
        disk.read(reinterpret_cast<char*>(&inode), sizeof(Inode));

        if (inode.i_type != '0') return -1;

        bool found = false;
        // Direct blocks
        for (int i = 0; i < 12 && !found; i++) {
            if (inode.i_block[i] == -1) continue;
            FolderBlock fb;
            disk.seekg(sb.s_block_start + inode.i_block[i] * sizeof(FileBlock));
            disk.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
            for (int j = 0; j < 4; j++) {
                if (fb.b_content[j].b_inodo == -1) continue;
                std::string fname(fb.b_content[j].b_name, strnlen(fb.b_content[j].b_name, 12));
                if (fname == name) {
                    currentInode = fb.b_content[j].b_inodo;
                    found = true;
                    break;
                }
            }
        }
        // Single indirect
        if (!found && inode.i_block[12] != -1) {
            PointerBlock pb;
            disk.seekg(sb.s_block_start + inode.i_block[12] * sizeof(FileBlock));
            disk.read(reinterpret_cast<char*>(&pb), sizeof(PointerBlock));
            for (int p = 0; p < 16 && !found; p++) {
                if (pb.b_pointers[p] == -1) continue;
                FolderBlock fb;
                disk.seekg(sb.s_block_start + pb.b_pointers[p] * sizeof(FileBlock));
                disk.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
                for (int j = 0; j < 4; j++) {
                    if (fb.b_content[j].b_inodo == -1) continue;
                    std::string fname(fb.b_content[j].b_name, strnlen(fb.b_content[j].b_name, 12));
                    if (fname == name) {
                        currentInode = fb.b_content[j].b_inodo;
                        found = true;
                        break;
                    }
                }
            }
        }
        if (!found) return -1;
    }

    return currentInode;
}

// Add an entry (name -> child inode) to a parent folder inode
static bool addEntryToFolder(std::fstream& disk, SuperBlock& sb, int parentInodeIdx, const std::string& name, int childInodeIdx) {
    Inode parentInode;
    disk.seekg(sb.s_inode_start + parentInodeIdx * sizeof(Inode));
    disk.read(reinterpret_cast<char*>(&parentInode), sizeof(Inode));

    // Try to find space in existing direct blocks
    for (int i = 0; i < 12; i++) {
        if (parentInode.i_block[i] == -1) {
            // Allocate new folder block
            int blockIdx = allocateBlock(disk, sb);
            if (blockIdx == -1) return false;
            parentInode.i_block[i] = blockIdx;

            FolderBlock fb;
            memset(fb.b_content[0].b_name, 0, 12);
            strncpy(fb.b_content[0].b_name, name.c_str(), 11);
            fb.b_content[0].b_inodo = childInodeIdx;

            disk.seekp(sb.s_block_start + blockIdx * sizeof(FileBlock));
            disk.write(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

            parentInode.i_mtime = time(nullptr);
            disk.seekp(sb.s_inode_start + parentInodeIdx * sizeof(Inode));
            disk.write(reinterpret_cast<char*>(&parentInode), sizeof(Inode));
            return true;
        }

        FolderBlock fb;
        disk.seekg(sb.s_block_start + parentInode.i_block[i] * sizeof(FileBlock));
        disk.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

        for (int j = 0; j < 4; j++) {
            if (fb.b_content[j].b_inodo == -1) {
                memset(fb.b_content[j].b_name, 0, 12);
                strncpy(fb.b_content[j].b_name, name.c_str(), 11);
                fb.b_content[j].b_inodo = childInodeIdx;

                disk.seekp(sb.s_block_start + parentInode.i_block[i] * sizeof(FileBlock));
                disk.write(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

                parentInode.i_mtime = time(nullptr);
                disk.seekp(sb.s_inode_start + parentInodeIdx * sizeof(Inode));
                disk.write(reinterpret_cast<char*>(&parentInode), sizeof(Inode));
                return true;
            }
        }
    }

    // Try single indirect pointer (block 12)
    if (parentInode.i_block[12] == -1) {
        int ptrBlockIdx = allocateBlock(disk, sb);
        if (ptrBlockIdx == -1) return false;
        parentInode.i_block[12] = ptrBlockIdx;
        PointerBlock pb;
        disk.seekp(sb.s_block_start + ptrBlockIdx * sizeof(FileBlock));
        disk.write(reinterpret_cast<char*>(&pb), sizeof(PointerBlock));
    }

    PointerBlock pb;
    disk.seekg(sb.s_block_start + parentInode.i_block[12] * sizeof(FileBlock));
    disk.read(reinterpret_cast<char*>(&pb), sizeof(PointerBlock));

    for (int p = 0; p < 16; p++) {
        if (pb.b_pointers[p] == -1) {
            int blockIdx = allocateBlock(disk, sb);
            if (blockIdx == -1) return false;
            pb.b_pointers[p] = blockIdx;

            FolderBlock fb;
            memset(fb.b_content[0].b_name, 0, 12);
            strncpy(fb.b_content[0].b_name, name.c_str(), 11);
            fb.b_content[0].b_inodo = childInodeIdx;

            disk.seekp(sb.s_block_start + blockIdx * sizeof(FileBlock));
            disk.write(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

            disk.seekp(sb.s_block_start + parentInode.i_block[12] * sizeof(FileBlock));
            disk.write(reinterpret_cast<char*>(&pb), sizeof(PointerBlock));

            parentInode.i_mtime = time(nullptr);
            disk.seekp(sb.s_inode_start + parentInodeIdx * sizeof(Inode));
            disk.write(reinterpret_cast<char*>(&parentInode), sizeof(Inode));
            return true;
        }

        FolderBlock fb;
        disk.seekg(sb.s_block_start + pb.b_pointers[p] * sizeof(FileBlock));
        disk.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

        for (int j = 0; j < 4; j++) {
            if (fb.b_content[j].b_inodo == -1) {
                memset(fb.b_content[j].b_name, 0, 12);
                strncpy(fb.b_content[j].b_name, name.c_str(), 11);
                fb.b_content[j].b_inodo = childInodeIdx;

                disk.seekp(sb.s_block_start + pb.b_pointers[p] * sizeof(FileBlock));
                disk.write(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

                parentInode.i_mtime = time(nullptr);
                disk.seekp(sb.s_inode_start + parentInodeIdx * sizeof(Inode));
                disk.write(reinterpret_cast<char*>(&parentInode), sizeof(Inode));
                return true;
            }
        }
    }

    return false;
}

// Create a single folder inode with . and .. entries
static int createFolderInode(std::fstream& disk, SuperBlock& sb, int parentInodeIdx) {
    int newInodeIdx = allocateInode(disk, sb);
    if (newInodeIdx == -1) return -1;

    int blockIdx = allocateBlock(disk, sb);
    if (blockIdx == -1) return -1;

    Inode newInode;
    newInode.i_uid = Globals::current_session.uid;
    newInode.i_gid = Globals::current_session.gid;
    newInode.i_s = 0;
    newInode.i_atime = time(nullptr);
    newInode.i_ctime = time(nullptr);
    newInode.i_mtime = time(nullptr);
    newInode.i_type = '0'; // folder
    newInode.i_perm[0] = '6';
    newInode.i_perm[1] = '6';
    newInode.i_perm[2] = '4';
    newInode.i_block[0] = blockIdx;

    disk.seekp(sb.s_inode_start + newInodeIdx * sizeof(Inode));
    disk.write(reinterpret_cast<char*>(&newInode), sizeof(Inode));

    FolderBlock fb;
    strcpy(fb.b_content[0].b_name, ".");
    fb.b_content[0].b_inodo = newInodeIdx;
    strcpy(fb.b_content[1].b_name, "..");
    fb.b_content[1].b_inodo = parentInodeIdx;

    disk.seekp(sb.s_block_start + blockIdx * sizeof(FileBlock));
    disk.write(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

    return newInodeIdx;
}

// ==================== MKDIR ====================

std::string cmd_mkdir(const ParsedCommand& cmd) {
    if (!Globals::current_session.active) return "ERROR mkdir: No hay sesion activa.";
    if (cmd.params.find("path") == cmd.params.end()) return "ERROR mkdir: Parametro -path es obligatorio.";

    std::string path = cmd.params.at("path");
    bool recursive = false;
    for (auto& f : cmd.flags) {
        if (f == "p") recursive = true;
    }

    std::string diskPath = Globals::current_session.disk_path;
    int partStart = Globals::current_session.part_start;

    std::fstream disk(diskPath, std::ios::in | std::ios::out | std::ios::binary);
    if (!disk.is_open()) return "ERROR mkdir: No se pudo abrir el disco.";

    SuperBlock sb;
    disk.seekg(partStart);
    disk.read(reinterpret_cast<char*>(&sb), sizeof(SuperBlock));

    // Split path into components
    std::vector<std::string> parts;
    std::istringstream ss(path);
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (!part.empty()) parts.push_back(part);
    }

    int currentInode = 0; // root
    std::string builtPath = "";

    for (size_t i = 0; i < parts.size(); i++) {
        builtPath += "/" + parts[i];
        int nextInode = findInodeByPath2(disk, sb, builtPath);

        if (nextInode == -1) {
            // Path doesn't exist
            if (!recursive && i < parts.size() - 1) {
                disk.close();
                return "ERROR mkdir: La carpeta padre no existe: " + builtPath + ". Use -p para crearla.";
            }
            if (!recursive && i == parts.size() - 1) {
                // Check if immediate parent exists
                std::string parentPath = "";
                for (size_t j = 0; j < i; j++) parentPath += "/" + parts[j];
                if (parentPath.empty()) parentPath = "/";
                int parentInode = findInodeByPath2(disk, sb, parentPath);
                if (parentInode == -1) {
                    disk.close();
                    return "ERROR mkdir: La carpeta padre no existe: " + parentPath;
                }
                currentInode = parentInode;
            }

            // Create folder
            int newInode = createFolderInode(disk, sb, currentInode);
            if (newInode == -1) {
                disk.close();
                return "ERROR mkdir: No hay espacio disponible para crear la carpeta.";
            }

            if (!addEntryToFolder(disk, sb, currentInode, parts[i], newInode)) {
                disk.close();
                return "ERROR mkdir: No se pudo agregar la carpeta al directorio padre.";
            }

            currentInode = newInode;
        } else {
            currentInode = nextInode;
        }
    }

    // Update superblock
    disk.seekp(partStart);
    disk.write(reinterpret_cast<char*>(&sb), sizeof(SuperBlock));
    disk.close();

    return "MKDIR: Carpeta '" + path + "' creada exitosamente.";
}

// ==================== MKFILE ====================

std::string cmd_mkfile(const ParsedCommand& cmd) {
    if (!Globals::current_session.active) return "ERROR mkfile: No hay sesion activa.";
    if (cmd.params.find("path") == cmd.params.end()) return "ERROR mkfile: Parametro -path es obligatorio.";

    std::string path = cmd.params.at("path");
    bool recursive = false;
    for (auto& f : cmd.flags) {
        if (f == "r") recursive = true;
    }

    int fileSize = 0;
    if (cmd.params.find("size") != cmd.params.end()) {
        try {
            fileSize = std::stoi(cmd.params.at("size"));
        } catch (...) {
            return "ERROR mkfile: El parametro -size debe ser un numero.";
        }
        if (fileSize < 0) return "ERROR mkfile: El parametro -size no puede ser negativo.";
    }

    std::string contFile = "";
    if (cmd.params.find("cont") != cmd.params.end()) {
        contFile = cmd.params.at("cont");
    }

    std::string diskPath = Globals::current_session.disk_path;
    int partStart = Globals::current_session.part_start;

    std::fstream disk(diskPath, std::ios::in | std::ios::out | std::ios::binary);
    if (!disk.is_open()) return "ERROR mkfile: No se pudo abrir el disco.";

    SuperBlock sb;
    disk.seekg(partStart);
    disk.read(reinterpret_cast<char*>(&sb), sizeof(SuperBlock));

    // Split path into dir parts and filename
    std::vector<std::string> parts;
    std::istringstream ss(path);
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (!part.empty()) parts.push_back(part);
    }

    if (parts.empty()) {
        disk.close();
        return "ERROR mkfile: Ruta invalida.";
    }

    std::string fileName = parts.back();
    parts.pop_back(); // Remove filename, keep directories

    // Navigate/create parent directories
    int currentInode = 0; // root
    std::string builtPath = "";

    for (size_t i = 0; i < parts.size(); i++) {
        builtPath += "/" + parts[i];
        int nextInode = findInodeByPath2(disk, sb, builtPath);

        if (nextInode == -1) {
            if (!recursive) {
                disk.close();
                return "ERROR mkfile: La carpeta padre no existe: " + builtPath + ". Use -r para crearla.";
            }
            // Create folder
            int newInode = createFolderInode(disk, sb, currentInode);
            if (newInode == -1) {
                disk.close();
                return "ERROR mkfile: No hay espacio para crear carpetas padre.";
            }
            addEntryToFolder(disk, sb, currentInode, parts[i], newInode);
            currentInode = newInode;
        } else {
            currentInode = nextInode;
        }
    }

    // Generate file content
    std::string content;
    if (!contFile.empty()) {
        // Read content from real file on disk
        std::ifstream realFile(contFile);
        if (!realFile.is_open()) {
            disk.close();
            return "ERROR mkfile: No se encontro el archivo de contenido: " + contFile;
        }
        std::ostringstream oss;
        oss << realFile.rdbuf();
        content = oss.str();
        realFile.close();
    } else if (fileSize > 0) {
        // Generate 0-9 repeating
        content.resize(fileSize);
        for (int i = 0; i < fileSize; i++) {
            content[i] = '0' + (i % 10);
        }
    }

    // Create file inode
    int fileInodeIdx = allocateInode(disk, sb);
    if (fileInodeIdx == -1) {
        disk.close();
        return "ERROR mkfile: No hay inodos disponibles.";
    }

    Inode fileInode;
    fileInode.i_uid = Globals::current_session.uid;
    fileInode.i_gid = Globals::current_session.gid;
    fileInode.i_s = content.size();
    fileInode.i_atime = time(nullptr);
    fileInode.i_ctime = time(nullptr);
    fileInode.i_mtime = time(nullptr);
    fileInode.i_type = '1'; // file
    fileInode.i_perm[0] = '6';
    fileInode.i_perm[1] = '6';
    fileInode.i_perm[2] = '4';

    // Write file content to blocks
    int bytesWritten = 0;
    int blockCount = 0;

    // Direct blocks (0-11)
    while (bytesWritten < (int)content.size() && blockCount < 12) {
        int blockIdx = allocateBlock(disk, sb);
        if (blockIdx == -1) break;
        fileInode.i_block[blockCount] = blockIdx;

        FileBlock fb;
        memset(fb.b_content, 0, 64);
        int toWrite = std::min(64, (int)content.size() - bytesWritten);
        memcpy(fb.b_content, content.c_str() + bytesWritten, toWrite);

        disk.seekp(sb.s_block_start + blockIdx * sizeof(FileBlock));
        disk.write(reinterpret_cast<char*>(&fb), sizeof(FileBlock));

        bytesWritten += toWrite;
        blockCount++;
    }

    // Single indirect (block 12)
    if (bytesWritten < (int)content.size()) {
        int ptrBlockIdx = allocateBlock(disk, sb);
        if (ptrBlockIdx != -1) {
            fileInode.i_block[12] = ptrBlockIdx;
            PointerBlock pb;

            int p = 0;
            while (bytesWritten < (int)content.size() && p < 16) {
                int blockIdx = allocateBlock(disk, sb);
                if (blockIdx == -1) break;
                pb.b_pointers[p] = blockIdx;

                FileBlock fb;
                memset(fb.b_content, 0, 64);
                int toWrite = std::min(64, (int)content.size() - bytesWritten);
                memcpy(fb.b_content, content.c_str() + bytesWritten, toWrite);

                disk.seekp(sb.s_block_start + blockIdx * sizeof(FileBlock));
                disk.write(reinterpret_cast<char*>(&fb), sizeof(FileBlock));

                bytesWritten += toWrite;
                p++;
            }

            disk.seekp(sb.s_block_start + ptrBlockIdx * sizeof(FileBlock));
            disk.write(reinterpret_cast<char*>(&pb), sizeof(PointerBlock));
        }
    }

    // Double indirect (block 13)
    if (bytesWritten < (int)content.size()) {
        int ptrBlockIdx1 = allocateBlock(disk, sb);
        if (ptrBlockIdx1 != -1) {
            fileInode.i_block[13] = ptrBlockIdx1;
            PointerBlock pb1;

            int p1 = 0;
            while (bytesWritten < (int)content.size() && p1 < 16) {
                int ptrBlockIdx2 = allocateBlock(disk, sb);
                if (ptrBlockIdx2 == -1) break;
                pb1.b_pointers[p1] = ptrBlockIdx2;
                PointerBlock pb2;

                int p2 = 0;
                while (bytesWritten < (int)content.size() && p2 < 16) {
                    int blockIdx = allocateBlock(disk, sb);
                    if (blockIdx == -1) break;
                    pb2.b_pointers[p2] = blockIdx;

                    FileBlock fb;
                    memset(fb.b_content, 0, 64);
                    int toWrite = std::min(64, (int)content.size() - bytesWritten);
                    memcpy(fb.b_content, content.c_str() + bytesWritten, toWrite);

                    disk.seekp(sb.s_block_start + blockIdx * sizeof(FileBlock));
                    disk.write(reinterpret_cast<char*>(&fb), sizeof(FileBlock));

                    bytesWritten += toWrite;
                    p2++;
                }

                disk.seekp(sb.s_block_start + ptrBlockIdx2 * sizeof(FileBlock));
                disk.write(reinterpret_cast<char*>(&pb2), sizeof(PointerBlock));
                p1++;
            }

            disk.seekp(sb.s_block_start + ptrBlockIdx1 * sizeof(FileBlock));
            disk.write(reinterpret_cast<char*>(&pb1), sizeof(PointerBlock));
        }
    }

    // Triple indirect (block 14)
    if (bytesWritten < (int)content.size()) {
        int ptrBlockIdx1 = allocateBlock(disk, sb);
        if (ptrBlockIdx1 != -1) {
            fileInode.i_block[14] = ptrBlockIdx1;
            PointerBlock pb1;

            int p1 = 0;
            while (bytesWritten < (int)content.size() && p1 < 16) {
                int ptrBlockIdx2 = allocateBlock(disk, sb);
                if (ptrBlockIdx2 == -1) break;
                pb1.b_pointers[p1] = ptrBlockIdx2;
                PointerBlock pb2;

                int p2 = 0;
                while (bytesWritten < (int)content.size() && p2 < 16) {
                    int ptrBlockIdx3 = allocateBlock(disk, sb);
                    if (ptrBlockIdx3 == -1) break;
                    pb2.b_pointers[p2] = ptrBlockIdx3;
                    PointerBlock pb3;

                    int p3 = 0;
                    while (bytesWritten < (int)content.size() && p3 < 16) {
                        int blockIdx = allocateBlock(disk, sb);
                        if (blockIdx == -1) break;
                        pb3.b_pointers[p3] = blockIdx;

                        FileBlock fb;
                        memset(fb.b_content, 0, 64);
                        int toWrite = std::min(64, (int)content.size() - bytesWritten);
                        memcpy(fb.b_content, content.c_str() + bytesWritten, toWrite);

                        disk.seekp(sb.s_block_start + blockIdx * sizeof(FileBlock));
                        disk.write(reinterpret_cast<char*>(&fb), sizeof(FileBlock));

                        bytesWritten += toWrite;
                        p3++;
                    }

                    disk.seekp(sb.s_block_start + ptrBlockIdx3 * sizeof(FileBlock));
                    disk.write(reinterpret_cast<char*>(&pb3), sizeof(PointerBlock));
                    p2++;
                }

                disk.seekp(sb.s_block_start + ptrBlockIdx2 * sizeof(FileBlock));
                disk.write(reinterpret_cast<char*>(&pb2), sizeof(PointerBlock));
                p1++;
            }

            disk.seekp(sb.s_block_start + ptrBlockIdx1 * sizeof(FileBlock));
            disk.write(reinterpret_cast<char*>(&pb1), sizeof(PointerBlock));
        }
    }

    // Write file inode
    disk.seekp(sb.s_inode_start + fileInodeIdx * sizeof(Inode));
    disk.write(reinterpret_cast<char*>(&fileInode), sizeof(Inode));

    // Add entry to parent folder
    addEntryToFolder(disk, sb, currentInode, fileName, fileInodeIdx);

    // Update superblock
    disk.seekp(partStart);
    disk.write(reinterpret_cast<char*>(&sb), sizeof(SuperBlock));
    disk.close();

    return "MKFILE: Archivo '" + path + "' creado exitosamente (" + std::to_string(content.size()) + " bytes).";
}
