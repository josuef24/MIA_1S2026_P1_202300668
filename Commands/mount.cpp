#include "parser.h"
#include "../Structures/structs.h"
#include "../Utils/globals.h"
#include <fstream>
#include <cstring>

std::string cmd_mount(const ParsedCommand& cmd) {
    if (cmd.params.find("path") == cmd.params.end()) {
        return "ERROR mount: Parametro -path es obligatorio.";
    }
    if (cmd.params.find("name") == cmd.params.end()) {
        return "ERROR mount: Parametro -name es obligatorio.";
    }

    std::string path = cmd.params.at("path");
    std::string name = cmd.params.at("name");

    // Open disk
    std::fstream disk(path, std::ios::in | std::ios::binary);
    if (!disk.is_open()) {
        return "ERROR mount: No se encontro el disco en la ruta: " + path;
    }

    // Read MBR
    MBR mbr;
    disk.seekg(0);
    disk.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

    // Find the partition by name
    int partIndex = -1;
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_s > 0) {
            std::string pname(mbr.mbr_partitions[i].part_name, strnlen(mbr.mbr_partitions[i].part_name, 16));
            if (pname == name) {
                partIndex = i;
                break;
            }
        }
    }

    if (partIndex == -1) {
        disk.close();
        return "ERROR mount: No se encontro la particion '" + name + "' en el disco.";
    }

    // Check if already mounted
    for (auto& mp : Globals::mounted_partitions) {
        if (mp.path == path && mp.name == name) {
            disk.close();
            return "ERROR mount: La particion '" + name + "' ya esta montada con id " + mp.id + ".";
        }
    }

    // Determine the letter for this disk
    if (Globals::disk_letters.find(path) == Globals::disk_letters.end()) {
        Globals::disk_letters[path] = Globals::next_letter;
        Globals::disk_partition_count[path] = 0;
        Globals::next_letter++;
    }

    char letter = Globals::disk_letters[path];
    Globals::disk_partition_count[path]++;
    int partNum = Globals::disk_partition_count[path];

    // Generate ID: carnet_digits + partNum + letter
    std::string id = Globals::carnet_digits + std::to_string(partNum) + letter;

    // Create mounted partition entry
    MountedPartition mp;
    mp.id = id;
    mp.path = path;
    mp.name = name;
    mp.part_start = mbr.mbr_partitions[partIndex].part_start;
    mp.part_size = mbr.mbr_partitions[partIndex].part_s;
    Globals::mounted_partitions.push_back(mp);

    // Update partition in MBR: set status, correlative, id
    disk.close();
    std::fstream diskW(path, std::ios::in | std::ios::out | std::ios::binary);
    diskW.seekg(0);
    diskW.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

    mbr.mbr_partitions[partIndex].part_status = '1';
    mbr.mbr_partitions[partIndex].part_correlative = partNum;
    memset(mbr.mbr_partitions[partIndex].part_id, 0, 4);
    strncpy(mbr.mbr_partitions[partIndex].part_id, id.c_str(), 4);

    diskW.seekp(0);
    diskW.write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    diskW.close();

    return "MOUNT: Particion '" + name + "' montada exitosamente con id " + id + ".";
}

std::string cmd_mounted(const ParsedCommand& cmd) {
    if (Globals::mounted_partitions.empty()) {
        return "MOUNTED: No hay particiones montadas.";
    }

    std::string result = "MOUNTED: Particiones montadas:\n";
    for (auto& mp : Globals::mounted_partitions) {
        result += "  ID: " + mp.id + " | Disco: " + mp.path + " | Particion: " + mp.name + "\n";
    }
    return result;
}
