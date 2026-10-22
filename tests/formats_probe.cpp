#include "retrosync/library.hpp"
#include <cstdlib>
#include <curl/curl.h>
#include <fstream>
#include <iostream>
using namespace rs;
void check(bool ok, const std::string &msg) {
  if (!ok) {
    std::cerr << "FAIL " << msg << std::endl;
    std::exit(1);
  }
}
int main(int argc, char **argv) {
  if (argc != 4)
    return 2;
  curl_global_init(CURL_GLOBAL_DEFAULT);
  Settings s;
  s.server = argv[1];
  s.allowHttp = true;
  Progress p;
  std::string e;
  auto type = static_cast<SaveType>(std::atoi(argv[2]));
  size_t size = std::atoi(argv[3]);
  check(makeDirectories("sdmc:/3ds/RetroSync3DS", e), e);
  check(signIn(s, "demo", "correct +&?", "", p, e), e);
  Library lib(s, "", p);
  check(lib.open(e), e);
  Install i;
  Rom rom{100, 1, "Format fixture", "Fixture.zip", "", 32768};
  Platform platform{1, "GBA", "gba"};
  check(lib.download(rom, platform, i, e), e);
  auto rows = requestJson(s, "/api/saves?rom_id=100", "", p);
  std::vector<SaveInfo> saves;
  check(parseSaves(rows.body, saves, e) && saves.size() == 1, e);
  check(lib.bind(i, saves[0], type, SaveEncoding::RawEmulator, e), e);
  SyncResult result;
  check(lib.sync(i, ConflictChoice::Ask, "", "", result, e) && !result.conflict,
        e);
  std::string bytes;
  check(readFile(i.savePath, bytes, 131072, e) && bytes.size() == size,
        "battery length");
  std::vector<uint8_t> server(size);
  for (size_t k = 0; k < size; ++k)
    server[k] = static_cast<uint8_t>(k % 251);
  std::vector<uint8_t> expected;
  check(convertSave(server, type, SaveEncoding::RawEmulator,
                    SaveEncoding::OpenAgbFirm, expected, e),
        e);
  check(std::vector<uint8_t>(bytes.begin(), bytes.end()) == expected,
        "download canonical conversion");
  expected[0] ^= 0x5a;
  {
    std::ofstream f(i.savePath, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char *>(expected.data()), expected.size());
  }
  check(lib.sync(i, ConflictChoice::Ask, "", "", result, e) && !result.conflict,
        e);
  rows = requestJson(s, "/api/saves?rom_id=100", "", p);
  check(parseSaves(rows.body, saves, e) && saves.size() == 2, "append history");
  auto last = saves.back();
  std::string output = "sdmc:/downloaded.sav";
  check(downloadFile(s,
                     "/api/saves/" + std::to_string(last.id) +
                         "/content?optimistic=false",
                     output, 131072, size, "", p, e),
        e);
  check(readFile(output, bytes, 131072, e), e);
  std::vector<uint8_t> expectedServer;
  check(convertSave(expected, type, SaveEncoding::OpenAgbFirm,
                    SaveEncoding::RawEmulator, expectedServer, e),
        e);
  check(std::vector<uint8_t>(bytes.begin(), bytes.end()) == expectedServer,
        "upload producer conversion");
  check(lib.bindNew(i, "new-slot", type, e, SaveEncoding::RawEmulator), e);
  check(i.emulator == "mgba" && i.remoteEncoding == SaveEncoding::RawEmulator,
        "new EmulatorJS link uses an explicit raw producer");
  std::cout << "PASS save-format HTTP round trip: type "
            << static_cast<int>(type) << ", " << size << " bytes" << std::endl;
  curl_global_cleanup();
}
