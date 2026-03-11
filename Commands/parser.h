#ifndef PARSER_H
#define PARSER_H

#include <string>
#include <map>
#include <vector>

struct ParsedCommand {
    std::string command;  // lowercase command name
    std::map<std::string, std::string> params;  // key=value pairs (keys lowercase)
    std::vector<std::string> flags;  // flags without values (like -r, -p)
};

// Parse a single command line into command + params
ParsedCommand parseCommand(const std::string& line);

// Process a full script (multiple lines)
void processScript(const std::string& script);

// Process a single line
std::string processLine(const std::string& line);

#endif
