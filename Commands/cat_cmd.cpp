#include "parser.h"
#include "../Structures/structs.h"
#include "../Utils/globals.h"
#include <fstream>
#include <cstring>
#include <sstream>
#include <vector>

// Helper: read file content from EXT2 path
static std::string readFileFromExt2(const std::string& diskPath, int partStart, const std::string& filePath);

// Helper: find inode by path
static int findInodeByPath(std::fstream& disk, const SuperBlock& sb, const std::string& path) {
    if (path == "/") return 0;

    // Split path
    std::vector<std::string> parts;
    std::istringstream ss(path);
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (!part.empty()) parts.push_back(part);
    }

    int currentInode = 0; // Start at root

    for (const auto& name : parts) {
        Inode inode;
        disk.seekg(sb.s_inode_start + currentInode * sizeof(Inode));
        disk.read(reinterpret_cast<char*>(&inode), sizeof(Inode));

        if (inode.i_type != '0') return -1; // Not a folder

        bool found = false;
        for (int i = 0; i < 12; i++) {
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
            if (found) break;
        }
        // Check indirect pointers (block 12 = single indirect)
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

// Read file content from an inode
static std::string readFileContent(std::fstream& disk, const SuperBlock& sb, int inodeIdx) {
    Inode inode;
    disk.seekg(sb.s_inode_start + inodeIdx * sizeof(Inode));
    disk.read(reinterpret_cast<char*>(&inode), sizeof(Inode));

    if (inode.i_type != '1') return ""; // Not a file

    std::string content;
    // Direct blocks
    for (int i = 0; i < 12; i++) {
        if (inode.i_block[i] == -1) break;
        FileBlock fb;
        disk.seekg(sb.s_block_start + inode.i_block[i] * sizeof(FileBlock));
        disk.read(reinterpret_cast<char*>(&fb), sizeof(FileBlock));
        int toRead = std::min(64, inode.i_s - (int)content.size());
        if (toRead > 0) content.append(fb.b_content, toRead);
    }
    // Single indirect (block 12)
    if (inode.i_block[12] != -1 && (int)content.size() < inode.i_s) {
        PointerBlock pb;
        disk.seekg(sb.s_block_start + inode.i_block[12] * sizeof(FileBlock));
        disk.read(reinterpret_cast<char*>(&pb), sizeof(PointerBlock));
        for (int p = 0; p < 16; p++) {
            if (pb.b_pointers[p] == -1 || (int)content.size() >= inode.i_s) break;
            FileBlock fb;
            disk.seekg(sb.s_block_start + pb.b_pointers[p] * sizeof(FileBlock));
            disk.read(reinterpret_cast<char*>(&fb), sizeof(FileBlock));
            int toRead = std::min(64, inode.i_s - (int)content.size());
            if (toRead > 0) content.append(fb.b_content, toRead);
        }
    }
    // Double indirect (block 13)
    if (inode.i_block[13] != -1 && (int)content.size() < inode.i_s) {
        PointerBlock pb1;
        disk.seekg(sb.s_block_start + inode.i_block[13] * sizeof(FileBlock));
        disk.read(reinterpret_cast<char*>(&pb1), sizeof(PointerBlock));
        for (int p1 = 0; p1 < 16 && (int)content.size() < inode.i_s; p1++) {
            if (pb1.b_pointers[p1] == -1) break;
            PointerBlock pb2;
            disk.seekg(sb.s_block_start + pb1.b_pointers[p1] * sizeof(FileBlock));
            disk.read(reinterpret_cast<char*>(&pb2), sizeof(PointerBlock));
            for (int p2 = 0; p2 < 16 && (int)content.size() < inode.i_s; p2++) {
                if (pb2.b_pointers[p2] == -1) break;
                FileBlock fb;
                disk.seekg(sb.s_block_start + pb2.b_pointers[p2] * sizeof(FileBlock));
                disk.read(reinterpret_cast<char*>(&fb), sizeof(FileBlock));
                int toRead = std::min(64, inode.i_s - (int)content.size());
                if (toRead > 0) content.append(fb.b_content, toRead);
            }
        }
    }
    // Triple indirect (block 14)
    if (inode.i_block[14] != -1 && (int)content.size() < inode.i_s) {
        PointerBlock pb1;
        disk.seekg(sb.s_block_start + inode.i_block[14] * sizeof(FileBlock));
        disk.read(reinterpret_cast<char*>(&pb1), sizeof(PointerBlock));
        for (int p1 = 0; p1 < 16 && (int)content.size() < inode.i_s; p1++) {
            if (pb1.b_pointers[p1] == -1) break;
            PointerBlock pb2;
            disk.seekg(sb.s_block_start + pb1.b_pointers[p1] * sizeof(FileBlock));
            disk.read(reinterpret_cast<char*>(&pb2), sizeof(PointerBlock));
            for (int p2 = 0; p2 < 16 && (int)content.size() < inode.i_s; p2++) {
                if (pb2.b_pointers[p2] == -1) break;
                PointerBlock pb3;
                disk.seekg(sb.s_block_start + pb2.b_pointers[p2] * sizeof(FileBlock));
                disk.read(reinterpret_cast<char*>(&pb3), sizeof(PointerBlock));
                for (int p3 = 0; p3 < 16 && (int)content.size() < inode.i_s; p3++) {
                    if (pb3.b_pointers[p3] == -1) break;
                    FileBlock fb;
                    disk.seekg(sb.s_block_start + pb3.b_pointers[p3] * sizeof(FileBlock));
                    disk.read(reinterpret_cast<char*>(&fb), sizeof(FileBlock));
                    int toRead = std::min(64, inode.i_s - (int)content.size());
                    if (toRead > 0) content.append(fb.b_content, toRead);
                }
            }
        }
    }

    return content;
}

std::string cmd_cat(const ParsedCommand& cmd) {
    if (!Globals::current_session.active) return "ERROR cat: No hay sesion activa.";

    std::string diskPath = Globals::current_session.disk_path;
    int partStart = Globals::current_session.part_start;

    std::fstream disk(diskPath, std::ios::in | std::ios::binary);
    if (!disk.is_open()) return "ERROR cat: No se pudo abrir el disco.";

    SuperBlock sb;
    disk.seekg(partStart);
    disk.read(reinterpret_cast<char*>(&sb), sizeof(SuperBlock));

    std::string result;
    // Support -file1, -file2, -file3, etc.
    for (int i = 1; i <= 20; i++) {
        std::string key = "file" + std::to_string(i);
        if (cmd.params.find(key) == cmd.params.end()) {
            if (i == 1) {
                disk.close();
                return "ERROR cat: Se requiere al menos -file1.";
            }
            break;
        }

        std::string filePath = cmd.params.at(key);
        int inodeIdx = findInodeByPath(disk, sb, filePath);
        if (inodeIdx == -1) {
            if (!result.empty()) result += "\n";
            result += "ERROR cat: No se encontro el archivo '" + filePath + "'.";
            continue;
        }

        std::string content = readFileContent(disk, sb, inodeIdx);
        if (!result.empty()) result += "\n";
        result += "CAT " + filePath + ":\n" + content;
    }

    disk.close();
    return result;
}
