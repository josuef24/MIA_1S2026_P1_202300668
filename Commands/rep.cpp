#include "parser.h"
#include "../Structures/structs.h"
#include "../Utils/globals.h"
#include <fstream>
#include <cstring>
#include <sstream>
#include <vector>
#include <ctime>
#include <sys/stat.h>
#include <algorithm>

static MountedPartition* findMounted(const std::string& id) {
    for (auto& mp : Globals::mounted_partitions) {
        if (mp.id == id) return &mp;
    }
    return nullptr;
}

static std::string toLowerStr(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::tolower);
    return r;
}

static void createParentDirs(const std::string& path) {
    size_t pos = 0;
    while ((pos = path.find('/', pos + 1)) != std::string::npos) {
        std::string dir = path.substr(0, pos);
        mkdir(dir.c_str(), 0777);
    }
}

static std::string getExtension(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    return toLowerStr(path.substr(dot + 1));
}

static std::string formatTime(time_t t) {
    if (t == 0) return "N/A";
    char buf[64];
    struct tm* tm = localtime(&t);
    strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M", tm);
    return std::string(buf);
}

// ==================== REPORT: MBR ====================
static std::string reportMBR(const std::string& diskPath, const std::string& outputPath) {
    std::fstream disk(diskPath, std::ios::in | std::ios::binary);
    if (!disk.is_open()) return "ERROR rep: No se pudo abrir el disco.";

    MBR mbr;
    disk.seekg(0);
    disk.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

    std::ostringstream dot;
    dot << "digraph MBR {\n";
    dot << "  node [shape=plaintext]\n";
    dot << "  mbr [label=<\n";
    dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
    dot << "    <TR><TD COLSPAN=\"2\" BGCOLOR=\"#4472C4\"><FONT COLOR=\"white\"><B>MBR</B></FONT></TD></TR>\n";
    dot << "    <TR><TD>mbr_tamano</TD><TD>" << mbr.mbr_tamano << "</TD></TR>\n";
    dot << "    <TR><TD>mbr_fecha_creacion</TD><TD>" << formatTime(mbr.mbr_fecha_creacion) << "</TD></TR>\n";
    dot << "    <TR><TD>mbr_dsk_signature</TD><TD>" << mbr.mbr_dsk_signature << "</TD></TR>\n";
    dot << "    <TR><TD>dsk_fit</TD><TD>" << mbr.dsk_fit << "</TD></TR>\n";

    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_s > 0) {
            std::string pname(mbr.mbr_partitions[i].part_name, strnlen(mbr.mbr_partitions[i].part_name, 16));
            dot << "    <TR><TD COLSPAN=\"2\" BGCOLOR=\"#70AD47\"><FONT COLOR=\"white\"><B>Partition " << (i+1) << "</B></FONT></TD></TR>\n";
            dot << "    <TR><TD>part_status</TD><TD>" << mbr.mbr_partitions[i].part_status << "</TD></TR>\n";
            dot << "    <TR><TD>part_type</TD><TD>" << mbr.mbr_partitions[i].part_type << "</TD></TR>\n";
            dot << "    <TR><TD>part_fit</TD><TD>" << mbr.mbr_partitions[i].part_fit << "</TD></TR>\n";
            dot << "    <TR><TD>part_start</TD><TD>" << mbr.mbr_partitions[i].part_start << "</TD></TR>\n";
            dot << "    <TR><TD>part_s</TD><TD>" << mbr.mbr_partitions[i].part_s << "</TD></TR>\n";
            dot << "    <TR><TD>part_name</TD><TD>" << pname << "</TD></TR>\n";
            dot << "    <TR><TD>part_correlative</TD><TD>" << mbr.mbr_partitions[i].part_correlative << "</TD></TR>\n";
            std::string pid(mbr.mbr_partitions[i].part_id, strnlen(mbr.mbr_partitions[i].part_id, 4));
            dot << "    <TR><TD>part_id</TD><TD>" << pid << "</TD></TR>\n";

            // If extended, show EBRs
            if (mbr.mbr_partitions[i].part_type == 'E') {
                int ebrPos = mbr.mbr_partitions[i].part_start;
                int ebrNum = 1;
                while (ebrPos != -1) {
                    EBR ebr;
                    disk.seekg(ebrPos);
                    disk.read(reinterpret_cast<char*>(&ebr), sizeof(EBR));
                    if (ebr.part_s > 0) {
                        std::string ename(ebr.part_name, strnlen(ebr.part_name, 16));
                        dot << "    <TR><TD COLSPAN=\"2\" BGCOLOR=\"#FFC000\"><B>EBR " << ebrNum << "</B></TD></TR>\n";
                        dot << "    <TR><TD>part_mount</TD><TD>" << ebr.part_mount << "</TD></TR>\n";
                        dot << "    <TR><TD>part_fit</TD><TD>" << ebr.part_fit << "</TD></TR>\n";
                        dot << "    <TR><TD>part_start</TD><TD>" << ebr.part_start << "</TD></TR>\n";
                        dot << "    <TR><TD>part_s</TD><TD>" << ebr.part_s << "</TD></TR>\n";
                        dot << "    <TR><TD>part_next</TD><TD>" << ebr.part_next << "</TD></TR>\n";
                        dot << "    <TR><TD>part_name</TD><TD>" << ename << "</TD></TR>\n";
                        ebrNum++;
                    }
                    ebrPos = ebr.part_next;
                }
            }
        }
    }

    dot << "    </TABLE>\n";
    dot << "  >]\n";
    dot << "}\n";

    disk.close();

    // Write DOT file and generate image
    createParentDirs(outputPath);
    std::string dotPath = outputPath + ".dot";
    std::ofstream dotFile(dotPath);
    dotFile << dot.str();
    dotFile.close();

    std::string ext = getExtension(outputPath);
    if (ext == "txt") {
        // just copy dot source
        std::ofstream out(outputPath);
        out << dot.str();
        out.close();
    } else {
        if (ext.empty()) ext = "png";
        std::string cmd = "dot -T" + ext + " " + dotPath + " -o " + outputPath + " 2>/dev/null";
        system(cmd.c_str());
    }

    return "REP: Reporte MBR generado en " + outputPath;
}

// ==================== REPORT: DISK ====================
static std::string reportDisk(const std::string& diskPath, const std::string& outputPath) {
    std::fstream disk(diskPath, std::ios::in | std::ios::binary);
    if (!disk.is_open()) return "ERROR rep: No se pudo abrir el disco.";

    MBR mbr;
    disk.seekg(0);
    disk.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

    struct Region { std::string name; int start; int size; };
    std::vector<Region> regions;
    regions.push_back({"MBR", 0, (int)sizeof(MBR)});

    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_s <= 0) continue;
        std::string pname(mbr.mbr_partitions[i].part_name, strnlen(mbr.mbr_partitions[i].part_name, 16));
        std::string typeStr = (mbr.mbr_partitions[i].part_type == 'E') ? "Extendida" : "Primaria";
        regions.push_back({typeStr + "\\n" + pname, mbr.mbr_partitions[i].part_start, mbr.mbr_partitions[i].part_s});
    }

    std::sort(regions.begin(), regions.end(), [](const Region& a, const Region& b) {
        return a.start < b.start;
    });

    // Find free spaces
    std::vector<Region> allRegions;
    int cursor = 0;
    for (auto& r : regions) {
        if (r.start > cursor) {
            allRegions.push_back({"Libre", cursor, r.start - cursor});
        }
        allRegions.push_back(r);
        cursor = r.start + r.size;
    }
    if (cursor < mbr.mbr_tamano) {
        allRegions.push_back({"Libre", cursor, mbr.mbr_tamano - cursor});
    }

    std::ostringstream dot;
    dot << "digraph DISK {\n";
    dot << "  node [shape=plaintext]\n";
    dot << "  disk [label=<\n";
    dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
    dot << "    <TR>\n";
    for (auto& r : allRegions) {
        double pct = (double)r.size / mbr.mbr_tamano * 100.0;
        std::string color = "#DDDDDD";
        if (r.name == "MBR") color = "#4472C4";
        else if (r.name.find("Primaria") != std::string::npos) color = "#70AD47";
        else if (r.name.find("Extendida") != std::string::npos) color = "#FFC000";
        else if (r.name == "Libre") color = "#F2F2F2";

        char pctBuf[16];
        snprintf(pctBuf, sizeof(pctBuf), "%.2f%%", pct);
        dot << "    <TD BGCOLOR=\"" << color << "\">" << r.name << "\\n" << pctBuf << "</TD>\n";
    }
    dot << "    </TR>\n";
    dot << "    </TABLE>\n";
    dot << "  >]\n";
    dot << "}\n";

    disk.close();

    createParentDirs(outputPath);
    std::string dotPath = outputPath + ".dot";
    std::ofstream dotFile(dotPath);
    dotFile << dot.str();
    dotFile.close();

    std::string ext = getExtension(outputPath);
    if (ext.empty()) ext = "png";
    std::string cmd = "dot -T" + ext + " " + dotPath + " -o " + outputPath + " 2>/dev/null";
    system(cmd.c_str());

    return "REP: Reporte DISK generado en " + outputPath;
}

// ==================== HELPER: Read SuperBlock ====================
static bool readSB(const std::string& diskPath, int partStart, SuperBlock& sb) {
    std::fstream disk(diskPath, std::ios::in | std::ios::binary);
    if (!disk.is_open()) return false;
    disk.seekg(partStart);
    disk.read(reinterpret_cast<char*>(&sb), sizeof(SuperBlock));
    disk.close();
    return sb.s_magic == 0xEF53;
}

// ==================== REPORT: Inode ====================
static std::string reportInode(const std::string& diskPath, int partStart, const std::string& outputPath) {
    std::fstream disk(diskPath, std::ios::in | std::ios::binary);
    if (!disk.is_open()) return "ERROR rep: No se pudo abrir el disco.";

    SuperBlock sb;
    disk.seekg(partStart);
    disk.read(reinterpret_cast<char*>(&sb), sizeof(SuperBlock));

    std::ostringstream dot;
    dot << "digraph Inodes {\n";
    dot << "  node [shape=plaintext]\n";
    dot << "  rankdir=LR\n";

    int count = 0;
    for (int i = 0; i < sb.s_inodes_count; i++) {
        char bit;
        disk.seekg(sb.s_bm_inode_start + i);
        disk.read(&bit, 1);
        if (bit != '1') continue;

        Inode inode;
        disk.seekg(sb.s_inode_start + i * sizeof(Inode));
        disk.read(reinterpret_cast<char*>(&inode), sizeof(Inode));

        std::string perm(inode.i_perm, 3);
        dot << "  inode" << i << " [label=<\n";
        dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
        dot << "    <TR><TD COLSPAN=\"2\" BGCOLOR=\"#4472C4\"><FONT COLOR=\"white\"><B>Inode " << i << "</B></FONT></TD></TR>\n";
        dot << "    <TR><TD>i_uid</TD><TD>" << inode.i_uid << "</TD></TR>\n";
        dot << "    <TR><TD>i_gid</TD><TD>" << inode.i_gid << "</TD></TR>\n";
        dot << "    <TR><TD>i_s</TD><TD>" << inode.i_s << "</TD></TR>\n";
        dot << "    <TR><TD>i_atime</TD><TD>" << formatTime(inode.i_atime) << "</TD></TR>\n";
        dot << "    <TR><TD>i_ctime</TD><TD>" << formatTime(inode.i_ctime) << "</TD></TR>\n";
        dot << "    <TR><TD>i_mtime</TD><TD>" << formatTime(inode.i_mtime) << "</TD></TR>\n";
        dot << "    <TR><TD>i_type</TD><TD>" << inode.i_type << "</TD></TR>\n";
        dot << "    <TR><TD>i_perm</TD><TD>" << perm << "</TD></TR>\n";
        for (int b = 0; b < 15; b++) {
            dot << "    <TR><TD>i_block[" << b << "]</TD><TD>" << inode.i_block[b] << "</TD></TR>\n";
        }
        dot << "    </TABLE>\n";
        dot << "  >]\n";
        count++;
    }

    // Link inodes
    for (int i = 0; i < count - 1; i++) {
        // Find ith and (i+1)th used inodes
    }

    dot << "}\n";
    disk.close();

    createParentDirs(outputPath);
    std::string dotPath = outputPath + ".dot";
    std::ofstream dotFile(dotPath);
    dotFile << dot.str();
    dotFile.close();

    std::string ext = getExtension(outputPath);
    if (ext.empty()) ext = "png";
    std::string cmd = "dot -T" + ext + " " + dotPath + " -o " + outputPath + " 2>/dev/null";
    system(cmd.c_str());

    return "REP: Reporte Inode generado en " + outputPath;
}

// ==================== REPORT: Block ====================
static std::string reportBlock(const std::string& diskPath, int partStart, const std::string& outputPath) {
    std::fstream disk(diskPath, std::ios::in | std::ios::binary);
    if (!disk.is_open()) return "ERROR rep: No se pudo abrir el disco.";

    SuperBlock sb;
    disk.seekg(partStart);
    disk.read(reinterpret_cast<char*>(&sb), sizeof(SuperBlock));

    std::ostringstream dot;
    dot << "digraph Blocks {\n";
    dot << "  node [shape=plaintext]\n";
    dot << "  rankdir=LR\n";

    for (int i = 0; i < sb.s_blocks_count; i++) {
        char bit;
        disk.seekg(sb.s_bm_block_start + i);
        disk.read(&bit, 1);
        if (bit != '1') continue;

        // Determine block type by checking which inode references it
        // For simplicity, try reading as folder block first, then file block
        bool isFolderBlock = false;

        // Check all inodes to find what references this block
        for (int n = 0; n < sb.s_inodes_count; n++) {
            char ibit;
            disk.seekg(sb.s_bm_inode_start + n);
            disk.read(&ibit, 1);
            if (ibit != '1') continue;

            Inode inode;
            disk.seekg(sb.s_inode_start + n * sizeof(Inode));
            disk.read(reinterpret_cast<char*>(&inode), sizeof(Inode));

            for (int b = 0; b < 12; b++) {
                if (inode.i_block[b] == i) {
                    if (inode.i_type == '0') isFolderBlock = true;
                    break;
                }
            }
        }

        if (isFolderBlock) {
            FolderBlock fb;
            disk.seekg(sb.s_block_start + i * sizeof(FileBlock));
            disk.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

            dot << "  block" << i << " [label=<\n";
            dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
            dot << "    <TR><TD COLSPAN=\"2\" BGCOLOR=\"#70AD47\"><FONT COLOR=\"white\"><B>Folder Block " << i << "</B></FONT></TD></TR>\n";
            for (int j = 0; j < 4; j++) {
                std::string bname(fb.b_content[j].b_name, strnlen(fb.b_content[j].b_name, 12));
                dot << "    <TR><TD>" << bname << "</TD><TD>" << fb.b_content[j].b_inodo << "</TD></TR>\n";
            }
            dot << "    </TABLE>\n";
            dot << "  >]\n";
        } else {
            FileBlock fb;
            disk.seekg(sb.s_block_start + i * sizeof(FileBlock));
            disk.read(reinterpret_cast<char*>(&fb), sizeof(FileBlock));

            std::string content(fb.b_content, strnlen(fb.b_content, 64));
            // Escape special chars
            std::string escaped;
            for (char c : content) {
                if (c == '<') escaped += "&lt;";
                else if (c == '>') escaped += "&gt;";
                else if (c == '&') escaped += "&amp;";
                else if (c == '"') escaped += "&quot;";
                else if (c == '\n') escaped += "\\n";
                else escaped += c;
            }

            dot << "  block" << i << " [label=<\n";
            dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
            dot << "    <TR><TD BGCOLOR=\"#ED7D31\"><FONT COLOR=\"white\"><B>File Block " << i << "</B></FONT></TD></TR>\n";
            dot << "    <TR><TD>" << escaped << "</TD></TR>\n";
            dot << "    </TABLE>\n";
            dot << "  >]\n";
        }
    }

    dot << "}\n";
    disk.close();

    createParentDirs(outputPath);
    std::string dotPath = outputPath + ".dot";
    std::ofstream dotFile(dotPath);
    dotFile << dot.str();
    dotFile.close();

    std::string ext = getExtension(outputPath);
    if (ext.empty()) ext = "png";
    std::string cmd = "dot -T" + ext + " " + dotPath + " -o " + outputPath + " 2>/dev/null";
    system(cmd.c_str());

    return "REP: Reporte Block generado en " + outputPath;
}

// ==================== REPORT: Bitmap Inode ====================
static std::string reportBmInode(const std::string& diskPath, int partStart, const std::string& outputPath) {
    std::fstream disk(diskPath, std::ios::in | std::ios::binary);
    if (!disk.is_open()) return "ERROR rep: No se pudo abrir el disco.";

    SuperBlock sb;
    disk.seekg(partStart);
    disk.read(reinterpret_cast<char*>(&sb), sizeof(SuperBlock));

    createParentDirs(outputPath);
    std::ofstream out(outputPath);
    for (int i = 0; i < sb.s_inodes_count; i++) {
        char bit;
        disk.seekg(sb.s_bm_inode_start + i);
        disk.read(&bit, 1);
        out << (bit == '1' ? '1' : '0');
        if ((i + 1) % 20 == 0) out << "\n";
        else out << " ";
    }
    out << "\n";
    out.close();
    disk.close();

    return "REP: Reporte bm_inode generado en " + outputPath;
}

// ==================== REPORT: Bitmap Block ====================
static std::string reportBmBlock(const std::string& diskPath, int partStart, const std::string& outputPath) {
    std::fstream disk(diskPath, std::ios::in | std::ios::binary);
    if (!disk.is_open()) return "ERROR rep: No se pudo abrir el disco.";

    SuperBlock sb;
    disk.seekg(partStart);
    disk.read(reinterpret_cast<char*>(&sb), sizeof(SuperBlock));

    createParentDirs(outputPath);
    std::ofstream out(outputPath);
    for (int i = 0; i < sb.s_blocks_count; i++) {
        char bit;
        disk.seekg(sb.s_bm_block_start + i);
        disk.read(&bit, 1);
        out << (bit == '1' ? '1' : '0');
        if ((i + 1) % 20 == 0) out << "\n";
        else out << " ";
    }
    out << "\n";
    out.close();
    disk.close();

    return "REP: Reporte bm_block generado en " + outputPath;
}

// ==================== REPORT: SuperBlock ====================
static std::string reportSb(const std::string& diskPath, int partStart, const std::string& outputPath) {
    std::fstream disk(diskPath, std::ios::in | std::ios::binary);
    if (!disk.is_open()) return "ERROR rep: No se pudo abrir el disco.";

    SuperBlock sb;
    disk.seekg(partStart);
    disk.read(reinterpret_cast<char*>(&sb), sizeof(SuperBlock));
    disk.close();

    std::ostringstream dot;
    dot << "digraph SB {\n";
    dot << "  node [shape=plaintext]\n";
    dot << "  sb [label=<\n";
    dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
    dot << "    <TR><TD COLSPAN=\"2\" BGCOLOR=\"#4472C4\"><FONT COLOR=\"white\"><B>SuperBlock</B></FONT></TD></TR>\n";
    dot << "    <TR><TD>s_filesystem_type</TD><TD>" << sb.s_filesystem_type << "</TD></TR>\n";
    dot << "    <TR><TD>s_inodes_count</TD><TD>" << sb.s_inodes_count << "</TD></TR>\n";
    dot << "    <TR><TD>s_blocks_count</TD><TD>" << sb.s_blocks_count << "</TD></TR>\n";
    dot << "    <TR><TD>s_free_blocks_count</TD><TD>" << sb.s_free_blocks_count << "</TD></TR>\n";
    dot << "    <TR><TD>s_free_inodes_count</TD><TD>" << sb.s_free_inodes_count << "</TD></TR>\n";
    dot << "    <TR><TD>s_mtime</TD><TD>" << formatTime(sb.s_mtime) << "</TD></TR>\n";
    dot << "    <TR><TD>s_umtime</TD><TD>" << formatTime(sb.s_umtime) << "</TD></TR>\n";
    dot << "    <TR><TD>s_mnt_count</TD><TD>" << sb.s_mnt_count << "</TD></TR>\n";
    char magicBuf[16];
    snprintf(magicBuf, sizeof(magicBuf), "0x%X", sb.s_magic);
    dot << "    <TR><TD>s_magic</TD><TD>" << magicBuf << "</TD></TR>\n";
    dot << "    <TR><TD>s_inode_s</TD><TD>" << sb.s_inode_s << "</TD></TR>\n";
    dot << "    <TR><TD>s_block_s</TD><TD>" << sb.s_block_s << "</TD></TR>\n";
    dot << "    <TR><TD>s_firts_ino</TD><TD>" << sb.s_firts_ino << "</TD></TR>\n";
    dot << "    <TR><TD>s_first_blo</TD><TD>" << sb.s_first_blo << "</TD></TR>\n";
    dot << "    <TR><TD>s_bm_inode_start</TD><TD>" << sb.s_bm_inode_start << "</TD></TR>\n";
    dot << "    <TR><TD>s_bm_block_start</TD><TD>" << sb.s_bm_block_start << "</TD></TR>\n";
    dot << "    <TR><TD>s_inode_start</TD><TD>" << sb.s_inode_start << "</TD></TR>\n";
    dot << "    <TR><TD>s_block_start</TD><TD>" << sb.s_block_start << "</TD></TR>\n";
    dot << "    </TABLE>\n";
    dot << "  >]\n";
    dot << "}\n";

    createParentDirs(outputPath);
    std::string dotPath = outputPath + ".dot";
    std::ofstream dotFile(dotPath);
    dotFile << dot.str();
    dotFile.close();

    std::string ext = getExtension(outputPath);
    if (ext.empty()) ext = "png";
    std::string cmd = "dot -T" + ext + " " + dotPath + " -o " + outputPath + " 2>/dev/null";
    system(cmd.c_str());

    return "REP: Reporte SuperBlock generado en " + outputPath;
}

// ==================== REPORT: Tree ====================
static void treeTraverseInode(std::fstream& disk, const SuperBlock& sb, int inodeIdx,
                              std::ostringstream& dot, std::ostringstream& edges) {
    Inode inode;
    disk.seekg(sb.s_inode_start + inodeIdx * sizeof(Inode));
    disk.read(reinterpret_cast<char*>(&inode), sizeof(Inode));

    std::string perm(inode.i_perm, 3);
    dot << "  inode" << inodeIdx << " [label=<\n";
    dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
    dot << "    <TR><TD COLSPAN=\"2\" BGCOLOR=\"#4472C4\"><FONT COLOR=\"white\"><B>Inode " << inodeIdx << "</B></FONT></TD></TR>\n";
    dot << "    <TR><TD>i_uid</TD><TD>" << inode.i_uid << "</TD></TR>\n";
    dot << "    <TR><TD>i_gid</TD><TD>" << inode.i_gid << "</TD></TR>\n";
    dot << "    <TR><TD>i_s</TD><TD>" << inode.i_s << "</TD></TR>\n";
    dot << "    <TR><TD>i_type</TD><TD>" << inode.i_type << "</TD></TR>\n";
    dot << "    <TR><TD>i_perm</TD><TD>" << perm << "</TD></TR>\n";
    for (int b = 0; b < 15; b++) {
        dot << "    <TR><TD>i_block[" << b << "]</TD><TD>" << inode.i_block[b] << "</TD></TR>\n";
    }
    dot << "    </TABLE>\n";
    dot << "  >]\n";

    if (inode.i_type == '0') { // Folder
        for (int b = 0; b < 12; b++) {
            if (inode.i_block[b] == -1) continue;
            int blockIdx = inode.i_block[b];
            edges << "  inode" << inodeIdx << " -> block" << blockIdx << "\n";

            FolderBlock fb;
            disk.seekg(sb.s_block_start + blockIdx * sizeof(FileBlock));
            disk.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

            dot << "  block" << blockIdx << " [label=<\n";
            dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
            dot << "    <TR><TD COLSPAN=\"2\" BGCOLOR=\"#70AD47\"><FONT COLOR=\"white\"><B>Folder Block " << blockIdx << "</B></FONT></TD></TR>\n";
            for (int j = 0; j < 4; j++) {
                std::string bname(fb.b_content[j].b_name, strnlen(fb.b_content[j].b_name, 12));
                dot << "    <TR><TD>" << bname << "</TD><TD>" << fb.b_content[j].b_inodo << "</TD></TR>\n";
            }
            dot << "    </TABLE>\n";
            dot << "  >]\n";

            for (int j = 0; j < 4; j++) {
                if (fb.b_content[j].b_inodo == -1) continue;
                std::string bname(fb.b_content[j].b_name, strnlen(fb.b_content[j].b_name, 12));
                if (bname == "." || bname == "..") continue;
                edges << "  block" << blockIdx << " -> inode" << fb.b_content[j].b_inodo << "\n";
                treeTraverseInode(disk, sb, fb.b_content[j].b_inodo, dot, edges);
            }
        }
        // Single indirect
        if (inode.i_block[12] != -1) {
            int ptrBlockIdx = inode.i_block[12];
            edges << "  inode" << inodeIdx << " -> ptrblock" << ptrBlockIdx << "\n";
            PointerBlock pb;
            disk.seekg(sb.s_block_start + ptrBlockIdx * sizeof(FileBlock));
            disk.read(reinterpret_cast<char*>(&pb), sizeof(PointerBlock));

            dot << "  ptrblock" << ptrBlockIdx << " [label=<\n";
            dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
            dot << "    <TR><TD BGCOLOR=\"#9B59B6\"><FONT COLOR=\"white\"><B>Ptr Block " << ptrBlockIdx << "</B></FONT></TD></TR>\n";
            for (int p = 0; p < 16; p++) {
                dot << "    <TR><TD>" << pb.b_pointers[p] << "</TD></TR>\n";
            }
            dot << "    </TABLE>>]\n";

            for (int p = 0; p < 16; p++) {
                if (pb.b_pointers[p] == -1) continue;
                int bIdx = pb.b_pointers[p];
                edges << "  ptrblock" << ptrBlockIdx << " -> block" << bIdx << "\n";

                FolderBlock fb;
                disk.seekg(sb.s_block_start + bIdx * sizeof(FileBlock));
                disk.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

                dot << "  block" << bIdx << " [label=<\n";
                dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
                dot << "    <TR><TD COLSPAN=\"2\" BGCOLOR=\"#70AD47\"><FONT COLOR=\"white\"><B>Folder Block " << bIdx << "</B></FONT></TD></TR>\n";
                for (int j = 0; j < 4; j++) {
                    std::string bname(fb.b_content[j].b_name, strnlen(fb.b_content[j].b_name, 12));
                    dot << "    <TR><TD>" << bname << "</TD><TD>" << fb.b_content[j].b_inodo << "</TD></TR>\n";
                }
                dot << "    </TABLE>>]\n";

                for (int j = 0; j < 4; j++) {
                    if (fb.b_content[j].b_inodo == -1) continue;
                    std::string bname(fb.b_content[j].b_name, strnlen(fb.b_content[j].b_name, 12));
                    if (bname == "." || bname == "..") continue;
                    edges << "  block" << bIdx << " -> inode" << fb.b_content[j].b_inodo << "\n";
                    treeTraverseInode(disk, sb, fb.b_content[j].b_inodo, dot, edges);
                }
            }
        }
    } else { // File
        for (int b = 0; b < 12; b++) {
            if (inode.i_block[b] == -1) continue;
            int blockIdx = inode.i_block[b];
            edges << "  inode" << inodeIdx << " -> fileblock" << blockIdx << "\n";

            FileBlock fb;
            disk.seekg(sb.s_block_start + blockIdx * sizeof(FileBlock));
            disk.read(reinterpret_cast<char*>(&fb), sizeof(FileBlock));

            int toRead = std::min(64, inode.i_s - b * 64);
            if (toRead < 0) toRead = 0;
            std::string content(fb.b_content, toRead);
            std::string escaped;
            for (char c : content) {
                if (c == '<') escaped += "&lt;";
                else if (c == '>') escaped += "&gt;";
                else if (c == '&') escaped += "&amp;";
                else if (c == '"') escaped += "&quot;";
                else if (c == '\n') escaped += "\\n";
                else if (c == '\0') break;
                else escaped += c;
            }

            dot << "  fileblock" << blockIdx << " [label=<\n";
            dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
            dot << "    <TR><TD BGCOLOR=\"#ED7D31\"><FONT COLOR=\"white\"><B>File Block " << blockIdx << "</B></FONT></TD></TR>\n";
            dot << "    <TR><TD>" << escaped << "</TD></TR>\n";
            dot << "    </TABLE>>]\n";
        }
        // Single indirect for files
        if (inode.i_block[12] != -1) {
            int ptrBlockIdx = inode.i_block[12];
            edges << "  inode" << inodeIdx << " -> fptrblock" << ptrBlockIdx << "\n";
            PointerBlock pb;
            disk.seekg(sb.s_block_start + ptrBlockIdx * sizeof(FileBlock));
            disk.read(reinterpret_cast<char*>(&pb), sizeof(PointerBlock));

            dot << "  fptrblock" << ptrBlockIdx << " [label=<\n";
            dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
            dot << "    <TR><TD BGCOLOR=\"#9B59B6\"><FONT COLOR=\"white\"><B>Ptr Block " << ptrBlockIdx << "</B></FONT></TD></TR>\n";
            for (int p = 0; p < 16; p++) {
                dot << "    <TR><TD>" << pb.b_pointers[p] << "</TD></TR>\n";
            }
            dot << "    </TABLE>>]\n";

            int offset = 12 * 64;
            for (int p = 0; p < 16; p++) {
                if (pb.b_pointers[p] == -1) continue;
                int bIdx = pb.b_pointers[p];
                edges << "  fptrblock" << ptrBlockIdx << " -> fileblock" << bIdx << "\n";

                FileBlock fb;
                disk.seekg(sb.s_block_start + bIdx * sizeof(FileBlock));
                disk.read(reinterpret_cast<char*>(&fb), sizeof(FileBlock));

                int toRead = std::min(64, inode.i_s - offset);
                if (toRead < 0) toRead = 0;
                std::string content(fb.b_content, toRead);
                std::string escaped;
                for (char c : content) {
                    if (c == '<') escaped += "&lt;";
                    else if (c == '>') escaped += "&gt;";
                    else if (c == '&') escaped += "&amp;";
                    else if (c == '"') escaped += "&quot;";
                    else if (c == '\n') escaped += "\\n";
                    else if (c == '\0') break;
                    else escaped += c;
                }

                dot << "  fileblock" << bIdx << " [label=<\n";
                dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
                dot << "    <TR><TD BGCOLOR=\"#ED7D31\"><FONT COLOR=\"white\"><B>File Block " << bIdx << "</B></FONT></TD></TR>\n";
                dot << "    <TR><TD>" << escaped << "</TD></TR>\n";
                dot << "    </TABLE>>]\n";

                offset += 64;
            }
        }
    }
}

static std::string reportTree(const std::string& diskPath, int partStart, const std::string& outputPath) {
    std::fstream disk(diskPath, std::ios::in | std::ios::out | std::ios::binary);
    if (!disk.is_open()) return "ERROR rep: No se pudo abrir el disco.";

    SuperBlock sb;
    disk.seekg(partStart);
    disk.read(reinterpret_cast<char*>(&sb), sizeof(SuperBlock));

    std::ostringstream dot, edges;
    dot << "digraph Tree {\n";
    dot << "  rankdir=LR\n";
    dot << "  node [shape=plaintext]\n";

    treeTraverseInode(disk, sb, 0, dot, edges);

    dot << edges.str();
    dot << "}\n";

    disk.close();

    createParentDirs(outputPath);
    std::string dotPath = outputPath + ".dot";
    std::ofstream dotFile(dotPath);
    dotFile << dot.str();
    dotFile.close();

    std::string ext = getExtension(outputPath);
    if (ext.empty()) ext = "png";
    std::string cmd = "dot -T" + ext + " " + dotPath + " -o " + outputPath + " 2>/dev/null";
    system(cmd.c_str());

    return "REP: Reporte Tree generado en " + outputPath;
}

// ==================== REPORT: File ====================
static std::string reportFile(const std::string& diskPath, int partStart, const std::string& outputPath, const std::string& filePath) {
    std::fstream disk(diskPath, std::ios::in | std::ios::binary);
    if (!disk.is_open()) return "ERROR rep: No se pudo abrir el disco.";

    SuperBlock sb;
    disk.seekg(partStart);
    disk.read(reinterpret_cast<char*>(&sb), sizeof(SuperBlock));

    // Find inode
    std::vector<std::string> parts;
    std::istringstream ss(filePath);
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (!part.empty()) parts.push_back(part);
    }

    int currentInode = 0;
    for (const auto& name : parts) {
        Inode inode;
        disk.seekg(sb.s_inode_start + currentInode * sizeof(Inode));
        disk.read(reinterpret_cast<char*>(&inode), sizeof(Inode));
        bool found = false;
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
        if (!found) {
            disk.close();
            return "ERROR rep: No se encontro el archivo: " + filePath;
        }
    }

    // Read file content
    Inode inode;
    disk.seekg(sb.s_inode_start + currentInode * sizeof(Inode));
    disk.read(reinterpret_cast<char*>(&inode), sizeof(Inode));

    std::string content;
    for (int b = 0; b < 12; b++) {
        if (inode.i_block[b] == -1) break;
        FileBlock fb;
        disk.seekg(sb.s_block_start + inode.i_block[b] * sizeof(FileBlock));
        disk.read(reinterpret_cast<char*>(&fb), sizeof(FileBlock));
        int toRead = std::min(64, inode.i_s - (int)content.size());
        if (toRead > 0) content.append(fb.b_content, toRead);
    }
    if (inode.i_block[12] != -1 && (int)content.size() < inode.i_s) {
        PointerBlock pb;
        disk.seekg(sb.s_block_start + inode.i_block[12] * sizeof(FileBlock));
        disk.read(reinterpret_cast<char*>(&pb), sizeof(PointerBlock));
        for (int p = 0; p < 16 && (int)content.size() < inode.i_s; p++) {
            if (pb.b_pointers[p] == -1) break;
            FileBlock fb;
            disk.seekg(sb.s_block_start + pb.b_pointers[p] * sizeof(FileBlock));
            disk.read(reinterpret_cast<char*>(&fb), sizeof(FileBlock));
            int toRead = std::min(64, inode.i_s - (int)content.size());
            if (toRead > 0) content.append(fb.b_content, toRead);
        }
    }

    disk.close();

    createParentDirs(outputPath);
    std::string ext = getExtension(outputPath);
    if (ext == "txt" || ext == "") {
        std::ofstream out(outputPath);
        out << filePath << ":\n" << content << "\n";
        out.close();
    } else {
        // Generate graphviz
        std::string escaped;
        for (char c : content) {
            if (c == '<') escaped += "&lt;";
            else if (c == '>') escaped += "&gt;";
            else if (c == '&') escaped += "&amp;";
            else if (c == '"') escaped += "&quot;";
            else if (c == '\n') escaped += "<BR/>";
            else escaped += c;
        }
        std::ostringstream dot;
        dot << "digraph File {\n";
        dot << "  node [shape=plaintext]\n";
        dot << "  file [label=<\n";
        dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
        dot << "    <TR><TD BGCOLOR=\"#4472C4\"><FONT COLOR=\"white\"><B>" << filePath << "</B></FONT></TD></TR>\n";
        dot << "    <TR><TD>" << escaped << "</TD></TR>\n";
        dot << "    </TABLE>>]\n";
        dot << "}\n";

        std::string dotPath = outputPath + ".dot";
        std::ofstream dotFile(dotPath);
        dotFile << dot.str();
        dotFile.close();

        std::string cmd = "dot -T" + ext + " " + dotPath + " -o " + outputPath + " 2>/dev/null";
        system(cmd.c_str());
    }

    return "REP: Reporte File generado en " + outputPath;
}

// ==================== REPORT: Ls ====================
static std::string reportLs(const std::string& diskPath, int partStart, const std::string& outputPath, const std::string& dirPath) {
    std::fstream disk(diskPath, std::ios::in | std::ios::binary);
    if (!disk.is_open()) return "ERROR rep: No se pudo abrir el disco.";

    SuperBlock sb;
    disk.seekg(partStart);
    disk.read(reinterpret_cast<char*>(&sb), sizeof(SuperBlock));

    // Find directory inode
    int dirInodeIdx = 0;
    if (dirPath != "/") {
        std::vector<std::string> parts;
        std::istringstream ss(dirPath);
        std::string part;
        while (std::getline(ss, part, '/')) {
            if (!part.empty()) parts.push_back(part);
        }
        for (const auto& name : parts) {
            Inode inode;
            disk.seekg(sb.s_inode_start + dirInodeIdx * sizeof(Inode));
            disk.read(reinterpret_cast<char*>(&inode), sizeof(Inode));
            bool found = false;
            for (int i = 0; i < 12 && !found; i++) {
                if (inode.i_block[i] == -1) continue;
                FolderBlock fb;
                disk.seekg(sb.s_block_start + inode.i_block[i] * sizeof(FileBlock));
                disk.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
                for (int j = 0; j < 4; j++) {
                    if (fb.b_content[j].b_inodo == -1) continue;
                    std::string fname(fb.b_content[j].b_name, strnlen(fb.b_content[j].b_name, 12));
                    if (fname == name) {
                        dirInodeIdx = fb.b_content[j].b_inodo;
                        found = true;
                        break;
                    }
                }
            }
            if (!found) {
                disk.close();
                return "ERROR rep: No se encontro la carpeta: " + dirPath;
            }
        }
    }

    // Build table
    std::ostringstream dot;
    dot << "digraph Ls {\n";
    dot << "  node [shape=plaintext]\n";
    dot << "  ls [label=<\n";
    dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
    dot << "    <TR><TD COLSPAN=\"7\" BGCOLOR=\"#4472C4\"><FONT COLOR=\"white\"><B>LS: " << dirPath << "</B></FONT></TD></TR>\n";
    dot << "    <TR><TD><B>Permisos</B></TD><TD><B>Owner</B></TD><TD><B>Grupo</B></TD><TD><B>Size</B></TD><TD><B>Fecha</B></TD><TD><B>Tipo</B></TD><TD><B>Name</B></TD></TR>\n";

    Inode dirInode;
    disk.seekg(sb.s_inode_start + dirInodeIdx * sizeof(Inode));
    disk.read(reinterpret_cast<char*>(&dirInode), sizeof(Inode));

    for (int b = 0; b < 12; b++) {
        if (dirInode.i_block[b] == -1) continue;
        FolderBlock fb;
        disk.seekg(sb.s_block_start + dirInode.i_block[b] * sizeof(FileBlock));
        disk.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
        for (int j = 0; j < 4; j++) {
            if (fb.b_content[j].b_inodo == -1) continue;
            std::string fname(fb.b_content[j].b_name, strnlen(fb.b_content[j].b_name, 12));
            if (fname == "." || fname == "..") continue;

            Inode childInode;
            disk.seekg(sb.s_inode_start + fb.b_content[j].b_inodo * sizeof(Inode));
            disk.read(reinterpret_cast<char*>(&childInode), sizeof(Inode));

            std::string perm(childInode.i_perm, 3);
            std::string type = (childInode.i_type == '0') ? "Carpeta" : "Archivo";

            dot << "    <TR><TD>" << perm << "</TD><TD>" << childInode.i_uid << "</TD><TD>" << childInode.i_gid << "</TD>";
            dot << "<TD>" << childInode.i_s << "</TD><TD>" << formatTime(childInode.i_mtime) << "</TD>";
            dot << "<TD>" << type << "</TD><TD>" << fname << "</TD></TR>\n";
        }
    }

    dot << "    </TABLE>>]\n";
    dot << "}\n";

    disk.close();

    createParentDirs(outputPath);
    std::string dotPath = outputPath + ".dot";
    std::ofstream dotFile(dotPath);
    dotFile << dot.str();
    dotFile.close();

    std::string ext = getExtension(outputPath);
    if (ext.empty()) ext = "png";
    std::string cmd = "dot -T" + ext + " " + dotPath + " -o " + outputPath + " 2>/dev/null";
    system(cmd.c_str());

    return "REP: Reporte Ls generado en " + outputPath;
}

// ==================== MAIN REP COMMAND ====================
std::string cmd_rep(const ParsedCommand& cmd) {
    if (cmd.params.find("name") == cmd.params.end()) return "ERROR rep: Parametro -name es obligatorio.";
    if (cmd.params.find("path") == cmd.params.end()) return "ERROR rep: Parametro -path es obligatorio.";
    if (cmd.params.find("id") == cmd.params.end()) return "ERROR rep: Parametro -id es obligatorio.";

    std::string name = toLowerStr(cmd.params.at("name"));
    std::string path = cmd.params.at("path");
    std::string id = cmd.params.at("id");

    // Clean up path - remove trailing quote if present
    while (!path.empty() && path.back() == '"') path.pop_back();

    MountedPartition* mp = findMounted(id);
    if (!mp) return "ERROR rep: No se encontro la particion montada con id " + id + ".";

    if (name == "mbr") {
        return reportMBR(mp->path, path);
    } else if (name == "disk") {
        return reportDisk(mp->path, path);
    } else if (name == "inode") {
        return reportInode(mp->path, mp->part_start, path);
    } else if (name == "block") {
        return reportBlock(mp->path, mp->part_start, path);
    } else if (name == "bm_inode") {
        return reportBmInode(mp->path, mp->part_start, path);
    } else if (name == "bm_block") {
        return reportBmBlock(mp->path, mp->part_start, path);
    } else if (name == "tree") {
        return reportTree(mp->path, mp->part_start, path);
    } else if (name == "sb") {
        return reportSb(mp->path, mp->part_start, path);
    } else if (name == "file") {
        std::string filePath = "/";
        if (cmd.params.find("path_file_ls") != cmd.params.end()) filePath = cmd.params.at("path_file_ls");
        return reportFile(mp->path, mp->part_start, path, filePath);
    } else if (name == "ls") {
        std::string dirPath = "/";
        if (cmd.params.find("path_file_ls") != cmd.params.end()) dirPath = cmd.params.at("path_file_ls");
        return reportLs(mp->path, mp->part_start, path, dirPath);
    } else {
        return "ERROR rep: Nombre de reporte no valido: " + name;
    }
}
