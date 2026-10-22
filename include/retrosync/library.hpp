#pragma once
#include "storage.hpp"
namespace rs {
struct Install {
  int romId = 0, platformId = 0, remoteId = 0;
  std::string title, romPath, romSha256;
  SaveType saveType = SaveType::Unknown;
  SaveEncoding remoteEncoding = SaveEncoding::Unknown;
  std::string slot, emulator, savePath, localBase, remoteBase;
  bool emulatorNull = true;
  std::string phase = "installed", pendingLocal, pendingRemote;
};
enum class ConflictChoice { Ask, KeepLocal, KeepRemote };
struct SyncResult {
  bool conflict = false;
  std::string message, localHash, remoteHash;
  uint64_t localSize = 0, remoteSize = 0;
  SaveInfo remote{};
};
class Library {
public:
  Library(Settings &settings, const std::string &ca, Progress &progress,
          const std::string &root = "sdmc:/3ds/RetroSync3DS");
  bool open(std::string &error);
  bool load(int romId, Install &install, std::string &error);
  bool download(const Rom &rom, const Platform &platform, Install &install,
                std::string &error);
  bool bind(Install &install, const SaveInfo &selected, SaveType type,
            SaveEncoding encoding, std::string &error);
  bool bindNew(Install &install, const std::string &slot, SaveType type,
               std::string &error, SaveEncoding encoding=SaveEncoding::OpenAgbFirm);
  bool sync(Install &install, ConflictChoice choice,
            const std::string &expectedLocal, const std::string &expectedRemote,
            SyncResult &result, std::string &error);
  bool checkLocal(const Install &install, std::string &status,
                  std::string &error);

private:
  Settings &s;
  std::string ca, root, account, key, device, version;
  Progress &p;
  int user = 0;
  bool put(const Install &install, std::string &error);
  bool deviceReady(std::string &error);
  bool saveLocation(const Install &install, std::string &path,
                    std::string &error);
  bool currentSave(const Install &install, SaveInfo &save, bool &exists,
                   std::string &error);
  bool acknowledge(const SaveInfo &save, const std::string &hash,
                   std::string &error);
};
} // namespace rs
