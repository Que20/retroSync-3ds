#include "retrosync/core.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace rs {
Decision reconcile(const Copy& l, const Copy& r, const Copy& b, bool compatible) {
    if (!compatible) return Decision::Unsupported;
    if (!l.exists && !r.exists) return b.exists ? Decision::BothDeleted : Decision::Unchanged;
    if ((l.exists && l.hash.empty()) || (r.exists && r.hash.empty()) || (b.exists && b.hash.empty()))
        return Decision::Unsupported;
    if (!l.exists) return b.exists ? Decision::LocalDeleted : Decision::Download;
    if (!r.exists) return b.exists ? Decision::RemoteDeleted : Decision::Upload;
    if (l.hash == r.hash) return Decision::Unchanged;
    if (!b.exists) return Decision::Conflict;
    if (l.hash == b.hash) return Decision::Download;
    if (r.hash == b.hash) return Decision::Upload;
    return Decision::Conflict;
}
bool safeFilename(const std::string& s) {
    if (s.empty() || s.size() > 180 || s == "." || s == ".." || s.back() == '.' || s.back() == ' ') return false;
    for (unsigned char c : s) if (c < 32 || c == 127 || std::strchr("/\\:*?\"<>|", c)) return false;
    return true;
}
bool safeSdDirectory(const std::string& s) {
    if (s.compare(0, 6, "sdmc:/") != 0 || s.size() > 400) return false;
    if (s.size() == 6) return true;
    size_t start = 6;
    while (start < s.size()) {
        size_t end = s.find('/', start);
        if (end == std::string::npos) end = s.size();
        if (!safeFilename(s.substr(start, end - start))) return false;
        start = end + 1;
    }
    return s.back() != '/';
}
bool validServerUrl(const std::string& s) {
    size_t begin = s.compare(0, 8, "https://") == 0 ? 8 : s.compare(0, 7, "http://") == 0 ? 7 : 0;
    if (!begin || s.size() <= begin || s.size() > 512 || s.back() == '/') return false;
    for (unsigned char c : s) if (c <= 32 || c >= 127 || c == '@' || c == '?' || c == '#' || c == '\\') return false;
    auto end = s.find('/', begin);
    std::string authority = s.substr(begin, end == std::string::npos ? end : end - begin);
    return !authority.empty() && authority[0] != ':';
}
std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') out += c;
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}
std::string displayText(const std::string& s, size_t maxBytes) {
    // libctru console has a limited font. Reject control sequences from untrusted metadata.
    std::string out;
    for (unsigned char c : s) {
        if (out.size() == maxBytes) break;
        out += c >= 32 && c < 127 ? static_cast<char>(c) : ' ';
    }
    return out;
}
static std::string trim(const std::string& s) {
    auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}
bool parseOafConfig(const std::string& ini, OafConfig& config, std::string& error) {
    if (ini.size() > 65536) { error = "Configuration too large"; return false; }
    OafConfig candidate = config;
    std::string section;
    size_t start = 0;
    while (start < ini.size()) {
        size_t end = ini.find('\n', start);
        if (end == std::string::npos) end = ini.size();
        std::string line = trim(ini.substr(start, end - start)); start = end + 1;
        if (line.compare(0,3,"\xef\xbb\xbf")==0) line=line.substr(3);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        for (size_t i=1;i<line.size();++i) if (line[i]==';' && std::isspace(static_cast<unsigned char>(line[i-1]))) {
            line=trim(line.substr(0,i)); break;
        }
        if (line[0] == '[' && line.back() == ']') { section = line.substr(1, line.size()-2); continue; }
        size_t eq = line.find_first_of("=:");
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq+1));
        if (key == "useSavesFolder" && section == "general") {
            if (value != "true" && value != "false") { error = "Invalid useSavesFolder"; return false; }
            candidate.useSavesFolder = value == "true";
        }
        if (key == "saveSlot" && section == "game") {
            if (value.size() != 1 || value[0] < '0' || value[0] > '9') { error = "Invalid saveSlot"; return false; }
            candidate.slot = value[0] - '0';
        }
    }
    config = candidate; return true;
}
bool oafSavePath(const std::string& rom, const OafConfig& c, std::string& out) {
    size_t slash = rom.find_last_of('/');
    if (slash == std::string::npos || !safeSdDirectory(rom.substr(0, slash)) || c.slot > 9) return false;
    std::string name = rom.substr(slash+1);
    if (!safeFilename(name) || name.size() <= 4) return false;
    std::string ext = name.substr(name.size()-4);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char x) { return std::tolower(x); });
    if (ext != ".gba") return false;
    out = (c.useSavesFolder ? "sdmc:/3ds/open_agb_firm/saves/" : rom.substr(0,slash+1)) + name.substr(0,name.size()-4);
    if (c.slot) out += "." + std::to_string(c.slot);
    out += ".sav"; return true;
}
bool convertSave(const std::vector<uint8_t>& in, SaveType type, SaveEncoding from, SaveEncoding to,
                 std::vector<uint8_t>& out, std::string& error) {
    size_t size = 0;
    switch (type) {
    case SaveType::Sram32K: size = 32768; break;
    case SaveType::Flash64K: size = 65536; break;
    case SaveType::Flash128K: size = 131072; break;
    case SaveType::Eeprom512: size = 512; break;
    case SaveType::Eeprom8K: size = 8192; break;
    default: error = "Save type unknown; original preserved"; return false;
    }
    if (from == SaveEncoding::Unknown || to == SaveEncoding::Unknown || in.size() != size) {
        error = "Save encoding or size unverified; original preserved"; return false;
    }
    // Generic battery data has no reliable magic/checksum. Reject obvious wrong containers only;
    // verified producer/core/type metadata remains mandatory, even if the length matches.
    if (in.size() >= 4 && (std::memcmp(in.data(), "PK\003\004", 4) == 0 ||
        std::memcmp(in.data(), "\x1f\x8b", 2) == 0)) {
        error = "Compressed save container unsupported"; return false;
    }
    out = in;
    if (from != to && (type == SaveType::Eeprom512 || type == SaveType::Eeprom8K))
        for (size_t i = 0; i < out.size(); i += 8) std::reverse(out.begin()+i, out.begin()+i+8);
    return true;
}
bool validateSettings(const Settings& s, std::string& error) {
    if (!validServerUrl(s.server)) { error = "Use a server URL without trailing slash, credentials or query"; return false; }
    if (s.server.compare(0,7,"http://") == 0 && !s.allowHttp) { error = "HTTP requires explicit LAN opt-in"; return false; }
    if (s.token.empty() || s.token.size() > 512) { error = "Set a client API token"; return false; }
    for (unsigned char c : s.token) if (c <= 32 || c >= 127) { error = "Invalid token characters"; return false; }
    if (!safeSdDirectory(s.romDirectory)) { error = "ROM directory must be a safe sdmc:/ path"; return false; }
    return true;
}
static bool readSettings(const std::string& path, Settings& s, std::string& error) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { error = "No settings yet. Press SELECT to configure."; return false; }
    char data[4097]; size_t n = std::fread(data, 1, sizeof(data), f);
    bool readError = std::ferror(f); std::fclose(f);
    if (readError || n > 4096) { error = "Settings unreadable or too large"; return false; }
    std::string text(data,n); size_t start = 0; Settings candidate;
    while (start < text.size()) {
        size_t end = text.find('\n',start); if (end == std::string::npos) end = text.size();
        std::string line = trim(text.substr(start,end-start)); start=end+1;
        size_t eq = line.find('='); if (eq == std::string::npos) continue;
        std::string k = line.substr(0,eq), v=line.substr(eq+1);
        if (k == "server") candidate.server=v;
        if (k == "token") candidate.token=v;
        if (k == "rom_directory") candidate.romDirectory=v;
        if (k == "allow_http") candidate.allowHttp=v == "true";
    }
    if (!validateSettings(candidate,error)) return false;
    s = candidate; return true;
}
bool loadSettings(const std::string& path, Settings& s, std::string& error) {
    if (readSettings(path,s,error)) { error.clear(); return true; }
    std::string ignored;
    if (readSettings(path+".bak",s,ignored)) { error="Recovered settings backup; save settings again"; return true; }
    return false;
}
static bool writeSettingsFile(const std::string& path, const Settings& s) {
    FILE* f = std::fopen(path.c_str(),"wb");
    if (!f) return false;
    bool ok = std::fprintf(f,"server=%s\ntoken=%s\nrom_directory=%s\nallow_http=%s\n",s.server.c_str(),s.token.c_str(),s.romDirectory.c_str(),s.allowHttp?"true":"false") > 0;
    if (std::fflush(f) != 0) ok=false;
    if (fsync(fileno(f)) != 0) ok=false;
    if (std::fclose(f) != 0) ok=false;
    return ok;
}
bool saveSettings(const std::string& path, const Settings& s, std::string& error) {
    if (!validateSettings(s,error)) return false;
    const std::string temp=path+".tmp", backup=path+".bak", backupTemp=path+".bak.tmp";
    if (!writeSettingsFile(temp,s)) { error="Cannot flush settings temporary file"; std::remove(temp.c_str()); return false; }
    // libctru rename deletes an existing destination before moving. Keep a flushed backup.
    Settings prior; std::string ignored;
    if (readSettings(path,prior,ignored)) {
        if (!writeSettingsFile(backupTemp,prior) || std::rename(backupTemp.c_str(),backup.c_str())!=0) {
            error="Cannot preserve previous settings"; std::remove(temp.c_str()); std::remove(backupTemp.c_str()); return false;
        }
    }
    if (std::rename(temp.c_str(),path.c_str()) != 0) {
        error="Cannot commit settings; check main file and backup"; std::remove(temp.c_str()); return false;
    }
    error.clear();
    return true;
}
}
