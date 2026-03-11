#include "parser.h"
#include "../Structures/structs.h"
#include "../Utils/globals.h"
#include <fstream>
#include <cstring>
#include <algorithm>

static std::string toLowerStr(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::tolower);
    return r;
}

std::string cmd_fdisk(const ParsedCommand& cmd) {
    // Validate required params
    if (cmd.params.find("path") == cmd.params.end()) {
        return "ERROR fdisk: Parametro -path es obligatorio.";
    }
    if (cmd.params.find("name") == cmd.params.end()) {
        return "ERROR fdisk: Parametro -name es obligatorio.";
    }
    if (cmd.params.find("size") == cmd.params.end()) {
        return "ERROR fdisk: Parametro -size es obligatorio.";
    }

    std::string path = cmd.params.at("path");
    std::string name = cmd.params.at("name");

    int size;
    try {
        size = std::stoi(cmd.params.at("size"));
    } catch (...) {
        return "ERROR fdisk: El parametro -size debe ser un numero valido.";
    }
    if (size <= 0) {
        return "ERROR fdisk: El parametro -size debe ser mayor que 0.";
    }

    // Parse unit (default K)
    char unit = 'K';
    if (cmd.params.find("unit") != cmd.params.end()) {
        std::string u = toLowerStr(cmd.params.at("unit"));
        if (u == "b") unit = 'B';
        else if (u == "k") unit = 'K';
        else if (u == "m") unit = 'M';
        else return "ERROR fdisk: El parametro -unit solo acepta B, K o M.";
    }

    // Parse type (default P)
    char type = 'P';
    if (cmd.params.find("type") != cmd.params.end()) {
        std::string t = toLowerStr(cmd.params.at("type"));
        if (t == "p") type = 'P';
        else if (t == "e") type = 'E';
        else if (t == "l") type = 'L';
        else return "ERROR fdisk: El parametro -type solo acepta P, E o L.";
    }

    // Parse fit (default WF)
    char fit = 'W';
    if (cmd.params.find("fit") != cmd.params.end()) {
        std::string f = toLowerStr(cmd.params.at("fit"));
        if (f == "bf") fit = 'B';
        else if (f == "ff") fit = 'F';
        else if (f == "wf") fit = 'W';
        else return "ERROR fdisk: El parametro -fit solo acepta BF, FF o WF.";
    }

    // Calculate size in bytes
    int sizeBytes;
    if (unit == 'B') sizeBytes = size;
    else if (unit == 'K') sizeBytes = size * 1024;
    else sizeBytes = size * 1024 * 1024;

    // Open disk file
    std::fstream disk(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!disk.is_open()) {
        return "ERROR fdisk: No se encontro el disco en la ruta: " + path;
    }

    // Read MBR
    MBR mbr;
    disk.seekg(0);
    disk.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

    // Check if partition name already exists
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_s > 0) {
            std::string existingName(mbr.mbr_partitions[i].part_name, strnlen(mbr.mbr_partitions[i].part_name, 16));
            if (existingName == name) {
                disk.close();
                return "ERROR fdisk: Ya existe una particion con el nombre '" + name + "' en este disco.";
            }
            // Also check logical partitions if extended
            if (mbr.mbr_partitions[i].part_type == 'E') {
                int ebrPos = mbr.mbr_partitions[i].part_start;
                while (ebrPos != -1) {
                    EBR ebr;
                    disk.seekg(ebrPos);
                    disk.read(reinterpret_cast<char*>(&ebr), sizeof(EBR));
                    if (ebr.part_s > 0) {
                        std::string logName(ebr.part_name, strnlen(ebr.part_name, 16));
                        if (logName == name) {
                            disk.close();
                            return "ERROR fdisk: Ya existe una particion logica con el nombre '" + name + "'.";
                        }
                    }
                    ebrPos = ebr.part_next;
                }
            }
        }
    }

    if (type == 'P' || type == 'E') {
        // Count existing primary + extended partitions
        int count = 0;
        bool hasExtended = false;
        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_s > 0) {
                count++;
                if (mbr.mbr_partitions[i].part_type == 'E') hasExtended = true;
            }
        }

        if (count >= 4) {
            disk.close();
            return "ERROR fdisk: Ya existen 4 particiones (primarias + extendidas). No se puede crear otra.";
        }
        if (type == 'E' && hasExtended) {
            disk.close();
            return "ERROR fdisk: Ya existe una particion extendida en este disco.";
        }

        // Find available space - collect all used ranges
        struct Range { int start; int end; };
        std::vector<Range> used;
        used.push_back({0, (int)sizeof(MBR)}); // MBR occupies the beginning

        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_s > 0) {
                used.push_back({mbr.mbr_partitions[i].part_start,
                               mbr.mbr_partitions[i].part_start + mbr.mbr_partitions[i].part_s});
            }
        }

        // Sort by start position
        std::sort(used.begin(), used.end(), [](const Range& a, const Range& b) {
            return a.start < b.start;
        });

        // Find gaps (free spaces)
        struct Gap { int start; int size; };
        std::vector<Gap> gaps;
        for (size_t i = 0; i < used.size(); i++) {
            int gapStart = used[i].end;
            int gapEnd = (i + 1 < used.size()) ? used[i + 1].start : mbr.mbr_tamano;
            if (gapEnd - gapStart >= sizeBytes) {
                gaps.push_back({gapStart, gapEnd - gapStart});
            }
        }

        if (gaps.empty()) {
            disk.close();
            return "ERROR fdisk: No hay suficiente espacio en el disco para crear la particion de " + std::to_string(sizeBytes) + " bytes.";
        }

        // Apply fit algorithm
        int startPos;
        if (mbr.dsk_fit == 'F') {
            // First Fit
            startPos = gaps[0].start;
        } else if (mbr.dsk_fit == 'B') {
            // Best Fit - smallest gap that fits
            int minSize = gaps[0].size;
            startPos = gaps[0].start;
            for (size_t i = 1; i < gaps.size(); i++) {
                if (gaps[i].size < minSize) {
                    minSize = gaps[i].size;
                    startPos = gaps[i].start;
                }
            }
        } else {
            // Worst Fit - largest gap
            int maxSize = gaps[0].size;
            startPos = gaps[0].start;
            for (size_t i = 1; i < gaps.size(); i++) {
                if (gaps[i].size > maxSize) {
                    maxSize = gaps[i].size;
                    startPos = gaps[i].start;
                }
            }
        }

        // Find empty slot in partition table
        int slot = -1;
        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_s == 0) {
                slot = i;
                break;
            }
        }

        // Write partition
        mbr.mbr_partitions[slot].part_status = '0';
        mbr.mbr_partitions[slot].part_type = type;
        mbr.mbr_partitions[slot].part_fit = fit;
        mbr.mbr_partitions[slot].part_start = startPos;
        mbr.mbr_partitions[slot].part_s = sizeBytes;
        mbr.mbr_partitions[slot].part_correlative = -1;
        memset(mbr.mbr_partitions[slot].part_name, 0, 16);
        strncpy(mbr.mbr_partitions[slot].part_name, name.c_str(), 15);
        memset(mbr.mbr_partitions[slot].part_id, 0, 4);

        // If extended, create first EBR
        if (type == 'E') {
            EBR ebr;
            ebr.part_start = startPos;
            ebr.part_s = 0;
            ebr.part_next = -1;
            ebr.part_fit = fit;
            disk.seekp(startPos);
            disk.write(reinterpret_cast<char*>(&ebr), sizeof(EBR));
        }

        // Write updated MBR
        disk.seekp(0);
        disk.write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        disk.close();

        std::string typeStr = (type == 'P') ? "primaria" : "extendida";
        return "FDISK: Particion " + typeStr + " '" + name + "' creada exitosamente (" + std::to_string(sizeBytes) + " bytes).";

    } else if (type == 'L') {
        // Find extended partition
        int extIndex = -1;
        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_type == 'E' && mbr.mbr_partitions[i].part_s > 0) {
                extIndex = i;
                break;
            }
        }
        if (extIndex == -1) {
            disk.close();
            return "ERROR fdisk: No existe una particion extendida. No se puede crear una logica.";
        }

        int extStart = mbr.mbr_partitions[extIndex].part_start;
        int extEnd = extStart + mbr.mbr_partitions[extIndex].part_s;

        // Walk EBR chain
        EBR currentEBR;
        int ebrPos = extStart;
        disk.seekg(ebrPos);
        disk.read(reinterpret_cast<char*>(&currentEBR), sizeof(EBR));

        // If first EBR has no partition (empty extended)
        if (currentEBR.part_s == 0) {
            // Create first logical partition
            int logStart = extStart + (int)sizeof(EBR);
            if (logStart + sizeBytes > extEnd) {
                disk.close();
                return "ERROR fdisk: No hay suficiente espacio en la particion extendida.";
            }
            currentEBR.part_s = sizeBytes;
            currentEBR.part_start = extStart;
            currentEBR.part_fit = fit;
            currentEBR.part_next = -1;
            memset(currentEBR.part_name, 0, 16);
            strncpy(currentEBR.part_name, name.c_str(), 15);

            disk.seekp(ebrPos);
            disk.write(reinterpret_cast<char*>(&currentEBR), sizeof(EBR));
            disk.close();
            return "FDISK: Particion logica '" + name + "' creada exitosamente.";
        }

        // Walk to last EBR
        while (currentEBR.part_next != -1) {
            ebrPos = currentEBR.part_next;
            disk.seekg(ebrPos);
            disk.read(reinterpret_cast<char*>(&currentEBR), sizeof(EBR));
        }

        // Calculate where the new EBR + logical partition goes
        int newEBRPos = currentEBR.part_start + (int)sizeof(EBR) + currentEBR.part_s;
        int newLogStart = newEBRPos + (int)sizeof(EBR);

        if (newLogStart + sizeBytes > extEnd) {
            disk.close();
            return "ERROR fdisk: No hay suficiente espacio en la particion extendida.";
        }

        // Update current EBR's next pointer
        currentEBR.part_next = newEBRPos;
        disk.seekp(ebrPos);
        disk.write(reinterpret_cast<char*>(&currentEBR), sizeof(EBR));

        // Write new EBR
        EBR newEBR;
        newEBR.part_start = newEBRPos;
        newEBR.part_s = sizeBytes;
        newEBR.part_fit = fit;
        newEBR.part_next = -1;
        memset(newEBR.part_name, 0, 16);
        strncpy(newEBR.part_name, name.c_str(), 15);

        disk.seekp(newEBRPos);
        disk.write(reinterpret_cast<char*>(&newEBR), sizeof(EBR));
        disk.close();

        return "FDISK: Particion logica '" + name + "' creada exitosamente.";
    }

    disk.close();
    return "ERROR fdisk: Tipo de particion no valido.";
}
