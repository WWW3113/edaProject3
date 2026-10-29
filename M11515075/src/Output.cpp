#include "Output.h"

#include <cmath>
#include <cstdio>
#include <fstream>

namespace {

// Coordinates: integers print exactly; otherwise enough digits to round-trip
// a site-aligned value.
void appendCoord(std::string& s, double v) {
    char buf[64];
    double r = std::round(v);
    if (std::fabs(v - r) < 1e-9 && std::fabs(r) < 9e15) {
        std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(r));
    } else {
        std::snprintf(buf, sizeof buf, "%.6f", v);
        char* e = buf + std::char_traits<char>::length(buf) - 1;
        while (*e == '0') *e-- = '\0';
        if (*e == '.') *e = '\0';
    }
    s += buf;
}

std::string baseName(const std::string& path) {
    size_t p = path.find_last_of('/');
    return p == std::string::npos ? path : path.substr(p + 1);
}

std::string extOf(const std::string& f) {
    size_t p = f.find_last_of('.');
    return p == std::string::npos ? std::string() : f.substr(p);
}

bool copyFile(const std::string& from, const std::string& to) {
    std::ifstream in(from, std::ios::binary);
    if (!in) return false;
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << in.rdbuf();
    return static_cast<bool>(out);
}

}  // namespace

std::string formatNumber(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.3f", v);
    std::string s(buf);
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    return s;
}

bool writePl(const Design& d, const Placement& p, const std::string& path) {
    std::string s;
    s.reserve(d.cells.size() * 40 + 64);
    s += "UCLA pl 1.0\n# Created by dp\n\n";
    for (int id : d.plOrder) {
        const Cell& c = d.cells[id];
        s += c.name;
        s += '\t';
        if (c.fixed && c.inPl) {
            s += c.xStr;
            s += '\t';
            s += c.yStr;
        } else {
            appendCoord(s, c.fixed ? c.gx : p.x[id]);
            s += '\t';
            appendCoord(s, c.fixed ? c.gy : p.y[id]);
        }
        if (!c.orient.empty()) {
            s += "\t: ";
            s += c.orient;
        }
        if (!c.fixedTag.empty()) {
            s += ' ';
            s += c.fixedTag;
        }
        s += '\n';
    }
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(s.data(), 1, s.size(), f) == s.size();
    return std::fclose(f) == 0 && ok;
}

bool writeAuxAndCopies(const Design& d, const std::string& outBase, std::string& err) {
    std::string bn = baseName(outBase);
    std::string aux = d.auxKeyword + " :";
    for (const std::string& f : d.auxFiles) aux += " " + bn + extOf(f);
    aux += "\n";
    {
        std::ofstream out(outBase + ".aux", std::ios::binary | std::ios::trunc);
        if (!out || !(out << aux)) { err = "cannot write " + outBase + ".aux"; return false; }
    }
    const std::pair<const std::string*, const char*> copies[] = {
        {&d.nodesFile, ".nodes"}, {&d.netsFile, ".nets"},
        {&d.sclFile, ".scl"}, {&d.wtsFile, ".wts"}};
    for (const auto& c : copies) {
        if (!copyFile(*c.first, outBase + c.second)) {
            if (std::string(c.second) == ".wts") continue;  // optional input
            err = "cannot copy " + *c.first + " to " + outBase + c.second;
            return false;
        }
    }
    return true;
}
