#ifndef GLOBALS_H
#define GLOBALS_H

#include <string>
#include <vector>
#include <map>
#include <iostream>

// Represents a mounted partition in RAM
struct MountedPartition {
    std::string id;        // e.g., "341A"
    std::string path;      // Disk file path
    std::string name;      // Partition name
    int part_start;        // Byte where partition starts in disk
    int part_size;         // Partition size
};

// Represents current logged-in session
struct Session {
    bool active;
    std::string user;
    std::string grp;
    int uid;
    int gid;
    std::string id_partition;  // Mounted partition ID
    std::string disk_path;
    int part_start;
    int part_size;

    Session() : active(false), uid(-1), gid(-1), part_start(-1), part_size(0) {}
};

// Global state
namespace Globals {
    // Mounted partitions: key = id (e.g., "341A")
    inline std::vector<MountedPartition> mounted_partitions;

    // Current session
    inline Session current_session;

    // Carnet last 2 digits (configurable)
    inline std::string carnet_digits = "68";

    // Disk letter mapping: disk_path -> letter (A, B, C, ...)
    inline std::map<std::string, char> disk_letters;

    // Next letter to assign
    inline char next_letter = 'A';

    // Partition counter per disk: disk_path -> next partition number
    inline std::map<std::string, int> disk_partition_count;
}

#endif // GLOBALS_H
