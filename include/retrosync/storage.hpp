#pragma once
#include "api.hpp"
namespace rs {
bool makeDirectories(const std::string &path, std::string &error);
bool fileExists(const std::string &path);
bool readFile(const std::string &path, std::string &data, size_t maximum,
              std::string &error);
bool writeFile(const std::string &path, const std::string &data,
               std::string &error);
bool hashFile(const std::string &path, std::string &md5, std::string &sha256,
              std::string &error);
std::string sha256Text(const std::string &text);
bool copyFile(const std::string &from, const std::string &to,
              std::string &error);
bool recoverReplacement(const std::string &destination, std::string &error);
bool replaceFile(const std::string &staged, const std::string &destination,
                 const std::string &expectedOldSha256, std::string &error);
bool extractGba(const std::string &archive, const std::string &output,
                Progress &progress, std::string &error);
bool inspectGba(const std::string &path, SaveType &type, std::string &error);
uint64_t freeBytes(const std::string &directory);
} // namespace rs
