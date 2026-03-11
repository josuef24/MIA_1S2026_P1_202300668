#include "parser.h"
#include "../Structures/structs.h"
#include "../Utils/globals.h"
#include <fstream>
#include <cstring>
#include <ctime>
#include <cstdlib>
#include <sys/stat.h>
#include <algorithm>

static std::string toLowerStr(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::tolower);
    return r;
}

// Create parent directories recursively
static void createParentDirs(const std::string& path) {
    size_t pos = 0;
    while ((pos = path.find('/', pos + 1)) != std::string::npos) {
        std::string dir = path.substr(0, pos);
        mkdir(dir.c_str(), 0777);
    }
}

std::string cmd_mkdisk(const ParsedCommand& cmd) {
    // Validate required params
    if (cmd.params.find("size") == cmd.params.end()) {
        return "ERROR mkdisk: Parametro -size es obligatorio.";
    }
    if (cmd.params.find("path") == cmd.params.end()) {
        return "ERROR mkdisk: Parametro -path es obligatorio.";
    }

    // Parse size
    int size;
    try {
        size = std::stoi(cmd.params.at("size"));
    } catch (...) {
        return "ERROR mkdisk: El parametro -size debe ser un numero valido.";
    }
    if (size <= 0) {
        return "ERROR mkdisk: El parametro -size debe ser mayor que 0.";
    }

    // Parse unit (default M)
    char unit = 'M';
    if (cmd.params.find("unit") != cmd.params.end()) {
        std::string u = toLowerStr(cmd.params.at("unit"));
        if (u == "k") unit = 'K';
        else if (u == "m") unit = 'M';
        else return "ERROR mkdisk: El parametro -unit solo acepta K o M.";
    }

    // Parse fit (default FF)
    char fit = 'F';
    if (cmd.params.find("fit") != cmd.params.end()) {
        std::string f = toLowerStr(cmd.params.at("fit"));
        if (f == "bf") fit = 'B';
        else if (f == "ff") fit = 'F';
        else if (f == "wf") fit = 'W';
        else return "ERROR mkdisk: El parametro -fit solo acepta BF, FF o WF.";
    }

    std::string path = cmd.params.at("path");

    // Calculate size in bytes
    int sizeBytes;
    if (unit == 'K') sizeBytes = size * 1024;
    else sizeBytes = size * 1024 * 1024;

    // Create parent directories
    createParentDirs(path);

    // Create the binary file
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return "ERROR mkdisk: No se pudo crear el disco en la ruta: " + path;
    }

    // Fill with zeros using 1024-byte buffer
    char buffer[1024];
    memset(buffer, 0, 1024);
    int totalBlocks = sizeBytes / 1024;
    int remaining = sizeBytes % 1024;
    for (int i = 0; i < totalBlocks; i++) {
        file.write(buffer, 1024);
    }
    if (remaining > 0) {
        file.write(buffer, remaining);
    }

    // Write MBR at the beginning
    file.seekp(0);
    MBR mbr;
    mbr.mbr_tamano = sizeBytes;
    mbr.mbr_fecha_creacion = time(nullptr);
    mbr.mbr_dsk_signature = rand() % 100000;
    mbr.dsk_fit = fit;
    // Initialize partitions
    for (int i = 0; i < 4; i++) {
        mbr.mbr_partitions[i] = Partition();
    }
    file.write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    file.close();

    return "MKDISK: Disco creado exitosamente en " + path + " (" + std::to_string(sizeBytes) + " bytes).";
}
