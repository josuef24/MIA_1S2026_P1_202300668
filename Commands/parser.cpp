#include "parser.h"
#include <algorithm>
#include <sstream>
#include <iostream>

// Forward declarations - all command handlers
std::string cmd_mkdisk(const ParsedCommand& cmd);
std::string cmd_rmdisk(const ParsedCommand& cmd);
std::string cmd_fdisk(const ParsedCommand& cmd);
std::string cmd_mount(const ParsedCommand& cmd);
std::string cmd_mounted(const ParsedCommand& cmd);
std::string cmd_mkfs(const ParsedCommand& cmd);
std::string cmd_login(const ParsedCommand& cmd);
std::string cmd_logout(const ParsedCommand& cmd);
std::string cmd_mkgrp(const ParsedCommand& cmd);
std::string cmd_rmgrp(const ParsedCommand& cmd);
std::string cmd_mkusr(const ParsedCommand& cmd);
std::string cmd_rmusr(const ParsedCommand& cmd);
std::string cmd_chgrp(const ParsedCommand& cmd);
std::string cmd_cat(const ParsedCommand& cmd);
std::string cmd_mkfile(const ParsedCommand& cmd);
std::string cmd_mkdir(const ParsedCommand& cmd);
std::string cmd_rep(const ParsedCommand& cmd);

static std::string toLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

ParsedCommand parseCommand(const std::string& line) {
    ParsedCommand cmd;
    std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#') {
        cmd.command = "";
        return cmd;
    }

    // Find the command name (first token before any -)
    size_t i = 0;
    // Skip leading whitespace
    while (i < trimmed.size() && (trimmed[i] == ' ' || trimmed[i] == '\t')) i++;

    // Extract command name
    size_t cmd_start = i;
    while (i < trimmed.size() && trimmed[i] != ' ' && trimmed[i] != '\t' && trimmed[i] != '-') i++;
    cmd.command = toLower(trimmed.substr(cmd_start, i - cmd_start));

    // Parse parameters
    while (i < trimmed.size()) {
        // Skip whitespace
        while (i < trimmed.size() && (trimmed[i] == ' ' || trimmed[i] == '\t')) i++;
        if (i >= trimmed.size()) break;

        // Check for comment
        if (trimmed[i] == '#') break;

        // Expect '-'
        if (trimmed[i] == '-') {
            i++; // skip '-'
            // Extract parameter name
            size_t key_start = i;
            while (i < trimmed.size() && trimmed[i] != '=' && trimmed[i] != ' ' && trimmed[i] != '\t') i++;
            std::string key = toLower(trimmed.substr(key_start, i - key_start));

            if (i < trimmed.size() && trimmed[i] == '=') {
                i++; // skip '='
                std::string value;
                if (i < trimmed.size() && trimmed[i] == '"') {
                    // Quoted value
                    i++; // skip opening quote
                    size_t val_start = i;
                    while (i < trimmed.size() && trimmed[i] != '"') i++;
                    value = trimmed.substr(val_start, i - val_start);
                    if (i < trimmed.size()) i++; // skip closing quote
                } else {
                    // Unquoted value
                    size_t val_start = i;
                    // Handle negative numbers: if value starts with '-' followed by digit
                    if (i < trimmed.size() && trimmed[i] == '-' && i + 1 < trimmed.size() && isdigit(trimmed[i+1])) {
                        i++; // skip the '-'
                        while (i < trimmed.size() && isdigit(trimmed[i])) i++;
                    } else {
                        while (i < trimmed.size() && trimmed[i] != ' ' && trimmed[i] != '\t') {
                            if (trimmed[i] == '-' && i > val_start) break;
                            i++;
                        }
                    }
                    value = trimmed.substr(val_start, i - val_start);
                    // Remove trailing quote if present from malformed input
                    while (!value.empty() && value.back() == '"') value.pop_back();
                }
                cmd.params[key] = value;
            } else {
                // Flag without value (e.g., -r, -p)
                cmd.flags.push_back(key);
            }
        } else {
            // Skip unexpected character
            i++;
        }
    }

    return cmd;
}

std::string processLine(const std::string& line) {
    std::string trimmed = trim(line);

    // Empty line
    if (trimmed.empty()) return "";

    // Comment
    if (trimmed[0] == '#') {
        return trimmed; // Return comment as-is
    }

    ParsedCommand cmd = parseCommand(trimmed);
    if (cmd.command.empty()) return "";

    std::string result;

    if (cmd.command == "mkdisk") {
        result = cmd_mkdisk(cmd);
    } else if (cmd.command == "rmdisk") {
        result = cmd_rmdisk(cmd);
    } else if (cmd.command == "fdisk") {
        result = cmd_fdisk(cmd);
    } else if (cmd.command == "mount") {
        result = cmd_mount(cmd);
    } else if (cmd.command == "mounted") {
        result = cmd_mounted(cmd);
    } else if (cmd.command == "mkfs") {
        result = cmd_mkfs(cmd);
    } else if (cmd.command == "login") {
        result = cmd_login(cmd);
    } else if (cmd.command == "logout") {
        result = cmd_logout(cmd);
    } else if (cmd.command == "mkgrp") {
        result = cmd_mkgrp(cmd);
    } else if (cmd.command == "rmgrp") {
        result = cmd_rmgrp(cmd);
    } else if (cmd.command == "mkusr") {
        result = cmd_mkusr(cmd);
    } else if (cmd.command == "rmusr") {
        result = cmd_rmusr(cmd);
    } else if (cmd.command == "chgrp") {
        result = cmd_chgrp(cmd);
    } else if (cmd.command == "cat") {
        result = cmd_cat(cmd);
    } else if (cmd.command == "mkfile") {
        result = cmd_mkfile(cmd);
    } else if (cmd.command == "mkdir") {
        result = cmd_mkdir(cmd);
    } else if (cmd.command == "rep") {
        result = cmd_rep(cmd);
    } else {
        result = "ERROR: Comando no reconocido: " + cmd.command;
    }

    return result;
}

void processScript(const std::string& script) {
    std::istringstream stream(script);
    std::string line;
    while (std::getline(stream, line)) {
        std::string result = processLine(line);
        if (!result.empty()) {
            std::cout << result << std::endl;
        }
    }
}
