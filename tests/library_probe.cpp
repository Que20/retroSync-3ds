#include "retrosync/library.hpp"
#include <cstdio>
#include <cstdlib>
#include <curl/curl.h>
#include <fstream>
#include <iostream>
using namespace rs;
unsigned checks = 0;
void check(bool yes, const std::string &message) {
  ++checks;
  if (!yes) {
    std::cerr << "FAIL " << message << std::endl;
    std::exit(1);
  }
}
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  curl_global_init(CURL_GLOBAL_DEFAULT);
  std::string error;
  Settings s;
  s.server = argv[1];
  s.allowHttp = true;
  Progress p;
  check(makeDirectories("sdmc:/3ds/RetroSync3DS", error), error);
  check(signIn(s, "demo", "correct +&?", "", p, error), "login: " + error);
  check(s.refreshToken == "refresh-1", "refresh token received");
  s.token = "expired";
  auto heartbeat = requestJson(s, "/api/heartbeat", "", p);
  check(heartbeat.error.empty(), "refresh expired access token");
  check(s.refreshToken == "refresh-2", "rotated refresh token");
  RomPage page;
  check(getRomPage(s, 1, 0, "", 20, "", p, page, error), error);
  check(page.limit == 5 && page.items.size() == 5, "adaptive 20 to 5");
  auto control = [&](const std::string &mode) {
    auto r = requestJson(s, "/api/test/" + mode, "", p);
    check(r.error.empty(), "fixture control");
  };
  control("huge");
  check(getRomPage(s, 1, 5, "", 5, "", p, page, error), error);
  check(page.limit == 1 && page.offset == 5, "adaptive 5 to 1 retains offset");
  control("normal");
  Library lib(s, "", p);
  check(lib.open(error), error);
  Rom rom{100, 1, "Fixture ROM", "Fixture.zip", "", 32768};
  Platform platform{1, "Game Boy Advance", "gba"};
  Install installed;
  check(lib.download(rom, platform, installed, error), "install: " + error);
  check(fileExists(installed.romPath), "ROM installed");
  check(installed.saveType == SaveType::Sram32K, "verified SRAM signature");
  std::string raw;
  check(readFile(installed.romPath, raw, 32768, error) && raw.size() == 32768,
        "ZIP extracted and verified");
  Install duplicate;
  check(lib.download(rom, platform, duplicate, error), "idempotent install");
  check(duplicate.romPath == installed.romPath, "stable installed naming");
  auto response = requestJson(s, "/api/saves?rom_id=100", "", p);
  std::vector<SaveInfo> saves;
  check(parseSaves(response.body, saves, error) && saves.size() == 1, error);
  check(lib.bind(installed, saves[0], SaveType::Sram32K,
                 SaveEncoding::RawEmulator, error),
        "link verified save");
  SyncResult result;
  check(lib.sync(installed, ConflictChoice::Ask, "", "", result, error),
        "initial download: " + error);
  check(!result.conflict && !installed.localBase.empty(),
        "baseline persisted after acknowledgement");
  check(readFile(installed.savePath, raw, 32768, error) &&
            raw == std::string(32768, 'R'),
        "downloaded remote battery bytes");
  auto local = [&](char c) {
    std::ofstream f(installed.savePath, std::ios::binary | std::ios::trunc);
    f << std::string(32768, c);
    f.close();
  };
  local('L');
  check(lib.sync(installed, ConflictChoice::Ask, "", "", result, error),
        "upload local changes: " + error);
  check(!result.conflict &&
            result.message.find("uploaded") != std::string::npos,
        "guarded upload completed");
  control("remote-X");
  check(lib.sync(installed, ConflictChoice::Ask, "", "", result, error),
        "download changed remote: " + error);
  check(readFile(installed.savePath, raw, 32768, error) && raw[0] == 'X',
        "remote only changes downloaded");
  local('B');
  control("remote-C");
  check(lib.sync(installed, ConflictChoice::Ask, "", "", result, error) &&
            result.conflict,
        "two changed copies conflict");
  check(readFile(installed.savePath, raw, 32768, error) && raw[0] == 'B',
        "conflict leaves original intact");
  auto oldLocal = result.localHash, oldRemote = result.remoteHash;
  local('D');
  check(lib.sync(installed, ConflictChoice::KeepRemote, oldLocal, oldRemote,
                 result, error) &&
            result.conflict,
        "stale conflict choice rejected");
  check(lib.sync(installed, ConflictChoice::KeepRemote, result.localHash,
                 result.remoteHash, result, error),
        "keep remote");
  check(readFile(installed.savePath, raw, 32768, error) && raw[0] == 'C',
        "chosen remote installed");
  local('E');
  control("remote-F");
  check(lib.sync(installed, ConflictChoice::Ask, "", "", result, error) &&
            result.conflict,
        "second conflict");
  check(lib.sync(installed, ConflictChoice::KeepLocal, result.localHash,
                 result.remoteHash, result, error),
        "keep local: " + error);
  check(readFile(installed.savePath, raw, 32768, error) && raw[0] == 'E',
        "keep local preserves bytes");
  local('G');
  control("race");
  check(!lib.sync(installed, ConflictChoice::Ask, "", "", result, error),
        "server race refused");
  check(installed.phase == "installed",
        "409 is definite rejection, no uncertain retry");
  control("normal");
  check(lib.sync(installed, ConflictChoice::Ask, "", "", result, error) &&
            result.conflict,
        "race returns as conflict");
  check(lib.sync(installed, ConflictChoice::KeepRemote, result.localHash,
                 result.remoteHash, result, error),
        "resolve race");
  local('H');
  control("lost-response");
  check(!lib.sync(installed, ConflictChoice::Ask, "", "", result, error),
        "lost upload response is not success");
  check(installed.phase == "upload-unconfirmed",
        "uncertain upload journal persisted");
  Install recovered;
  check(lib.load(100, recovered, error) &&
            recovered.phase == "upload-unconfirmed",
        "uncertainty survives restart");
  check(lib.sync(recovered, ConflictChoice::Ask, "", "", result, error),
        "recover response from server history: " + error);
  check(recovered.phase == "installed",
        "confirmed upload recovered without duplicate POST");
  installed = recovered;
  local('I');
  control("lost-no-commit");
  check(!lib.sync(installed, ConflictChoice::Ask, "", "", result, error),
        "lost upload without evidence");
  check(!lib.sync(installed, ConflictChoice::Ask, "", "", result, error) &&
            installed.phase == "upload-unconfirmed",
        "never blindly retry uncertain writes");
  control("assertions");
  std::cout << "PASS " << checks << " library integration checks" << std::endl;
  curl_global_cleanup();
}
