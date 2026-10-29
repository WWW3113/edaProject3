#include <algorithm>
#include <charconv>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string_view>

#include "Design.h"

namespace {

using Tokens = std::vector<std::string_view>;

bool readFile(const std::string& path, std::string& buf) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    std::streamoff n = in.tellg();
    in.seekg(0, std::ios::beg);
    buf.resize(static_cast<size_t>(n));
    if (n > 0) in.read(&buf[0], n);
    return static_cast<bool>(in) || in.eof();
}

inline bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
}

// Splits a whole-file buffer into lines of tokens. '#' starts a comment and
// ':' is always a token of its own, so "NumNodes:10" and "NumNodes : 10"
// tokenize identically.
class LineReader {
public:
    explicit LineReader(const std::string& buf)
        : p_(buf.data()), end_(buf.data() + buf.size()) {}

    bool next(Tokens& toks) {
        toks.clear();
        while (p_ < end_) {
            const char* ls = p_;
            const char* le = static_cast<const char*>(std::memchr(p_, '\n', end_ - p_));
            if (!le) le = end_;
            p_ = (le < end_) ? le + 1 : end_;
            ++line_;
            split(ls, le, toks);
            if (!toks.empty()) return true;
        }
        return false;
    }
    int line() const { return line_; }

private:
    static void split(const char* s, const char* e, Tokens& toks) {
        while (s < e) {
            char c = *s;
            if (c == '#') break;
            if (isSpace(c)) { ++s; continue; }
            if (c == ':') { toks.emplace_back(s, 1); ++s; continue; }
            const char* b = s;
            while (s < e && !isSpace(*s) && *s != ':' && *s != '#') ++s;
            toks.emplace_back(b, static_cast<size_t>(s - b));
        }
    }

    const char* p_;
    const char* end_;
    int line_ = 0;
};

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

bool toDouble(std::string_view s, double& v) {
    if (!s.empty() && s[0] == '+') s.remove_prefix(1);
    auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    return r.ec == std::errc() && r.ptr == s.data() + s.size();
}

bool toLong(std::string_view s, long long& v) {
    double d;
    if (!toDouble(s, d)) return false;
    v = static_cast<long long>(d);
    return true;
}

bool isHeader(const Tokens& t) { return iequals(t[0], "UCLA"); }

// Value following "Key :" (or "Key") starting at token index i (the key).
bool valueAfter(const Tokens& t, size_t i, std::string_view& v, size_t* next = nullptr) {
    size_t j = i + 1;
    if (j < t.size() && t[j] == ":") ++j;
    if (j >= t.size()) return false;
    v = t[j];
    if (next) *next = j + 1;
    return true;
}

std::string dirOf(const std::string& path) {
    size_t p = path.find_last_of('/');
    return p == std::string::npos ? std::string() : path.substr(0, p + 1);
}

bool endsWith(const std::string& s, const char* suf) {
    size_t n = std::strlen(suf);
    return s.size() >= n && iequals(std::string_view(s).substr(s.size() - n), suf);
}

std::string fail(const std::string& file, int line, const std::string& msg) {
    return file + ":" + std::to_string(line) + ": " + msg;
}

bool parseAux(Design& d, const std::string& base, std::string& err) {
    d.auxPath = base + ".aux";
    std::string buf;
    std::string dir = dirOf(base);
    if (readFile(d.auxPath, buf)) {
        LineReader lr(buf);
        Tokens t;
        while (lr.next(t)) {
            size_t i = 0;
            if (t.size() >= 2 && t[1] == ":") {
                d.auxKeyword = std::string(t[0]);
                i = 2;
            }
            for (; i < t.size(); ++i) {
                if (t[i] == ":") continue;
                std::string f(t[i]);
                d.auxFiles.push_back(f);
                std::string path = (f.empty() || f[0] == '/') ? f : dir + f;
                if (endsWith(f, ".nodes")) d.nodesFile = path;
                else if (endsWith(f, ".nets")) d.netsFile = path;
                else if (endsWith(f, ".wts")) d.wtsFile = path;
                else if (endsWith(f, ".pl")) d.plFile = path;
                else if (endsWith(f, ".scl")) d.sclFile = path;
            }
        }
    } else {
        std::cerr << "warning: cannot read " << d.auxPath << ", using default file names\n";
    }
    // Fall back to <base>.<ext> for anything the .aux did not name.
    if (d.nodesFile.empty()) d.nodesFile = base + ".nodes";
    if (d.netsFile.empty()) d.netsFile = base + ".nets";
    if (d.plFile.empty()) d.plFile = base + ".pl";
    if (d.sclFile.empty()) d.sclFile = base + ".scl";
    if (d.wtsFile.empty()) d.wtsFile = base + ".wts";
    if (d.auxFiles.empty()) {
        std::string b = base.substr(dir.size());
        d.auxFiles = {b + ".nodes", b + ".nets", b + ".wts", b + ".pl", b + ".scl"};
    }
    (void)err;
    return true;
}

bool parseNodes(Design& d, std::string& err) {
    std::string buf;
    if (!readFile(d.nodesFile, buf)) { err = "cannot read " + d.nodesFile; return false; }
    LineReader lr(buf);
    Tokens t;
    while (lr.next(t)) {
        if (isHeader(t) || iequals(t[0], "NumNodes") || iequals(t[0], "NumTerminals")) continue;
        if (t.size() < 3) { err = fail(d.nodesFile, lr.line(), "malformed node line"); return false; }
        Cell c;
        c.name = std::string(t[0]);
        if (!toDouble(t[1], c.w) || !toDouble(t[2], c.h)) {
            err = fail(d.nodesFile, lr.line(), "bad node size");
            return false;
        }
        for (size_t i = 3; i < t.size(); ++i) {
            if (iequals(t[i], "terminal")) c.fixed = true;
            else if (iequals(t[i], "terminal_NI")) c.fixed = c.ni = true;
        }
        int id = static_cast<int>(d.cells.size());
        if (!d.cellIdx.emplace(c.name, id).second) {
            err = fail(d.nodesFile, lr.line(), "duplicate node " + c.name);
            return false;
        }
        d.cells.push_back(std::move(c));
    }
    return true;
}

int lookup(const Design& d, std::string_view name) {
    auto it = d.cellIdx.find(std::string(name));
    return it == d.cellIdx.end() ? -1 : it->second;
}

bool parseNets(Design& d, std::string& err) {
    std::string buf;
    if (!readFile(d.netsFile, buf)) { err = "cannot read " + d.netsFile; return false; }
    LineReader lr(buf);
    Tokens t;
    Net* cur = nullptr;
    auto finish = [&]() {
        if (!cur) return;
        auto& p = cur->pins;
        std::sort(p.begin(), p.end());
        p.erase(std::unique(p.begin(), p.end()), p.end());
    };
    std::string key;
    while (lr.next(t)) {
        if (isHeader(t) || iequals(t[0], "NumNets") || iequals(t[0], "NumPins")) continue;
        if (iequals(t[0], "NetDegree")) {
            finish();
            d.nets.emplace_back();
            cur = &d.nets.back();
            size_t j;
            std::string_view deg;
            if (!valueAfter(t, 0, deg, &j)) {
                err = fail(d.netsFile, lr.line(), "malformed NetDegree line");
                return false;
            }
            long long k;
            if (toLong(deg, k) && k > 0) cur->pins.reserve(static_cast<size_t>(k));
            cur->name = j < t.size() ? std::string(t[j]) : "net" + std::to_string(d.nets.size() - 1);
            continue;
        }
        if (!cur) { err = fail(d.netsFile, lr.line(), "pin before NetDegree"); return false; }
        // "obj I|O|B [: dx dy]" -- the offset is read past and ignored.
        int id = lookup(d, t[0]);
        if (id < 0) {
            err = fail(d.netsFile, lr.line(), "unknown object " + std::string(t[0]));
            return false;
        }
        cur->pins.push_back(id);
        ++d.numPins;
    }
    finish();
    return true;
}

bool parsePl(Design& d, std::string& err) {
    std::string buf;
    if (!readFile(d.plFile, buf)) { err = "cannot read " + d.plFile; return false; }
    LineReader lr(buf);
    Tokens t;
    while (lr.next(t)) {
        if (isHeader(t)) continue;
        if (t.size() < 3) { err = fail(d.plFile, lr.line(), "malformed pl line"); return false; }
        int id = lookup(d, t[0]);
        if (id < 0) {
            err = fail(d.plFile, lr.line(), "unknown object " + std::string(t[0]));
            return false;
        }
        Cell& c = d.cells[id];
        if (!toDouble(t[1], c.gx) || !toDouble(t[2], c.gy)) {
            err = fail(d.plFile, lr.line(), "bad coordinates");
            return false;
        }
        c.xStr = std::string(t[1]);
        c.yStr = std::string(t[2]);
        for (size_t i = 3; i < t.size(); ++i) {
            if (t[i] == ":") continue;
            if (t[i][0] == '/') {
                if (!c.fixedTag.empty()) c.fixedTag += ' ';
                c.fixedTag += std::string(t[i]);
                if (iequals(t[i], "/FIXED")) c.fixed = true;
                else if (iequals(t[i], "/FIXED_NI")) c.fixed = c.ni = true;
            } else if (c.orient.empty()) {
                c.orient = std::string(t[i]);
            }
        }
        if (!c.inPl) d.plOrder.push_back(id);
        c.inPl = true;
    }
    return true;
}

bool parseScl(Design& d, std::string& err) {
    std::string buf;
    if (!readFile(d.sclFile, buf)) { err = "cannot read " + d.sclFile; return false; }
    LineReader lr(buf);
    Tokens t;
    Row* cur = nullptr;
    double pendingOrigin = 0;
    bool havePending = false;
    while (lr.next(t)) {
        if (isHeader(t) || iequals(t[0], "NumRows")) continue;
        if (iequals(t[0], "CoreRow")) {
            d.rows.emplace_back();
            cur = &d.rows.back();
            havePending = false;
            continue;
        }
        if (iequals(t[0], "End")) { cur = nullptr; continue; }
        if (!cur) continue;
        for (size_t i = 0; i < t.size(); ++i) {
            std::string_view key = t[i], v;
            if (key == ":") continue;
            size_t next;
            if (!valueAfter(t, i, v, &next)) break;
            double val;
            bool num = toDouble(v, val);
            if (iequals(key, "Coordinate") && num) cur->y = val;
            else if (iequals(key, "Height") && num) cur->h = val;
            else if (iequals(key, "Sitewidth") && num) cur->siteW = val;
            else if (iequals(key, "Sitespacing") && num) cur->siteSp = val;
            else if (iequals(key, "SubrowOrigin") && num) { pendingOrigin = val; havePending = true; }
            else if (iequals(key, "NumSites") && num) {
                if (!havePending) {
                    err = fail(d.sclFile, lr.line(), "NumSites without SubrowOrigin");
                    return false;
                }
                cur->subrows.push_back({pendingOrigin, static_cast<long long>(val)});
                havePending = false;
            } else {
                continue;  // Siteorient, Sitesymmetry, unknown keys: skip key only
            }
            i = next - 1;
        }
    }
    for (Row& r : d.rows)
        std::sort(r.subrows.begin(), r.subrows.end(),
                  [](const Subrow& a, const Subrow& b) { return a.origin < b.origin; });
    std::stable_sort(d.rows.begin(), d.rows.end(),
                     [](const Row& a, const Row& b) { return a.y < b.y; });
    return true;
}

}  // namespace

bool parseDesign(const std::string& base, Design& d, std::string& err) {
    if (!parseAux(d, base, err)) return false;
    if (!parseNodes(d, err)) return false;
    if (!parseNets(d, err)) return false;
    if (!parsePl(d, err)) return false;
    if (!parseScl(d, err)) return false;

    for (size_t i = 0; i < d.cells.size(); ++i) {
        const Cell& c = d.cells[i];
        if (!c.inPl) {
            std::cerr << "warning: " << c.name << " missing from .pl, assuming (0,0)\n";
            d.plOrder.push_back(static_cast<int>(i));
        }
        if (!c.fixed) d.movable.push_back(static_cast<int>(i));
    }
    if (d.rows.empty()) { err = "no rows in " + d.sclFile; return false; }
    return true;
}
