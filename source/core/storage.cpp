#include "retrosync/storage.hpp"
#include "cJSON.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <zlib.h>
#ifdef __3DS__
#include <mbedtls/md.h>
#else
#include <openssl/evp.h>
#endif
namespace rs {
namespace {
struct Digests {
#ifdef __3DS__
  mbedtls_md_context_t m, s;
  Digests() {
    mbedtls_md_init(&m);
    mbedtls_md_init(&s);
    mbedtls_md_setup(&m, mbedtls_md_info_from_type(MBEDTLS_MD_MD5), 0);
    mbedtls_md_setup(&s, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
    mbedtls_md_starts(&m);
    mbedtls_md_starts(&s);
  }
  ~Digests() {
    mbedtls_md_free(&m);
    mbedtls_md_free(&s);
  }
  void add(const void *p, size_t n) {
    mbedtls_md_update(&m, static_cast<const unsigned char *>(p), n);
    mbedtls_md_update(&s, static_cast<const unsigned char *>(p), n);
  }
  void end(unsigned char *a, unsigned char *b) {
    mbedtls_md_finish(&m, a);
    mbedtls_md_finish(&s, b);
  }
#else
  EVP_MD_CTX *m = EVP_MD_CTX_new(), *s = EVP_MD_CTX_new();
  Digests() {
    EVP_DigestInit_ex(m, EVP_md5(), nullptr);
    EVP_DigestInit_ex(s, EVP_sha256(), nullptr);
  }
  ~Digests() {
    EVP_MD_CTX_free(m);
    EVP_MD_CTX_free(s);
  }
  void add(const void *p, size_t n) {
    EVP_DigestUpdate(m, p, n);
    EVP_DigestUpdate(s, p, n);
  }
  void end(unsigned char *a, unsigned char *b) {
    unsigned n;
    EVP_DigestFinal_ex(m, a, &n);
    EVP_DigestFinal_ex(s, b, &n);
  }
#endif
};
std::string hex(const unsigned char *p, size_t n) {
  static const char *h = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < n; ++i) {
    s += h[p[i] >> 4];
    s += h[p[i] & 15];
  }
  return s;
}
bool flushClose(FILE *f) {
  bool ok = std::fflush(f) == 0;
  if (fsync(fileno(f)) != 0)
    ok = false;
  if (std::fclose(f) != 0)
    ok = false;
  return ok;
}
std::string jsonString(const cJSON *j, const char *k) {
  auto *f = cJSON_GetObjectItemCaseSensitive(j, k);
  return cJSON_IsString(f) ? f->valuestring : "";
}
bool sameHash(const std::string &p, const std::string &expected) {
  std::string m, s, e;
  return !expected.empty() && hashFile(p, m, s, e) && s == expected;
}
} // namespace
bool fileExists(const std::string &p) {
  struct stat s{};
  return stat(p.c_str(), &s) == 0 && S_ISREG(s.st_mode);
}
bool makeDirectories(const std::string &p, std::string &err) {
  err.clear();
  if (p.empty() || p.find("..") != std::string::npos ||
      p.find('\\') != std::string::npos) {
    err = "Unsafe storage directory";
    return false;
  }
  for (size_t i = 1; i <= p.size(); ++i)
    if (i == p.size() || p[i] == '/') {
      std::string part = p.substr(0, i);
      if (part == "sdmc:")
        continue;
      if (mkdir(part.c_str(), 0777) != 0 && errno != EEXIST) {
        err = "Cannot create SD directory";
        return false;
      }
    }
  return true;
}
bool readFile(const std::string &p, std::string &data, size_t maximum,
              std::string &err) {
  err.clear();
  FILE *f = std::fopen(p.c_str(), "rb");
  if (!f) {
    err = "File missing or unreadable";
    return false;
  }
  data.clear();
  std::array<char, 4096> b{};
  size_t n;
  bool ok = true;
  while ((n = std::fread(b.data(), 1, b.size(), f))) {
    if (n > maximum - data.size()) {
      ok = false;
      break;
    }
    data.append(b.data(), n);
  }
  if (std::ferror(f))
    ok = false;
  std::fclose(f);
  if (!ok) {
    data.clear();
    err = "File unreadable or exceeds size limit";
  }
  return ok;
}
bool hashFile(const std::string &p, std::string &md5, std::string &sha,
              std::string &err) {
  err.clear();
  FILE *f = std::fopen(p.c_str(), "rb");
  if (!f) {
    err = "Cannot hash missing file";
    return false;
  }
  Digests d;
  std::array<unsigned char, 32768> b{};
  size_t n;
  while ((n = std::fread(b.data(), 1, b.size(), f)))
    d.add(b.data(), n);
  bool ok = !std::ferror(f);
  std::fclose(f);
  if (!ok) {
    err = "Cannot read SD file";
    return false;
  }
  unsigned char m[16], s[32];
  d.end(m, s);
  md5 = hex(m, 16);
  sha = hex(s, 32);
  return true;
}
std::string sha256Text(const std::string &text) {
  Digests d;
  d.add(text.data(), text.size());
  unsigned char m[16], s[32];
  d.end(m, s);
  return hex(s, 32);
}
bool copyFile(const std::string &from, const std::string &to,
              std::string &err) {
  err.clear();
  FILE *a = std::fopen(from.c_str(), "rb");
  if (!a) {
    err = "Cannot read backup source";
    return false;
  }
  FILE *b = std::fopen(to.c_str(), "wbx");
  if (!b) {
    std::fclose(a);
    err = "Cannot create backup";
    return false;
  }
  std::array<char, 32768> data{};
  size_t n;
  bool ok = true;
  while ((n = std::fread(data.data(), 1, data.size(), a)))
    if (std::fwrite(data.data(), 1, n, b) != n) {
      ok = false;
      break;
    }
  if (std::ferror(a))
    ok = false;
  std::fclose(a);
  if (!flushClose(b))
    ok = false;
  if (!ok) {
    std::remove(to.c_str());
    err = "Backup failed; original preserved";
  }
  return ok;
}
bool writeFile(const std::string &p, const std::string &data,
               std::string &err) {
  err.clear();
  // Metadata backup survives libctru's non-atomic overwrite rename.
  const auto tmp = p + ".tmp", bak = p + ".bak";
  FILE *f = std::fopen(tmp.c_str(), "wb");
  if (!f) {
    err = "Cannot stage metadata";
    return false;
  }
  bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
  if (!flushClose(f))
    ok = false;
  if (!ok) {
    err = "Cannot flush metadata";
    return false;
  }
  if (fileExists(p)) {
    // Do not replace a good recovery backup with an unreadable primary
    // document.
    std::string prior, readError;
    cJSON *priorJson = nullptr;
    if (readFile(p, prior, 16384, readError))
      priorJson = cJSON_ParseWithLengthOpts(prior.c_str(), prior.size() + 1,
                                            nullptr, 1);
    const bool preserveBackup = !priorJson && fileExists(bak);
    cJSON_Delete(priorJson);
    std::string m, s, e;
    if (!hashFile(p, m, s, e)) {
      err = e;
      return false;
    }
    if (!preserveBackup && !sameHash(bak, s)) {
      std::remove(bak.c_str());
      if (!copyFile(p, bak, err))
        return false;
    }
  }
  if (std::rename(tmp.c_str(), p.c_str()) != 0) {
    err = "Cannot commit metadata; backup retained";
    return false;
  }
  return true;
}
uint64_t freeBytes(const std::string &p) {
  struct statvfs s{};
  return statvfs(p.c_str(), &s) == 0
             ? static_cast<uint64_t>(s.f_bavail) * s.f_frsize
             : 0;
}
bool recoverReplacement(const std::string &dest, std::string &err) {
  err.clear();
  const auto journal = dest + ".rs-journal";
  if (!fileExists(journal) && !fileExists(journal + ".bak"))
    return true;
  std::string raw;
  if (!readFile(journal, raw, 8192, err) &&
      !readFile(journal + ".bak", raw, 8192, err))
    return false;
  cJSON *j = cJSON_ParseWithLength(raw.c_str(), raw.size());
  const auto old = jsonString(j, "old"), next = jsonString(j, "next"),
             backup = jsonString(j, "backup");
  cJSON_Delete(j);
  if (next.size() != 64 ||
      next.find_first_not_of("0123456789abcdef") != std::string::npos ||
      backup != dest + ".rs-backup-" + old ||
      (!old.empty() &&
       (old.size() != 64 ||
        old.find_first_not_of("0123456789abcdef") != std::string::npos))) {
    err = "Invalid recovery journal; files preserved";
    return false;
  }
  if (sameHash(dest, next) || (!old.empty() && sameHash(dest, old)) ||
      (!fileExists(dest) && old.empty())) {
    std::remove(journal.c_str());
    std::remove((journal + ".bak").c_str());
    return true;
  }
  if (!fileExists(dest) && !old.empty() && sameHash(backup, old)) {
    if (!copyFile(backup, dest, err))
      return false;
    std::remove(journal.c_str());
    std::remove((journal + ".bak").c_str());
    return true;
  }
  err = "Interrupted replacement needs review; backup preserved";
  return false;
}
bool replaceFile(const std::string &src, const std::string &dest,
                 const std::string &old, std::string &err) {
  err.clear();
  if (src == dest || !recoverReplacement(dest, err))
    return false;
  std::string md5, next;
  if (!hashFile(src, md5, next, err))
    return false;
  if ((old.empty() && fileExists(dest)) ||
      (!old.empty() && !sameHash(dest, old))) {
    err = "Local file changed; retry sync";
    return false;
  }
  if (next == old) {
    std::remove(src.c_str());
    return true;
  }
  const auto backup = dest + ".rs-backup-" + old;
  if (!old.empty() && !sameHash(backup, old) && !copyFile(dest, backup, err))
    return false;
  cJSON *j = cJSON_CreateObject();
  cJSON_AddStringToObject(j, "old", old.c_str());
  cJSON_AddStringToObject(j, "next", next.c_str());
  cJSON_AddStringToObject(j, "backup", backup.c_str());
  char *json = cJSON_PrintUnformatted(j);
  bool ok = json && writeFile(dest + ".rs-journal", json, err);
  cJSON_free(json);
  cJSON_Delete(j);
  if (!ok)
    return false;
  // Recheck after backup/journal I/O. Never replace a save changed by another
  // process.
  if ((old.empty() && fileExists(dest)) ||
      (!old.empty() && !sameHash(dest, old))) {
    err = "Local file changed during backup";
    return false;
  }
  if (std::rename(src.c_str(), dest.c_str()) != 0) {
    err = "Replacement interrupted; backup retained";
    return false;
  }
  if (!sameHash(dest, next)) {
    err = "Installed file failed verification; backup retained";
    return false;
  }
  std::remove((dest + ".rs-journal").c_str());
  std::remove((dest + ".rs-journal.bak").c_str());
  return true;
}
namespace {
uint16_t u16(const unsigned char *p) { return p[0] | (p[1] << 8); }
uint32_t u32(const unsigned char *p) {
  return static_cast<uint32_t>(u16(p)) |
         (static_cast<uint32_t>(u16(p + 2)) << 16);
}
bool at(FILE *f, uint32_t pos, void *p, size_t n) {
  return std::fseek(f, static_cast<long>(pos), SEEK_SET) == 0 &&
         std::fread(p, 1, n, f) == n;
}
} // namespace
bool extractGba(const std::string &archive, const std::string &out, Progress &p,
                std::string &err) {
  err.clear();
  FILE *f = std::fopen(archive.c_str(), "rb");
  if (!f) {
    err = "Archive missing";
    return false;
  }
  std::fseek(f, 0, SEEK_END);
  long length = std::ftell(f);
  if (length < 22 || length > 64 * 1024 * 1024) {
    std::fclose(f);
    err = "Unsupported ZIP size";
    return false;
  }
  size_t tailSize = std::min<long>(length, 65557);
  std::vector<unsigned char> tail(tailSize);
  at(f, length - tailSize, tail.data(), tailSize);
  long end = -1;
  for (long i = static_cast<long>(tailSize) - 22; i >= 0; --i)
    if (u32(tail.data() + i) == 0x06054b50 &&
        i + 22 + u16(tail.data() + i + 20) == static_cast<long>(tailSize)) {
      end = i;
      break;
    }
  bool ok = end >= 0;
  unsigned count = 0;
  uint32_t dir = 0, dirEnd = 0;
  if (ok) {
    auto *e = tail.data() + end;
    count = u16(e + 10);
    dir = u32(e + 16);
    dirEnd = dir + u32(e + 12);
    ok = u16(e + 4) == 0 && u16(e + 6) == 0 && count == u16(e + 8) &&
         count <= 256 && dirEnd >= dir &&
         dirEnd <= static_cast<uint32_t>(length - tailSize + end);
  }
  uint32_t compressed = 0, expanded = 0, offset = 0, crc = 0;
  unsigned method = 0;
  std::string chosen;
  unsigned candidates = 0;
  for (unsigned i = 0; ok && i < count; ++i) {
    unsigned char h[46];
    if (dir > dirEnd || dirEnd - dir < 46 || !at(f, dir, h, 46) ||
        u32(h) != 0x02014b50) {
      ok = false;
      break;
    }
    unsigned n = u16(h + 28), extra = u16(h + 30), comment = u16(h + 32);
    uint32_t advance = 46 + n + extra + comment;
    if (n == 0 || n > 255 || advance > dirEnd - dir) {
      ok = false;
      break;
    }
    std::string name(n, '\0');
    if (std::fread(&name[0], 1, n, f) != n) {
      ok = false;
      break;
    }
    if (name.find("..") != std::string::npos || name[0] == '/' ||
        name.find_first_of("\\:\r\n") != std::string::npos ||
        name.find('\0') != std::string::npos) {
      ok = false;
      break;
    }
    if ((u32(h + 38) >> 16 & 0170000) == 0120000) {
      ok = false;
      break;
    }
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (lower.size() > 4 && lower.substr(lower.size() - 4) == ".gba") {
      ++candidates;
      chosen = name;
      compressed = u32(h + 20);
      expanded = u32(h + 24);
      offset = u32(h + 42);
      crc = u32(h + 16);
      method = u16(h + 10);
      if ((u16(h + 8) & 1) || u16(h + 34) != 0 ||
          (method != 0 && method != 8) || expanded < 192 ||
          expanded > 32 * 1024 * 1024 || compressed == 0 ||
          compressed > 64 * 1024 * 1024)
        ok = false;
    }
    dir += advance;
  }
  if (candidates != 1)
    ok = false;
  unsigned char local[30];
  if (ok) {
    ok = at(f, offset, local, 30) && u32(local) == 0x04034b50 &&
         u16(local + 8) == method && !(u16(local + 6) & 1) &&
         u16(local + 26) == chosen.size();
    if (ok) {
      std::string name(chosen.size(), '\0');
      ok = std::fread(&name[0], 1, name.size(), f) == name.size() &&
           name == chosen;
      uint64_t start =
          static_cast<uint64_t>(offset) + 30 + chosen.size() + u16(local + 28);
      ok = ok && start + compressed <= static_cast<uint64_t>(length) &&
           std::fseek(f, start, SEEK_SET) == 0;
    }
  }
  FILE *dest = ok ? std::fopen(out.c_str(), "wbx") : nullptr;
  bool created = dest != nullptr;
  if (!dest)
    ok = false;
  std::array<unsigned char, 16384> input{}, output{};
  z_stream z{};
  bool inflateReady = ok && method == 8 && inflateInit2(&z, -MAX_WBITS) == Z_OK;
  if (ok && method == 8 && !inflateReady)
    ok = false;
  uint64_t written = 0;
  uint32_t remaining = compressed;
  uLong actual = crc32(0L, Z_NULL, 0);
  int code = Z_OK;
  while (ok && remaining && !p.cancel.load()) {
    size_t n = std::min<size_t>(remaining, input.size());
    if (std::fread(input.data(), 1, n, f) != n) {
      ok = false;
      break;
    }
    remaining -= n;
    if (method == 0) {
      if (written + n > expanded ||
          std::fwrite(input.data(), 1, n, dest) != n) {
        ok = false;
        break;
      }
      actual = crc32(actual, input.data(), n);
      written += n;
    } else {
      z.next_in = input.data();
      z.avail_in = n;
      do {
        z.next_out = output.data();
        z.avail_out = output.size();
        code = inflate(&z, Z_NO_FLUSH);
        size_t got = output.size() - z.avail_out;
        if ((code != Z_OK && code != Z_STREAM_END) ||
            written + got > expanded ||
            std::fwrite(output.data(), 1, got, dest) != got) {
          ok = false;
          break;
        }
        actual = crc32(actual, output.data(), got);
        written += got;
        if (code == Z_STREAM_END) {
          if (remaining || z.avail_in)
            ok = false;
          break;
        }
      } while (z.avail_in || z.avail_out == 0);
    }
  }
  if (inflateReady)
    inflateEnd(&z);
  if (dest && !flushClose(dest))
    ok = false;
  std::fclose(f);
  ok = ok && !p.cancel.load() && written == expanded && actual == crc &&
       (method == 0 || code == Z_STREAM_END);
  if (!ok) {
    if (created)
      std::remove(out.c_str());
    err = p.cancel.load()
              ? "Extraction cancelled"
              : "Unsupported, ambiguous or damaged ZIP; original preserved";
  }
  return ok;
}
bool inspectGba(const std::string &path, SaveType &type, std::string &err) {
  err.clear();
  FILE *f = std::fopen(path.c_str(), "rb");
  if (!f) {
    err = "Installed ROM missing";
    return false;
  }
  std::fseek(f, 0, SEEK_END);
  long n = std::ftell(f);
  std::rewind(f);
  unsigned char h[192];
  bool ok = n >= 192 && n <= 32 * 1024 * 1024 &&
            std::fread(h, 1, 192, f) == 192 && h[0xb2] == 0x96;
  unsigned check = 0;
  for (unsigned i = 0xa0; i <= 0xbc && ok; ++i)
    check += h[i];
  if (ok)
    ok = static_cast<unsigned char>(0 - check - 0x19) == h[0xbd];
  type = SaveType::Unknown;
  std::rewind(f);
  std::array<char, 32768> data{};
  std::string carry;
  bool sram = false, flash = false, flash1 = false, eeprom = false;
  size_t got;
  while (ok && (got = std::fread(data.data(), 1, data.size(), f))) {
    std::string chunk = carry + std::string(data.data(), got);
    sram |= chunk.find("SRAM_V") != std::string::npos ||
            chunk.find("SRAM_F_V") != std::string::npos;
    flash |= chunk.find("FLASH_V") != std::string::npos ||
             chunk.find("FLASH512_V") != std::string::npos;
    flash1 |= chunk.find("FLASH1M_V") != std::string::npos;
    eeprom |= chunk.find("EEPROM_V") != std::string::npos;
    carry = chunk.substr(chunk.size() > 16 ? chunk.size() - 16 : 0);
  }
  if (std::ferror(f))
    ok = false;
  std::fclose(f);
  if (!ok) {
    err = "Invalid GBA header or ROM size";
    return false;
  }
  if (static_cast<int>(sram) + flash + flash1 + eeprom == 1) {
    if (sram)
      type = SaveType::Sram32K;
    if (flash)
      type = SaveType::Flash64K;
    if (flash1)
      type = SaveType::Flash128K;
  }
  return true;
}
} // namespace rs
