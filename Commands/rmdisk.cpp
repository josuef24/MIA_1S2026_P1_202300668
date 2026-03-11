#include "parser.h"
#include <cstdio>
#include <fstream>

std::string cmd_rmdisk(const ParsedCommand& cmd) {
    if (cmd.params.find("path") == cmd.params.end()) {
        return "ERROR rmdisk: Parametro -path es obligatorio.";
    }
    std::string path = cmd.params.at("path");

    // Check if file exists
    std::ifstream file(path);
    if (!file.good()) {
        return "ERROR rmdisk: El disco no existe en la ruta: " + path;
    }
    file.close();

    // Delete the file
    if (std::remove(path.c_str()) != 0) {
        return "ERROR rmdisk: No se pudo eliminar el disco.";
    }

    return "RMDISK: Disco eliminado exitosamente: " + path;
}
