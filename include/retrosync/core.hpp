#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rs {
enum class Decision { Unchanged, Upload, Download, Conflict, LocalDeleted, RemoteDeleted, BothDeleted, Unsupported };
struct Copy { bool exists; std::string hash; };
// Hashes must refer to the same canonical save format. Empty hash means unknown.
Decision reconcile(const Copy& local, const Copy& remote, const Copy& baseline, bool compatible);
bool safeFilename(const std::string& name);
bool safeSdDirectory(const std::string& path);
bool validServerUrl(const std::string& url);
std::string urlEncode(const std::string& value);
std::string displayText(const std::string& value, size_t maxBytes);

struct OafConfig { bool useSavesFolder = true; unsigned slot = 0; };
bool parseOafConfig(const std::string& ini, OafConfig& config, std::string& error);
bool oafSavePath(const std::string& rom, const OafConfig& config, std::string& path);
enum class SaveType { Unknown, Sram32K, Flash64K, Flash128K, Eeprom512, Eeprom8K };
enum class SaveEncoding { Unknown, RawEmulator, OpenAgbFirm };
// Size alone cannot establish type or byte order. Callers must supply verified metadata.
bool convertSave(const std::vector<uint8_t>& input, SaveType type, SaveEncoding from,
                 SaveEncoding to, std::vector<uint8_t>& output, std::string& error);

struct Settings {
    std::string server, token;
    std::string romDirectory = "sdmc:/roms/gba";
    bool allowHttp = false;
};
bool loadSettings(const std::string& path, Settings& settings, std::string& error);
bool saveSettings(const std::string& path, const Settings& settings, std::string& error);
bool validateSettings(const Settings& settings, std::string& error);
}
