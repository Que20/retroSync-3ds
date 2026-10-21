#pragma once
#include "core.hpp"
#include <atomic>

namespace rs {
constexpr unsigned PageSize = 20;
constexpr size_t MaxJsonBytes = 512 * 1024;
struct Platform { int id; std::string name, slug; };
struct Rom { int id, platformId; std::string title, filename, summary; uint64_t size; };
struct SaveInfo { int id; std::string filename, slot, emulator, hash, updated; uint64_t size; bool archival = true; };
struct RomPage { std::vector<Rom> items; unsigned total = 0, offset = 0; };
bool parseHeartbeat(const std::string& json, std::string& version, std::string& error);
bool parsePlatforms(const std::string& json, std::vector<Platform>& platforms, std::string& error);
bool parseRoms(const std::string& json, RomPage& page, std::string& error);
bool parseSaves(const std::string& json, std::vector<SaveInfo>& saves, std::string& error);
std::string romListPath(int platformId, unsigned offset, const std::string& search);

struct HttpResponse { long status = 0; std::string body, error; };
// curl_global_init/cleanup are owned by the application, outside worker lifetime.
HttpResponse getJson(const Settings& settings, const std::string& path, const std::string& caFile,
                     std::atomic<bool>& cancel, std::atomic<unsigned>& received);
}
