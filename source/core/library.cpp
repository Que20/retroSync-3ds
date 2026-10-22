#include "retrosync/library.hpp"
#include "cJSON.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>
namespace rs {
namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
Json json(const std::string &text) {
  return Json(
      cJSON_ParseWithLengthOpts(text.c_str(), text.size() + 1, nullptr, 1),
      cJSON_Delete);
}
const cJSON *field(const cJSON *j, const char *key) {
  return cJSON_GetObjectItemCaseSensitive(j, key);
}
std::string str(const cJSON *j, const char *key) {
  auto *v = field(j, key);
  return cJSON_IsString(v) ? v->valuestring : "";
}
int integer(const cJSON *j, const char *key) {
  auto *v = field(j, key);
  return cJSON_IsNumber(v) && std::isfinite(v->valuedouble) &&
                 v->valuedouble >= 0 && v->valuedouble <= INT_MAX &&
                 std::floor(v->valuedouble) == v->valuedouble
             ? static_cast<int>(v->valuedouble)
             : -1;
}
bool hash(const std::string &s, size_t n) {
  return s.size() == n &&
         s.find_first_not_of("0123456789abcdef") == std::string::npos;
}
std::string dump(const cJSON *j) {
  char *s = cJSON_PrintUnformatted(j);
  std::string out = s ? s : "";
  cJSON_free(s);
  return out;
}
Json object() { return Json(cJSON_CreateObject(), cJSON_Delete); }
void add(cJSON *j, const char *k, const std::string &value) {
  cJSON_AddStringToObject(j, k, value.c_str());
}
bool validSlot(const std::string &slot) {
  return !slot.empty() && slot.size() <= 64 &&
         slot.find_first_of("\r\n") == std::string::npos;
}
bool parseOne(const cJSON *j, SaveInfo &save, std::string &err) {
  auto a = Json(cJSON_CreateArray(), cJSON_Delete);
  cJSON_AddItemToArray(a.get(), cJSON_Duplicate(j, 1));
  std::vector<SaveInfo> rows;
  if (!parseSaves(dump(a.get()), rows, err) || rows.size() != 1)
    return false;
  save = rows[0];
  return true;
}
bool readJsonFile(const std::string &name, Json &j, std::string &err) {
  std::string raw;
  if (readFile(name, raw, 16384, err)) {
    j = json(raw);
    if (j) {
      err.clear();
      return true;
    }
  }
  if (readFile(name + ".bak", raw, 16384, err)) {
    j = json(raw);
    if (j) {
      err.clear();
      return true;
    }
  }
  err = "Metadata is damaged; backups preserved";
  return false;
}
std::string parent(const std::string &path) {
  return path.substr(0, path.find_last_of('/'));
}
bool validInstall(const Install &i) {
  return i.romId > 0 && i.platformId > 0 &&
         safeSdDirectory(parent(i.romPath)) &&
         safeFilename(i.romPath.substr(i.romPath.find_last_of('/') + 1)) &&
         i.romPath.size() > 4 &&
         i.romPath.substr(i.romPath.size() - 4) == ".gba" &&
         hash(i.romSha256, 64) && static_cast<int>(i.saveType) >= 0 &&
         static_cast<int>(i.saveType) <= 5 &&
         static_cast<int>(i.remoteEncoding) >= 0 &&
         static_cast<int>(i.remoteEncoding) <= 2 &&
         (i.localBase.empty() || hash(i.localBase, 32)) &&
         (i.remoteBase.empty() || hash(i.remoteBase, 32)) &&
         (i.pendingLocal.empty() || hash(i.pendingLocal, 32)) &&
         (i.pendingRemote.empty() || hash(i.pendingRemote, 32)) &&
         (i.phase == "installed" || i.phase == "installing" ||
          i.phase == "upload-unconfirmed") &&
         (i.phase != "upload-unconfirmed" ||
          (!i.pendingLocal.empty() && !i.pendingRemote.empty())) &&
         i.title.size() <= 256 && i.emulator.size() <= 64 &&
         (i.slot.empty() || validSlot(i.slot));
}
bool writeBytes(const std::string &path, const std::vector<uint8_t> &data,
                std::string &err) {
  FILE *f = std::fopen(path.c_str(), "wbx");
  if (!f) {
    err = "Cannot stage save";
    return false;
  }
  bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
  if (std::fflush(f) != 0)
    ok = false;
  if (fsync(fileno(f)) != 0)
    ok = false;
  if (std::fclose(f) != 0)
    ok = false;
  if (!ok) {
    std::remove(path.c_str());
    err = "Cannot flush save staging file";
  }
  return ok;
}
} // namespace
Library::Library(Settings &settings, const std::string &caFile,
                 Progress &progress, const std::string &storage)
    : s(settings), ca(caFile), root(storage), p(progress) {}
bool Library::open(std::string &err) {
  auto r = requestJson(s, "/api/heartbeat", ca, p);
  if (!r.error.empty()) {
    err = r.error;
    return false;
  }
  if (!parseHeartbeat(r.body, version, err))
    return false;
  r = requestJson(s, "/api/users/me", ca, p);
  if (!r.error.empty()) {
    err = (r.status == 401 || r.status == 403)
              ? "Sign in with a RomM account to use downloads and saves"
              : r.error;
    return false;
  }
  auto j = json(r.body);
  user = integer(j.get(), "id");
  if (user < 1) {
    err = "Cannot verify server account identity";
    return false;
  }
  key = sha256Text(s.server + "\n" + std::to_string(user));
  account = root + "/accounts/" + key;
  return makeDirectories(account, err);
}
bool Library::put(const Install &i, std::string &err) {
  if (account.empty() || !validInstall(i)) {
    err = "Invalid install record; original preserved";
    return false;
  }
  auto j = object();
  cJSON_AddNumberToObject(j.get(), "version", 1);
  add(j.get(), "account", key);
  cJSON_AddNumberToObject(j.get(), "rom_id", i.romId);
  cJSON_AddNumberToObject(j.get(), "platform_id", i.platformId);
  cJSON_AddNumberToObject(j.get(), "remote_id", i.remoteId);
  cJSON_AddNumberToObject(j.get(), "save_type", static_cast<int>(i.saveType));
  cJSON_AddNumberToObject(j.get(), "remote_encoding",
                          static_cast<int>(i.remoteEncoding));
  add(j.get(), "title", i.title);
  add(j.get(), "rom_path", i.romPath);
  add(j.get(), "rom_sha256", i.romSha256);
  add(j.get(), "slot", i.slot);
  add(j.get(), "emulator", i.emulator);
  cJSON_AddBoolToObject(j.get(), "emulator_null", i.emulatorNull);
  add(j.get(), "save_path", i.savePath);
  add(j.get(), "local_base", i.localBase);
  add(j.get(), "remote_base", i.remoteBase);
  add(j.get(), "phase", i.phase);
  add(j.get(), "pending_local", i.pendingLocal);
  add(j.get(), "pending_remote", i.pendingRemote);
  return writeFile(account + "/rom-" + std::to_string(i.romId) + ".json",
                   dump(j.get()), err);
}
bool Library::load(int id, Install &i, std::string &err) {
  if (account.empty() || id <= 0) {
    err = "No verified account";
    return false;
  }
  const auto path = account + "/rom-" + std::to_string(id) + ".json";
  i = Install{};
  if (!fileExists(path) && !fileExists(path + ".bak")) {
    err.clear();
    return true;
  }
  Json j(nullptr, cJSON_Delete);
  if (!readJsonFile(path, j, err))
    return false;
  if (integer(j.get(), "version") != 1 || str(j.get(), "account") != key ||
      integer(j.get(), "rom_id") != id) {
    err = "Install record belongs to another account/version";
    return false;
  }
  i.romId = id;
  i.platformId = integer(j.get(), "platform_id");
  i.remoteId = integer(j.get(), "remote_id");
  i.title = str(j.get(), "title");
  i.romPath = str(j.get(), "rom_path");
  i.romSha256 = str(j.get(), "rom_sha256");
  i.saveType = static_cast<SaveType>(integer(j.get(), "save_type"));
  i.remoteEncoding =
      static_cast<SaveEncoding>(integer(j.get(), "remote_encoding"));
  i.slot = str(j.get(), "slot");
  i.emulator = str(j.get(), "emulator");
  i.emulatorNull = cJSON_IsTrue(field(j.get(), "emulator_null"));
  i.savePath = str(j.get(), "save_path");
  i.localBase = str(j.get(), "local_base");
  i.remoteBase = str(j.get(), "remote_base");
  i.phase = str(j.get(), "phase");
  i.pendingLocal = str(j.get(), "pending_local");
  i.pendingRemote = str(j.get(), "pending_remote");
  if (!validInstall(i) ||
      i.romPath.substr(i.romPath.find_last_of('/') + 1) !=
          "rs-" + key.substr(0, 16) + "-" + std::to_string(id) + ".gba") {
    err = "Invalid install paths or hashes";
    return false;
  }
  if (!recoverReplacement(i.romPath, err))
    return false;
  if (i.phase == "installing" && fileExists(i.romPath)) {
    std::string m, h;
    if (hashFile(i.romPath, m, h, err) && h == i.romSha256) {
      i.phase = "installed";
      return put(i, err);
    }
  }
  if (i.phase == "installing") {
    std::string m, h;
    const auto staged = i.romPath + ".part";
    if (fileExists(staged) && hashFile(staged, m, h, err) && h == i.romSha256 &&
        !fileExists(i.romPath)) {
      if (!replaceFile(staged, i.romPath, "", err))
        return false;
      i.phase = "installed";
      return put(i, err);
    }
    if (!fileExists(i.romPath)) {
      // No installed bytes were committed. A fresh download is safe; retain the
      // record as a backup.
      std::remove((path + ".interrupted").c_str());
      if (std::rename(path.c_str(), (path + ".interrupted").c_str()) != 0) {
        err = "Cannot preserve interrupted install record";
        return false;
      }
      std::remove((path + ".bak").c_str());
      i = Install{};
    }
  }
  return true;
}
bool Library::download(const Rom &rom, const Platform &platform, Install &i,
                       std::string &err) {
  if (account.empty()) {
    err = "Sign in before installing";
    return false;
  }
  if (platform.slug != "gba") {
    err = "V1 installs GBA games for open_agb_firm";
    return false;
  }
  Install old;
  if (!load(rom.id, old, err))
    return false;
  if (old.romId) {
    std::string m, h;
    if (!hashFile(old.romPath, m, h, err) || h != old.romSha256) {
      err = "Installed ROM changed or missing; preserve it before repair";
      return false;
    }
    i = old;
    err.clear();
    return true;
  }
  auto r = requestJson(s, "/api/roms/" + std::to_string(rom.id), ca, p);
  if (!r.error.empty()) {
    err = r.error;
    return false;
  }
  auto j = json(r.body);
  auto *files = field(j.get(), "files");
  const cJSON *chosen = nullptr;
  unsigned count = 0;
  if (integer(j.get(), "id") != rom.id ||
      integer(j.get(), "platform_id") != platform.id || !cJSON_IsArray(files)) {
    err = "Unsupported ROM file metadata";
    return false;
  }
  const cJSON *f;
  cJSON_ArrayForEach(f, files) {
    if (cJSON_IsFalse(field(f, "is_top_level")))
      continue;
    chosen = f;
    ++count;
  }
  int fid = integer(chosen, "id");
  const auto name = str(chosen, "file_name"),
             expectedMd5 = str(chosen, "md5_hash");
  auto *size = field(chosen, "file_size_bytes");
  uint64_t length = cJSON_IsNumber(size) && size->valuedouble >= 192 &&
                            size->valuedouble <= 64 * 1024 * 1024
                        ? static_cast<uint64_t>(size->valuedouble)
                        : 0;
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  const bool zip = lower.size() > 4 && lower.substr(lower.size() - 4) == ".zip",
             gba = lower.size() > 4 && lower.substr(lower.size() - 4) == ".gba";
  if (count != 1 || fid < 1 || !length || !safeFilename(name) ||
      (!zip && !gba) || (!expectedMd5.empty() && !hash(expectedMd5, 32))) {
    err = "Select a single GBA or ZIP containing one GBA ROM";
    return false;
  }
  if (!makeDirectories(s.romDirectory, err))
    return false;
  if (freeBytes(s.romDirectory) < length + 32 * 1024 * 1024 + 1024 * 1024) {
    err = "Not enough SD space for download and extraction";
    return false;
  }
  i = Install{};
  i.romId = rom.id;
  i.platformId = platform.id;
  i.title = rom.title;
  i.romPath = s.romDirectory + "/rs-" + key.substr(0, 16) + "-" +
              std::to_string(rom.id) + ".gba";
  const auto staged = i.romPath + ".part", archive = i.romPath + ".download";
  if (fileExists(i.romPath)) {
    err = "Untracked destination exists; original preserved";
    return false;
  }
  std::remove(staged.c_str());
  std::remove(archive.c_str());
  auto path = "/api/roms/" + std::to_string(rom.id) + "/content/" +
              urlEncode(name) + "?file_ids=" + std::to_string(fid);
  if (!downloadFile(s, path, archive, 64 * 1024 * 1024, length, ca, p, err))
    return false;
  std::string md5, sha;
  if (!hashFile(archive, md5, sha, err) ||
      (!expectedMd5.empty() && md5 != expectedMd5)) {
    std::remove(archive.c_str());
    err = "Downloaded ROM checksum mismatch";
    return false;
  }
  if (zip) {
    if (!extractGba(archive, staged, p, err)) {
      std::remove(archive.c_str());
      return false;
    }
    std::remove(archive.c_str());
  } else if (std::rename(archive.c_str(), staged.c_str()) != 0) {
    err = "Cannot stage ROM";
    return false;
  }
  if (!inspectGba(staged, i.saveType, err) ||
      !hashFile(staged, md5, i.romSha256, err)) {
    std::remove(staged.c_str());
    return false;
  }
  if (p.cancel.load()) {
    std::remove(staged.c_str());
    err = "Download cancelled";
    return false;
  }
  i.phase = "installing";
  if (!put(i, err) || !replaceFile(staged, i.romPath, "", err))
    return false;
  i.phase = "installed";
  if (!put(i, err))
    return false;
  err.clear();
  return true;
}
bool Library::saveLocation(const Install &i, std::string &path,
                           std::string &err) {
  OafConfig config;
  std::string raw;
  const auto global = "sdmc:/3ds/open_agb_firm/config.ini";
  if (fileExists(global) &&
      (!readFile(global, raw, 65536, err) || !parseOafConfig(raw, config, err)))
    return false;
  OafConfig zero = config;
  zero.slot = 0;
  std::string base;
  if (!oafSavePath(i.romPath, zero, base)) {
    err = "Invalid open_agb_firm path";
    return false;
  }
  auto ini = base.substr(0, base.size() - 4) + ".ini";
  if (fileExists(ini) &&
      (!readFile(ini, raw, 65536, err) || !parseOafConfig(raw, config, err)))
    return false;
  auto configuredType = [](const std::string &value) {
    if (value == "sram_256k")
      return SaveType::Sram32K;
    if (value == "eeprom_8k" || value == "rom_256m_eeprom_8k")
      return SaveType::Eeprom512;
    if (value == "eeprom_64k" || value == "rom_256m_eeprom_64k")
      return SaveType::Eeprom8K;
    if (value.compare(0, 11, "flash_512k_") == 0)
      return SaveType::Flash64K;
    if (value.compare(0, 9, "flash_1m_") == 0)
      return SaveType::Flash128K;
    return SaveType::Unknown;
  };
  const auto overrideType = config.saveType != "auto" ? config.saveType
                            : config.saveOverride     ? config.defaultSave
                                                      : "auto";
  if (overrideType != "auto") {
    auto effective = configuredType(overrideType);
    if (effective == SaveType::Unknown ||
        (i.saveType != SaveType::Unknown && effective != i.saveType)) {
      err = "open_agb_firm save-type override differs; verify chip type before "
            "syncing";
      return false;
    }
  }
  if (!oafSavePath(i.romPath, config, path)) {
    err = "Unsupported open_agb_firm save slot";
    return false;
  }
  return true;
}
bool Library::bind(Install &i, const SaveInfo &selected, SaveType type,
                   SaveEncoding encoding, std::string &err) {
  if (i.phase == "upload-unconfirmed") {
    err = "Resolve the pending upload before relinking";
    return false;
  }
  if (selected.romId != i.romId || selected.userId != user) {
    err = "Selected save belongs to another game or account";
    return false;
  }
  if (!i.romId || selected.archival || !validSlot(selected.slot) ||
      type == SaveType::Unknown || encoding == SaveEncoding::Unknown) {
    err = "Choose a named slot, verified save chip and remote format";
    return false;
  }
  Install candidate = i;
  candidate.saveType = type;
  std::string path;
  if (!saveLocation(candidate, path, err))
    return false;
  // Relinking keeps the old baselines until the new relationship is explicitly
  // selected.
  i.slot = selected.slot;
  i.emulator = selected.emulator;
  i.emulatorNull = selected.emulatorNull;
  i.saveType = type;
  i.remoteEncoding = encoding;
  i.savePath = path;
  i.localBase.clear();
  i.remoteBase.clear();
  i.pendingLocal.clear();
  i.pendingRemote.clear();
  i.remoteId = 0;
  i.phase = "installed";
  return put(i, err);
}
bool Library::bindNew(Install &i, const std::string &slot, SaveType type,
                      std::string &err, SaveEncoding encoding) {
  if (i.phase == "upload-unconfirmed") {
    err = "Resolve the pending upload before relinking";
    return false;
  }
  if (!validSlot(slot) || type == SaveType::Unknown ||
      encoding == SaveEncoding::Unknown) {
    err = "Choose a slot name and verified save type";
    return false;
  }
  Install candidate = i;
  candidate.saveType = type;
  std::string path;
  if (!saveLocation(candidate, path, err))
    return false;
  i.slot = slot;
  i.emulator = encoding == SaveEncoding::RawEmulator ? "mgba" : "open_agb_firm";
  i.emulatorNull = false;
  i.remoteEncoding = encoding;
  i.saveType = type;
  i.savePath = path;
  i.localBase.clear();
  i.remoteBase.clear();
  i.pendingLocal.clear();
  i.pendingRemote.clear();
  i.remoteId = 0;
  i.phase = "installed";
  return put(i, err);
}
bool Library::deviceReady(std::string &err) {
  if (version != "5.4.0" && version.compare(0, 6, "5.4.0-") != 0) {
    err = "Safe save writes currently require RomM 5.4.0";
    return false;
  }
  const auto name = account + "/device.json";
  Json j(nullptr, cJSON_Delete);
  std::string hostname;
  if (fileExists(name) || fileExists(name + ".bak")) {
    if (!readJsonFile(name, j, err))
      return false;
    hostname = str(j.get(), "hostname");
    device = str(j.get(), "id");
  }
  if (hostname.empty()) {
    hostname =
        "retrosync-" + sha256Text(key + std::to_string(std::time(nullptr)) +
                                  std::to_string(std::clock()))
                           .substr(0, 20);
    j = object();
    add(j.get(), "hostname", hostname);
    add(j.get(), "id", "");
    if (!writeFile(name, dump(j.get()), err))
      return false;
  }
  if (!device.empty()) {
    if (device.size() != 36 ||
        device.find_first_not_of("0123456789abcdef-") != std::string::npos) {
      err = "Invalid registered device identifier";
      return false;
    }
    auto r = requestJson(s, "/api/devices/" + device, ca, p);
    if (r.error.empty()) {
      auto info = json(r.body);
      if (integer(info.get(), "user_id") != user ||
          !cJSON_IsTrue(field(info.get(), "sync_enabled"))) {
        err = "Device owner or sync permission changed";
        return false;
      }
      return true;
    }
    if (r.status != 404) {
      err = r.error;
      return false;
    }
    device.clear();
  }
  auto payload = object();
  add(payload.get(), "name", "RetroSync 3DS");
  add(payload.get(), "platform", "Nintendo 3DS");
  add(payload.get(), "client", "RetroSync3DS");
  add(payload.get(), "client_version", "1.0.0");
  add(payload.get(), "hostname", hostname);
  add(payload.get(), "sync_mode", "api");
  cJSON_AddBoolToObject(payload.get(), "allow_existing", true);
  Request req;
  req.method = "POST";
  req.body = dump(payload.get());
  auto r = requestJson(s, "/api/devices", ca, p, req);
  if (!r.error.empty()) {
    err = r.error;
    return false;
  }
  auto response = json(r.body);
  device = str(response.get(), "device_id");
  if (device.size() != 36 ||
      device.find_first_not_of("0123456789abcdef-") != std::string::npos) {
    err = "Unsupported device registration response";
    return false;
  }
  j = object();
  add(j.get(), "hostname", hostname);
  add(j.get(), "id", device);
  return writeFile(name, dump(j.get()), err);
}
namespace {
bool utcMicros(const std::string &value, int64_t &result) {
  auto digits = [&](size_t pos, size_t n, int &out) {
    if (pos + n > value.size())
      return false;
    out = 0;
    for (size_t k = pos; k < pos + n; ++k) {
      if (value[k] < '0' || value[k] > '9')
        return false;
      out = out * 10 + value[k] - '0';
    }
    return true;
  };
  int y, m, d, h, minute, second;
  if (value.size() < 20 || value[4] != '-' || value[7] != '-' ||
      value[10] != 'T' || value[13] != ':' || value[16] != ':' ||
      !digits(0, 4, y) || !digits(5, 2, m) || !digits(8, 2, d) ||
      !digits(11, 2, h) || !digits(14, 2, minute) || !digits(17, 2, second) ||
      y < 1970 || m < 1 || m > 12 || d < 1 || h > 23 || minute > 59 ||
      second > 59)
    return false;
  const int lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  bool leap = y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
  if (d > lengths[m - 1] + (m == 2 && leap ? 1 : 0))
    return false;
  size_t pos = 19;
  int fraction = 0, digitsCount = 0;
  if (pos < value.size() && value[pos] == '.') {
    ++pos;
    while (pos < value.size() && value[pos] >= '0' && value[pos] <= '9') {
      if (++digitsCount > 6)
        return false;
      fraction = fraction * 10 + value[pos++] - '0';
    }
    if (!digitsCount)
      return false;
  }
  for (; digitsCount < 6; ++digitsCount)
    fraction *= 10;
  int offset = 0;
  if (value.substr(pos) != "Z") {
    int oh, om;
    if (pos + 6 != value.size() || (value[pos] != '+' && value[pos] != '-') ||
        value[pos + 3] != ':' || !digits(pos + 1, 2, oh) ||
        !digits(pos + 4, 2, om) || oh > 14 || om > 59)
      return false;
    offset = (oh * 60 + om) * 60 * (value[pos] == '+' ? 1 : -1);
  }
  int year = y - (m <= 2 ? 1 : 0);
  int era = year / 400;
  unsigned yoe = static_cast<unsigned>(year - era * 400);
  unsigned doy =
      (153 * static_cast<unsigned>(m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int64_t days = static_cast<int64_t>(era) * 146097 + yoe * 365 + yoe / 4 -
                 yoe / 100 + doy - 719468;
  result = ((days * 24 + h) * 3600 + minute * 60 + second - offset) * 1000000 +
           fraction;
  return true;
}
} // namespace
bool Library::currentSave(const Install &i, SaveInfo &save, bool &exists,
                          std::string &err) {
  auto response = requestJson(s,
                              "/api/saves?rom_id=" + std::to_string(i.romId) +
                                  "&slot=" + urlEncode(i.slot),
                              ca, p);
  if (!response.error.empty()) {
    err = response.error;
    return false;
  }
  std::vector<SaveInfo> rows;
  if (!parseSaves(response.body, rows, err))
    return false;
  exists = false;
  int64_t latest = 0;
  // Match RomM 5.4's guarded-write ordering exactly: updated_at DESC, id DESC.
  // The summary endpoint can choose an older row when several uploads share a
  // timestamp.
  for (const auto &row : rows)
    if (!row.archival && row.slot == i.slot) {
      if (row.romId != i.romId || row.userId != user) {
        err = "Remote save identity mismatch";
        return false;
      }
      int64_t stamp;
      if (!utcMicros(row.updated, stamp)) {
        err = "Unsupported save timestamp; refusing to guess active version";
        return false;
      }
      if (!exists || stamp > latest || (stamp == latest && row.id > save.id)) {
        save = row;
        latest = stamp;
        exists = true;
      }
    }
  return true;
}

bool Library::acknowledge(const SaveInfo &save, const std::string &md5,
                          std::string &err) {
  // With optimistic=false the server has not guessed a baseline before bytes
  // were verified.
  auto r = requestJson(s, "/api/saves/" + std::to_string(save.id), ca, p);
  if (!r.error.empty()) {
    err = r.error;
    return false;
  }
  auto j = json(r.body);
  if (str(j.get(), "content_hash") != md5 ||
      str(j.get(), "updated_at") != save.updated) {
    err = "Remote changed before acknowledgement";
    return false;
  }
  auto payload = object();
  add(payload.get(), "device_id", device);
  add(payload.get(), "content_hash", md5);
  Request req;
  req.method = "POST";
  req.body = dump(payload.get());
  r = requestJson(s, "/api/saves/" + std::to_string(save.id) + "/downloaded",
                  ca, p, req);
  if (!r.error.empty()) {
    err = r.error;
    return false;
  }
  j = json(r.body);
  if (str(j.get(), "content_hash") != md5) {
    err = "Remote changed during acknowledgement";
    return false;
  }
  return true;
}
bool Library::checkLocal(const Install &i, std::string &status,
                         std::string &err) {
  if (!i.romId) {
    status = "Not installed";
    return true;
  }
  std::string md5, sha;
  if (!hashFile(i.romPath, md5, sha, err) || sha != i.romSha256) {
    status = "ROM missing or modified";
    return false;
  }
  if (i.slot.empty()) {
    status = "Installed - choose a save slot";
    return true;
  }
  std::string path;
  if (!saveLocation(i, path, err))
    return false;
  if (path != i.savePath) {
    status = "open_agb_firm slot changed - relink saves";
    return true;
  }
  if (!recoverReplacement(path, err))
    return false;
  if (!fileExists(path)) {
    status = "Local save missing";
    return true;
  }
  if (!hashFile(path, md5, sha, err))
    return false;
  status = i.phase == "upload-unconfirmed" ? "Upload unconfirmed - refresh sync"
           : i.localBase.empty()           ? "Local save not synced"
           : md5 == i.localBase ? "Local save unchanged"
                                : "Local save changed - ready to sync";
  return true;
}
bool Library::sync(Install &i, ConflictChoice choice,
                   const std::string &expectedLocal,
                   const std::string &expectedRemote, SyncResult &result,
                   std::string &err) {
  const auto chosenLocal = expectedLocal, chosenRemote = expectedRemote;
  result = SyncResult{};
  if (!validInstall(i) || i.slot.empty() || i.saveType == SaveType::Unknown ||
      i.remoteEncoding == SaveEncoding::Unknown) {
    err = "Install ROM and link a verified save slot first";
    return false;
  }
  std::string md5, romSha;
  if (!hashFile(i.romPath, md5, romSha, err) || romSha != i.romSha256) {
    err = "Installed ROM changed; sync stopped";
    return false;
  }
  std::string local;
  if (!saveLocation(i, local, err) || local != i.savePath) {
    err = "open_agb_firm save path changed; relink before syncing";
    return false;
  }
  if (!makeDirectories(parent(local), err) || !recoverReplacement(local, err))
    return false;
  if (freeBytes(parent(local)) < 2 * 1024 * 1024) {
    err = "Not enough SD space for save backups";
    return false;
  }
  SaveInfo remote{};
  bool hasRemote;
  if (!currentSave(i, remote, hasRemote, err))
    return false;
  if (hasRemote && (remote.emulator != i.emulator ||
                    remote.emulatorNull != i.emulatorNull)) {
    err = "Remote save producer changed; verify its format and relink";
    return false;
  }
  bool hasLocal = fileExists(local);
  std::string localMd5, localSha;
  if (hasLocal && !hashFile(local, localMd5, localSha, err))
    return false;
  std::string remoteRaw, remoteSha, remoteCanonical;
  std::vector<uint8_t> remoteLocalBytes;
  const auto incoming = local + ".rs-incoming",
             converted = local + ".rs-converted",
             outgoing = local + ".rs-outgoing";
  std::remove(incoming.c_str());
  std::remove(converted.c_str());
  std::remove(outgoing.c_str());
  if (hasRemote) {
    if (!hash(remote.hash, 32)) {
      err = "Server save lacks a verified MD5; refusing automatic sync";
      return false;
    }
    if (!downloadFile(s,
                      "/api/saves/" + std::to_string(remote.id) +
                          "/content?optimistic=false",
                      incoming, 131072, remote.size, ca, p, err))
      return false;
    if (!hashFile(incoming, remoteRaw, remoteSha, err) ||
        remoteRaw != remote.hash) {
      std::remove(incoming.c_str());
      err = "Remote save bytes changed; retry sync";
      return false;
    }
    std::string bytes;
    if (!readFile(incoming, bytes, 131072, err)) {
      std::remove(incoming.c_str());
      return false;
    }
    std::vector<uint8_t> raw(bytes.begin(), bytes.end());
    if (!convertSave(raw, i.saveType, i.remoteEncoding,
                     SaveEncoding::OpenAgbFirm, remoteLocalBytes, err)) {
      std::remove(incoming.c_str());
      return false;
    }
    if (!writeBytes(converted, remoteLocalBytes, err)) {
      std::remove(incoming.c_str());
      return false;
    }
    std::string ignored;
    if (!hashFile(converted, remoteCanonical, ignored, err))
      return false;
  }
  // Keep downloaded originals as content-addressed evidence even when the user
  // cancels a conflict.
  if (hasRemote) {
    const auto original = account + "/remote-" + remoteRaw + ".sav";
    if (!fileExists(original)) {
      if (!copyFile(incoming, original, err))
        return false;
    }
    std::remove(incoming.c_str());
  }
  if (i.phase == "upload-unconfirmed") {
    auto r = requestJson(s,
                         "/api/saves?rom_id=" + std::to_string(i.romId) +
                             "&slot=" + urlEncode(i.slot),
                         ca, p);
    std::vector<SaveInfo> rows;
    if (!r.error.empty() || !parseSaves(r.body, rows, err)) {
      err = r.error.empty() ? err : r.error;
      return false;
    }
    bool found = false;
    for (const auto &row : rows)
      if (row.slot == i.slot && !row.archival && row.emulator == i.emulator &&
          row.emulatorNull == i.emulatorNull && row.hash == i.pendingRemote)
        found = true;
    if (!found) {
      err =
          "Previous upload outcome unknown; check RomM history before retrying";
      return false;
    }
    i.localBase = i.pendingLocal;
    i.remoteBase = i.pendingRemote;
    i.pendingLocal.clear();
    i.pendingRemote.clear();
    i.phase = "installed";
    if (!put(i, err))
      return false;
  }
  const Copy l{hasLocal, localMd5}, r{hasRemote, remoteCanonical},
      baseline{!i.localBase.empty(), i.localBase};
  auto decision = reconcile(l, r, baseline, true);
  result.localHash = localMd5;
  result.remoteHash = remoteRaw;
  result.remote = remote;
  if (hasLocal) {
    struct stat st{};
    if (stat(local.c_str(), &st) == 0)
      result.localSize = st.st_size;
  }
  result.remoteSize = remote.size;
  if (choice != ConflictChoice::Ask &&
      (chosenLocal != localMd5 || chosenRemote != remoteRaw))
    decision = Decision::Conflict;
  if (decision == Decision::Conflict) {
    if (choice == ConflictChoice::Ask || chosenLocal != localMd5 ||
        chosenRemote != remoteRaw) {
      result.conflict = true;
      result.message = "Both copies differ. Choose which to keep.";
      return true;
    }
    decision = choice == ConflictChoice::KeepLocal ? Decision::Upload
                                                   : Decision::Download;
  }
  if (decision == Decision::LocalDeleted ||
      decision == Decision::RemoteDeleted ||
      decision == Decision::BothDeleted) {
    err = "A tracked save is missing; no deletion propagated. Relink "
          "explicitly to restore.";
    return false;
  }
  if (decision == Decision::Unsupported) {
    err = "Save identity or format unknown; originals preserved";
    return false;
  }
  if (p.cancel.load()) {
    err = "Sync cancelled; originals preserved";
    return false;
  }
  if (!hasLocal && !hasRemote) {
    result.message = "Play the game once to create a local save";
    return true;
  }
  if (!deviceReady(err))
    return false;
  // Re-read the exact active slot after user interaction/transfer/conversion
  // and before server bookkeeping.
  SaveInfo fresh{};
  bool exists;
  if (!currentSave(i, fresh, exists, err))
    return false;
  if (exists != hasRemote ||
      (exists && (fresh.id != remote.id || fresh.hash != remoteRaw ||
                  fresh.updated != remote.updated))) {
    err = "Remote save changed during sync; retry to review";
    return false;
  }
  if (hasLocal) {
    std::string check, sha;
    if (!hashFile(local, check, sha, err) || sha != localSha) {
      err = "Local save changed during sync; retry";
      return false;
    }
  }
  if (decision == Decision::Unchanged) {
    if (hasRemote && !acknowledge(remote, remoteRaw, err))
      return false;
    i.localBase = localMd5;
    i.remoteBase = remoteRaw;
    i.remoteId = hasRemote ? remote.id : 0;
    i.phase = "installed";
    i.pendingLocal.clear();
    i.pendingRemote.clear();
    std::remove(converted.c_str());
    if (!put(i, err))
      return false;
    result.message = "Saves are up to date";
    return true;
  }
  if (decision == Decision::Download) {
    if (!hasRemote) {
      err = "Remote save missing";
      return false;
    }
    if (!replaceFile(converted, local, localSha, err))
      return false;
    // Keep the old manifest baseline until the server confirms acknowledgement.
    if (!acknowledge(remote, remoteRaw, err))
      return false;
    i.localBase = remoteCanonical;
    i.remoteBase = remoteRaw;
    i.remoteId = remote.id;
    i.phase = "installed";
    i.pendingLocal.clear();
    i.pendingRemote.clear();
    if (!put(i, err))
      return false;
    result.message = "Remote save installed; previous local copy backed up";
    return true;
  }
  if (decision == Decision::Upload) {
    if (!hasLocal) {
      err = "Local save missing";
      return false;
    }
    std::string bytes;
    if (!readFile(local, bytes, 131072, err))
      return false;
    std::vector<uint8_t> raw(bytes.begin(), bytes.end()), serverBytes;
    if (!convertSave(raw, i.saveType, SaveEncoding::OpenAgbFirm,
                     i.remoteEncoding, serverBytes, err) ||
        !writeBytes(outgoing, serverBytes, err))
      return false;
    std::string uploadMd5, sha;
    if (!hashFile(outgoing, uploadMd5, sha, err))
      return false;
    // Acknowledge only bytes actually downloaded and preserved. The server then
    // guards against newer versions.
    if (hasRemote && !acknowledge(remote, remoteRaw, err))
      return false;
    i.phase = "upload-unconfirmed";
    i.pendingLocal = localMd5;
    i.pendingRemote = uploadMd5;
    if (!put(i, err))
      return false;
    if (p.cancel.load()) {
      i.phase = "installed";
      i.pendingLocal.clear();
      i.pendingRemote.clear();
      put(i, err);
      err = "Upload cancelled before send";
      return false;
    }
    Request req;
    req.method = "POST";
    req.uploadFile = outgoing;
    const auto path =
        "/api/saves?rom_id=" + std::to_string(i.romId) +
        "&slot=" + urlEncode(i.slot) +
        (i.emulatorNull ? "" : "&emulator=" + urlEncode(i.emulator)) +
        "&device_id=" + device + "&content_hash=" + uploadMd5 +
        "&overwrite=false&autocleanup=false";
    auto response = requestJson(s, path, ca, p, req);
    if (!response.error.empty()) {
      if (response.status == 401 || response.status == 403 ||
          response.status == 409 || response.status == 422) {
        i.phase = "installed";
        i.pendingLocal.clear();
        i.pendingRemote.clear();
        std::string ignored;
        put(i, ignored);
      }
      err = response.error;
      return false;
    }
    auto responseJson = json(response.body);
    SaveInfo uploaded{};
    if (!parseOne(responseJson.get(), uploaded, err) ||
        uploaded.hash != uploadMd5 || uploaded.slot != i.slot ||
        uploaded.emulator != i.emulator ||
        uploaded.emulatorNull != i.emulatorNull || uploaded.romId != i.romId ||
        uploaded.userId != user) {
      err = "Upload response unconfirmed; refresh history before any retry";
      return false;
    }
    // The result is not committed to the local baseline until a read proves
    // persistence.
    auto verify =
        requestJson(s, "/api/saves/" + std::to_string(uploaded.id), ca, p);
    if (!verify.error.empty()) {
      err = "Upload sent; confirmation pending";
      return false;
    }
    auto v = json(verify.body);
    if (str(v.get(), "content_hash") != uploadMd5) {
      err = "Uploaded save changed before confirmation";
      return false;
    }
    i.localBase = localMd5;
    i.remoteBase = uploadMd5;
    i.remoteId = uploaded.id;
    i.phase = "installed";
    i.pendingLocal.clear();
    i.pendingRemote.clear();
    if (!put(i, err))
      return false;
    std::remove(outgoing.c_str());
    std::remove(converted.c_str());
    result.message = "Local save uploaded; RomM history preserved";
    return true;
  }
  err = "Unsupported sync decision";
  return false;
}
} // namespace rs
