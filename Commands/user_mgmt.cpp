#include "parser.h"
#include "../Structures/structs.h"
#include "../Utils/globals.h"
#include <fstream>
#include <cstring>
#include <sstream>
#include <vector>
#include <algorithm>

// Helper to find mounted partition
static MountedPartition* findMounted(const std::string& id) {
    for (auto& mp : Globals::mounted_partitions) {
        if (mp.id == id) return &mp;
    }
    return nullptr;
}

// Read the full content of users.txt from the EXT2 filesystem
static std::string readUsersFile(const std::string& diskPath, int partStart) {
    std::fstream disk(diskPath, std::ios::in | std::ios::binary);
    if (!disk.is_open()) return "";

    // Read SuperBlock
    SuperBlock sb;
    disk.seekg(partStart);
    disk.read(reinterpret_cast<char*>(&sb), sizeof(SuperBlock));

    // Read root inode (inode 0)
    Inode rootInode;
    disk.seekg(sb.s_inode_start);
    disk.read(reinterpret_cast<char*>(&rootInode), sizeof(Inode));

    // Find users.txt in root folder blocks
    for (int i = 0; i < 12; i++) {
        if (rootInode.i_block[i] == -1) continue;

        FolderBlock fb;
        disk.seekg(sb.s_block_start + rootInode.i_block[i] * sizeof(FileBlock));
        disk.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

        for (int j = 0; j < 4; j++) {
            if (fb.b_content[j].b_inodo == -1) continue;
            std::string fname(fb.b_content[j].b_name, strnlen(fb.b_content[j].b_name, 12));
            if (fname == "users.txt") {
                int usersInodeIdx = fb.b_content[j].b_inodo;

                // Read users inode
                Inode usersInode;
                disk.seekg(sb.s_inode_start + usersInodeIdx * sizeof(Inode));
                disk.read(reinterpret_cast<char*>(&usersInode), sizeof(Inode));

                // Read content from file blocks
                std::string content;
                for (int k = 0; k < 12; k++) {
                    if (usersInode.i_block[k] == -1) break;
                    FileBlock fblock;
                    disk.seekg(sb.s_block_start + usersInode.i_block[k] * sizeof(FileBlock));
                    disk.read(reinterpret_cast<char*>(&fblock), sizeof(FileBlock));
                    int toRead = std::min(64, usersInode.i_s - (int)content.size());
                    if (toRead > 0)
                        content.append(fblock.b_content, toRead);
                }
                disk.close();
                return content;
            }
        }
    }
    disk.close();
    return "";
}

// Write the users.txt content back to the EXT2 filesystem
static bool writeUsersFile(const std::string& diskPath, int partStart, const std::string& content) {
    std::fstream disk(diskPath, std::ios::in | std::ios::out | std::ios::binary);
    if (!disk.is_open()) return false;

    SuperBlock sb;
    disk.seekg(partStart);
    disk.read(reinterpret_cast<char*>(&sb), sizeof(SuperBlock));

    // Find users.txt inode
    Inode rootInode;
    disk.seekg(sb.s_inode_start);
    disk.read(reinterpret_cast<char*>(&rootInode), sizeof(Inode));

    int usersInodeIdx = -1;
    for (int i = 0; i < 12; i++) {
        if (rootInode.i_block[i] == -1) continue;
        FolderBlock fb;
        disk.seekg(sb.s_block_start + rootInode.i_block[i] * sizeof(FileBlock));
        disk.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
        for (int j = 0; j < 4; j++) {
            if (fb.b_content[j].b_inodo == -1) continue;
            std::string fname(fb.b_content[j].b_name, strnlen(fb.b_content[j].b_name, 12));
            if (fname == "users.txt") {
                usersInodeIdx = fb.b_content[j].b_inodo;
                break;
            }
        }
        if (usersInodeIdx != -1) break;
    }

    if (usersInodeIdx == -1) {
        disk.close();
        return false;
    }

    // Read users inode
    Inode usersInode;
    disk.seekg(sb.s_inode_start + usersInodeIdx * sizeof(Inode));
    disk.read(reinterpret_cast<char*>(&usersInode), sizeof(Inode));

    // Calculate how many blocks needed
    int blocksNeeded = (content.size() + 63) / 64;
    if (blocksNeeded == 0) blocksNeeded = 1;

    // Write content block by block
    int written = 0;
    for (int i = 0; i < blocksNeeded && i < 12; i++) {
        int blockIdx = usersInode.i_block[i];

        // If block not allocated, allocate new one
        if (blockIdx == -1) {
            blockIdx = sb.s_first_blo;
            usersInode.i_block[i] = blockIdx;
            // Mark in bitmap
            char one = '1';
            disk.seekp(sb.s_bm_block_start + blockIdx);
            disk.write(&one, 1);
            sb.s_first_blo++;
            // Find next free block
            while (sb.s_first_blo < sb.s_blocks_count) {
                char bit;
                disk.seekg(sb.s_bm_block_start + sb.s_first_blo);
                disk.read(&bit, 1);
                if (bit == '\0' || bit == '0') break;
                sb.s_first_blo++;
            }
            sb.s_free_blocks_count--;
        }

        FileBlock fblock;
        memset(fblock.b_content, 0, 64);
        int toWrite = std::min(64, (int)content.size() - written);
        memcpy(fblock.b_content, content.c_str() + written, toWrite);
        disk.seekp(sb.s_block_start + blockIdx * sizeof(FileBlock));
        disk.write(reinterpret_cast<char*>(&fblock), sizeof(FileBlock));
        written += toWrite;
    }

    // Update inode size
    usersInode.i_s = content.size();
    usersInode.i_mtime = time(nullptr);
    disk.seekp(sb.s_inode_start + usersInodeIdx * sizeof(Inode));
    disk.write(reinterpret_cast<char*>(&usersInode), sizeof(Inode));

    // Update superblock
    disk.seekp(partStart);
    disk.write(reinterpret_cast<char*>(&sb), sizeof(SuperBlock));

    disk.close();
    return true;
}

std::string cmd_login(const ParsedCommand& cmd) {
    if (cmd.params.find("user") == cmd.params.end()) return "ERROR login: Parametro -user es obligatorio.";
    if (cmd.params.find("pass") == cmd.params.end()) return "ERROR login: Parametro -pass es obligatorio.";
    if (cmd.params.find("id") == cmd.params.end()) return "ERROR login: Parametro -id es obligatorio.";

    if (Globals::current_session.active) {
        return "ERROR login: Ya hay una sesion activa. Debe cerrar sesion con logout primero.";
    }

    std::string user = cmd.params.at("user");
    std::string pass = cmd.params.at("pass");
    std::string id = cmd.params.at("id");

    MountedPartition* mp = findMounted(id);
    if (!mp) return "ERROR login: No se encontro la particion montada con id " + id + ".";

    // Read users.txt
    std::string usersContent = readUsersFile(mp->path, mp->part_start);
    if (usersContent.empty()) {
        return "ERROR login: No se pudo leer users.txt.";
    }

    // Parse users.txt
    std::istringstream stream(usersContent);
    std::string line;
    bool found = false;
    int uid = -1, gid = -1;
    std::string userGrp;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        // Remove trailing \r if present
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // Parse: UID, U, group, username, password
        std::vector<std::string> parts;
        std::istringstream lineStream(line);
        std::string part;
        while (std::getline(lineStream, part, ',')) {
            // Trim whitespace
            size_t s = part.find_first_not_of(" \t");
            size_t e = part.find_last_not_of(" \t");
            if (s != std::string::npos) parts.push_back(part.substr(s, e - s + 1));
            else parts.push_back("");
        }

        if (parts.size() < 3) continue;
        if (parts[0] == "0") continue; // Deleted

        if (parts[1] == "U" && parts.size() >= 5) {
            if (parts[3] == user && parts[4] == pass) {
                uid = std::stoi(parts[0]);
                userGrp = parts[2];
                found = true;

                // Find group ID
                std::istringstream stream2(usersContent);
                std::string line2;
                while (std::getline(stream2, line2)) {
                    if (line2.empty()) continue;
                    if (!line2.empty() && line2.back() == '\r') line2.pop_back();
                    std::vector<std::string> parts2;
                    std::istringstream ls2(line2);
                    std::string p2;
                    while (std::getline(ls2, p2, ',')) {
                        size_t s = p2.find_first_not_of(" \t");
                        size_t e = p2.find_last_not_of(" \t");
                        if (s != std::string::npos) parts2.push_back(p2.substr(s, e - s + 1));
                        else parts2.push_back("");
                    }
                    if (parts2.size() >= 3 && parts2[1] == "G" && parts2[0] != "0" && parts2[2] == userGrp) {
                        gid = std::stoi(parts2[0]);
                        break;
                    }
                }
                break;
            } else if (parts[3] == user && parts[4] != pass) {
                return "ERROR login: Autenticacion fallida. Contrasena incorrecta.";
            }
        }
    }

    if (!found) return "ERROR login: El usuario '" + user + "' no existe.";

    Globals::current_session.active = true;
    Globals::current_session.user = user;
    Globals::current_session.grp = userGrp;
    Globals::current_session.uid = uid;
    Globals::current_session.gid = gid;
    Globals::current_session.id_partition = id;
    Globals::current_session.disk_path = mp->path;
    Globals::current_session.part_start = mp->part_start;
    Globals::current_session.part_size = mp->part_size;

    return "LOGIN: Sesion iniciada como '" + user + "' en particion " + id + ".";
}

std::string cmd_logout(const ParsedCommand& cmd) {
    if (!Globals::current_session.active) {
        return "ERROR logout: No hay ninguna sesion activa.";
    }
    Globals::current_session.active = false;
    Globals::current_session.user = "";
    Globals::current_session.grp = "";
    Globals::current_session.uid = -1;
    Globals::current_session.gid = -1;
    Globals::current_session.id_partition = "";
    return "LOGOUT: Sesion cerrada exitosamente.";
}

std::string cmd_mkgrp(const ParsedCommand& cmd) {
    if (!Globals::current_session.active) return "ERROR mkgrp: No hay sesion activa.";
    if (Globals::current_session.user != "root") return "ERROR mkgrp: Solo el usuario root puede crear grupos.";
    if (cmd.params.find("name") == cmd.params.end()) return "ERROR mkgrp: Parametro -name es obligatorio.";

    std::string name = cmd.params.at("name");
    std::string diskPath = Globals::current_session.disk_path;
    int partStart = Globals::current_session.part_start;

    std::string usersContent = readUsersFile(diskPath, partStart);
    if (usersContent.empty()) return "ERROR mkgrp: No se pudo leer users.txt.";

    // Check if group already exists
    std::istringstream stream(usersContent);
    std::string line;
    int maxGid = 0;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> parts;
        std::istringstream ls(line);
        std::string p;
        while (std::getline(ls, p, ',')) {
            size_t s = p.find_first_not_of(" \t");
            size_t e = p.find_last_not_of(" \t");
            if (s != std::string::npos) parts.push_back(p.substr(s, e - s + 1));
            else parts.push_back("");
        }
        if (parts.size() >= 3 && parts[1] == "G") {
            int gid = std::stoi(parts[0]);
            if (gid > maxGid) maxGid = gid;
            if (gid != 0 && parts[2] == name) {
                return "ERROR mkgrp: El grupo '" + name + "' ya existe.";
            }
        }
    }

    // Append new group
    int newGid = maxGid + 1;
    usersContent += std::to_string(newGid) + ",G," + name + "\n";

    if (!writeUsersFile(diskPath, partStart, usersContent)) {
        return "ERROR mkgrp: No se pudo escribir users.txt.";
    }

    return "MKGRP: Grupo '" + name + "' creado exitosamente (GID=" + std::to_string(newGid) + ").";
}

std::string cmd_rmgrp(const ParsedCommand& cmd) {
    if (!Globals::current_session.active) return "ERROR rmgrp: No hay sesion activa.";
    if (Globals::current_session.user != "root") return "ERROR rmgrp: Solo el usuario root puede eliminar grupos.";
    if (cmd.params.find("name") == cmd.params.end()) return "ERROR rmgrp: Parametro -name es obligatorio.";

    std::string name = cmd.params.at("name");
    std::string diskPath = Globals::current_session.disk_path;
    int partStart = Globals::current_session.part_start;

    std::string usersContent = readUsersFile(diskPath, partStart);

    // Find and mark group as deleted (id = 0)
    std::istringstream stream(usersContent);
    std::string line;
    std::string newContent;
    bool found = false;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        std::string origLine = line;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::vector<std::string> parts;
        std::istringstream ls(line);
        std::string p;
        while (std::getline(ls, p, ',')) {
            size_t s = p.find_first_not_of(" \t");
            size_t e = p.find_last_not_of(" \t");
            if (s != std::string::npos) parts.push_back(p.substr(s, e - s + 1));
            else parts.push_back("");
        }

        if (parts.size() >= 3 && parts[1] == "G" && parts[0] != "0" && parts[2] == name) {
            newContent += "0,G," + name + "\n";
            found = true;
        } else {
            newContent += line + "\n";
        }
    }

    if (!found) return "ERROR rmgrp: El grupo '" + name + "' no existe.";

    writeUsersFile(diskPath, partStart, newContent);
    return "RMGRP: Grupo '" + name + "' eliminado exitosamente.";
}

std::string cmd_mkusr(const ParsedCommand& cmd) {
    if (!Globals::current_session.active) return "ERROR mkusr: No hay sesion activa.";
    if (Globals::current_session.user != "root") return "ERROR mkusr: Solo el usuario root puede crear usuarios.";
    if (cmd.params.find("user") == cmd.params.end()) return "ERROR mkusr: Parametro -user es obligatorio.";
    if (cmd.params.find("pass") == cmd.params.end()) return "ERROR mkusr: Parametro -pass es obligatorio.";
    if (cmd.params.find("grp") == cmd.params.end()) return "ERROR mkusr: Parametro -grp es obligatorio.";

    std::string user = cmd.params.at("user");
    std::string pass = cmd.params.at("pass");
    std::string grp = cmd.params.at("grp");
    std::string diskPath = Globals::current_session.disk_path;
    int partStart = Globals::current_session.part_start;

    std::string usersContent = readUsersFile(diskPath, partStart);

    // Check if group exists
    bool grpExists = false;
    int maxUid = 0;
    {
        std::istringstream stream(usersContent);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.empty()) continue;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::vector<std::string> parts;
            std::istringstream ls(line);
            std::string p;
            while (std::getline(ls, p, ',')) {
                size_t s = p.find_first_not_of(" \t");
                size_t e = p.find_last_not_of(" \t");
                if (s != std::string::npos) parts.push_back(p.substr(s, e - s + 1));
                else parts.push_back("");
            }
            if (parts.size() >= 3 && parts[1] == "G" && parts[0] != "0" && parts[2] == grp) {
                grpExists = true;
            }
            if (parts.size() >= 5 && parts[1] == "U") {
                int uid = std::stoi(parts[0]);
                if (uid > maxUid) maxUid = uid;
                if (parts[0] != "0" && parts[3] == user) {
                    return "ERROR mkusr: El usuario '" + user + "' ya existe.";
                }
            }
        }
    }

    if (!grpExists) return "ERROR mkusr: El grupo '" + grp + "' no existe.";

    int newUid = maxUid + 1;
    usersContent += std::to_string(newUid) + ",U," + grp + "," + user + "," + pass + "\n";

    writeUsersFile(diskPath, partStart, usersContent);
    return "MKUSR: Usuario '" + user + "' creado exitosamente.";
}

std::string cmd_rmusr(const ParsedCommand& cmd) {
    if (!Globals::current_session.active) return "ERROR rmusr: No hay sesion activa.";
    if (Globals::current_session.user != "root") return "ERROR rmusr: Solo el usuario root puede eliminar usuarios.";
    if (cmd.params.find("user") == cmd.params.end()) return "ERROR rmusr: Parametro -user es obligatorio.";

    std::string user = cmd.params.at("user");
    std::string diskPath = Globals::current_session.disk_path;
    int partStart = Globals::current_session.part_start;

    std::string usersContent = readUsersFile(diskPath, partStart);

    std::istringstream stream(usersContent);
    std::string line;
    std::string newContent;
    bool found = false;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> parts;
        std::istringstream ls(line);
        std::string p;
        while (std::getline(ls, p, ',')) {
            size_t s = p.find_first_not_of(" \t");
            size_t e = p.find_last_not_of(" \t");
            if (s != std::string::npos) parts.push_back(p.substr(s, e - s + 1));
            else parts.push_back("");
        }
        if (parts.size() >= 5 && parts[1] == "U" && parts[0] != "0" && parts[3] == user) {
            newContent += "0,U," + parts[2] + "," + user + "," + parts[4] + "\n";
            found = true;
        } else {
            newContent += line + "\n";
        }
    }

    if (!found) return "ERROR rmusr: El usuario '" + user + "' no existe.";

    writeUsersFile(diskPath, partStart, newContent);
    return "RMUSR: Usuario '" + user + "' eliminado exitosamente.";
}

std::string cmd_chgrp(const ParsedCommand& cmd) {
    if (!Globals::current_session.active) return "ERROR chgrp: No hay sesion activa.";
    if (Globals::current_session.user != "root") return "ERROR chgrp: Solo el usuario root puede cambiar grupos.";
    if (cmd.params.find("user") == cmd.params.end()) return "ERROR chgrp: Parametro -user es obligatorio.";
    if (cmd.params.find("grp") == cmd.params.end()) return "ERROR chgrp: Parametro -grp es obligatorio.";

    std::string user = cmd.params.at("user");
    std::string grp = cmd.params.at("grp");
    std::string diskPath = Globals::current_session.disk_path;
    int partStart = Globals::current_session.part_start;

    std::string usersContent = readUsersFile(diskPath, partStart);

    // Check if group exists
    bool grpExists = false;
    {
        std::istringstream stream(usersContent);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.empty()) continue;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::vector<std::string> parts;
            std::istringstream ls(line);
            std::string p;
            while (std::getline(ls, p, ',')) {
                size_t s = p.find_first_not_of(" \t");
                size_t e = p.find_last_not_of(" \t");
                if (s != std::string::npos) parts.push_back(p.substr(s, e - s + 1));
                else parts.push_back("");
            }
            if (parts.size() >= 3 && parts[1] == "G" && parts[0] != "0" && parts[2] == grp) {
                grpExists = true;
                break;
            }
        }
    }
    if (!grpExists) return "ERROR chgrp: El grupo '" + grp + "' no existe.";

    // Change user's group
    std::istringstream stream(usersContent);
    std::string line;
    std::string newContent;
    bool found = false;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> parts;
        std::istringstream ls(line);
        std::string p;
        while (std::getline(ls, p, ',')) {
            size_t s = p.find_first_not_of(" \t");
            size_t e = p.find_last_not_of(" \t");
            if (s != std::string::npos) parts.push_back(p.substr(s, e - s + 1));
            else parts.push_back("");
        }
        if (parts.size() >= 5 && parts[1] == "U" && parts[0] != "0" && parts[3] == user) {
            newContent += parts[0] + ",U," + grp + "," + parts[3] + "," + parts[4] + "\n";
            found = true;
        } else {
            newContent += line + "\n";
        }
    }

    if (!found) return "ERROR chgrp: El usuario '" + user + "' no existe.";

    writeUsersFile(diskPath, partStart, newContent);
    return "CHGRP: Grupo del usuario '" + user + "' cambiado a '" + grp + "'.";
}
