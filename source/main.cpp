#include "retrosync/library.hpp"
#include "retrosync/ui.hpp"
#include <3ds.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <curl/curl.h>
#include <malloc.h>
#include <memory>
#include <set>
namespace {
const char *SettingsPath = "sdmc:/3ds/RetroSync3DS/settings.ini";
enum class Screen { Platforms, Games, Detail, Saves, Settings, Link, Conflict };
enum class Op { Connect, Login, Page, Info, Download, History, Bind, Sync };
struct Job {
  Op op = Op::Connect;
  rs::Settings settings;
  rs::Progress progress;
  std::atomic<bool> done{false};
  std::string error, status, ca, username, password, filter, version,
      expectedLocal, expectedRemote, slot;
  int platform = 0;
  unsigned offset = 0, limit = rs::PageSize;
  std::vector<rs::Platform> platforms;
  std::vector<rs::SaveInfo> saves;
  rs::RomPage page;
  rs::Rom rom{};
  rs::Platform selectedPlatform{};
  rs::Install installed;
  rs::SaveInfo selectedSave{};
  rs::SaveType type = rs::SaveType::Unknown;
  rs::SaveEncoding encoding = rs::SaveEncoding::Unknown;
  rs::ConflictChoice choice = rs::ConflictChoice::Ask;
  rs::SyncResult sync;
  bool newSlot = false;
  std::set<int> onSd;
};
void run(void *arg) {
  auto &j = *static_cast<Job *>(arg);
  if (j.op == Op::Login) {
    bool ok = rs::signIn(j.settings, j.username, j.password, j.ca, j.progress,
                         j.error);
    std::fill(j.password.begin(), j.password.end(), '\0');
    j.password.clear();
    if (!ok) {
      j.done.store(true, std::memory_order_release);
      return;
    }
  }
  if (j.op == Op::Connect || j.op == Op::Login) {
    auto r = rs::requestJson(j.settings, "/api/heartbeat", j.ca, j.progress);
    j.error = r.error;
    if (j.error.empty() && rs::parseHeartbeat(r.body, j.version, j.error)) {
      r = rs::requestJson(j.settings, "/api/platforms", j.ca, j.progress);
      j.error = r.error;
      if (j.error.empty())
        rs::parsePlatforms(r.body, j.platforms, j.error);
    }
    j.status = "Library connected. Choose a platform.";
  } else if (j.op == Op::Page) {
    rs::getRomPage(j.settings, j.platform, j.offset, j.filter, j.limit, j.ca,
                   j.progress, j.page, j.error);
    if (j.error.empty()) {
      rs::Library lib(j.settings, j.ca, j.progress);
      std::string ignored;
      if (lib.open(ignored))
        for (const auto &rom : j.page.items) {
          rs::Install i;
          if (lib.load(rom.id, i, ignored) && i.romId &&
              rs::fileExists(i.romPath))
            j.onSd.insert(rom.id);
        }
    }
    j.status = "A: game details. X: remote saves.";
  } else {
    rs::Library lib(j.settings, j.ca, j.progress);
    if (j.op == Op::History) {
      auto r = rs::requestJson(j.settings,
                               "/api/saves?rom_id=" + std::to_string(j.rom.id),
                               j.ca, j.progress);
      j.error = r.error;
      if (j.error.empty())
        rs::parseSaves(r.body, j.saves, j.error);
      std::string ignored;
      if (j.error.empty() && lib.open(ignored))
        lib.load(j.rom.id, j.installed, ignored);
      j.status = "Choose a named slot to link its current version.";
    } else if (lib.open(j.error) && lib.load(j.rom.id, j.installed, j.error)) {
      if (j.op == Op::Info)
        lib.checkLocal(j.installed, j.status, j.error);
      else if (j.op == Op::Download) {
        if (lib.download(j.rom, j.selectedPlatform, j.installed, j.error))
          j.status = "Game installed and verified. Open it in open_agb_firm.";
      } else {
        bool ready = true;
        if (j.op == Op::Bind) {
          ready = j.newSlot ? lib.bindNew(j.installed, j.slot, j.type, j.error,
                                          j.encoding)
                            : lib.bind(j.installed, j.selectedSave, j.type,
                                       j.encoding, j.error);
        }
        if (ready && lib.sync(j.installed, j.choice, j.expectedLocal,
                              j.expectedRemote, j.sync, j.error))
          j.status = j.sync.message;
      }
    }
  }
  j.done.store(true, std::memory_order_release);
}
bool keyboard(std::string &value, const char *hint, bool secret = false) {
  SwkbdState kb;
  swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, 512);
  swkbdSetHintText(&kb, hint);
  if (secret)
    swkbdSetPasswordMode(&kb, SWKBD_PASSWORD_HIDE);
  else
    swkbdSetInitialText(&kb, value.c_str());
  char text[513] = {0};
  bool ok = swkbdInputText(&kb, text, sizeof(text)) == SWKBD_BUTTON_RIGHT;
  if (ok)
    value = text;
  std::memset(text, 0, sizeof(text));
  return ok;
}
const char *chip(rs::SaveType t) {
  switch (t) {
  case rs::SaveType::Sram32K:
    return "SRAM 32 KiB";
  case rs::SaveType::Flash64K:
    return "FLASH 64 KiB";
  case rs::SaveType::Flash128K:
    return "FLASH 128 KiB";
  case rs::SaveType::Eeprom512:
    return "EEPROM 512 bytes";
  case rs::SaveType::Eeprom8K:
    return "EEPROM 8 KiB";
  default:
    return "Choose verified save chip";
  }
}
const char *encoding(rs::SaveEncoding t) {
  return t == rs::SaveEncoding::RawEmulator   ? "Raw emulator (mGBA / VBA)"
         : t == rs::SaveEncoding::OpenAgbFirm ? "open_agb_firm"
                                              : "Choose remote byte format";
}
std::string bytes(uint64_t n) {
  if (n >= 1024 * 1024)
    return std::to_string(n / (1024 * 1024)) + " MiB";
  return std::to_string(n / 1024) + " KiB";
}
} // namespace
int main() {
  if (!rs::initUi())
    return 1;
  osSetSpeedupEnable(true);
  romfsInit();
  void *sockets = memalign(0x1000, 1024 * 1024);
  bool network =
      sockets && R_SUCCEEDED(socInit(static_cast<u32 *>(sockets), 1024 * 1024));
  bool curl = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
  bool ac = R_SUCCEEDED(acInit());
  rs::Settings settings;
  std::string status, error, ca = "romfs:/cacert.pem";
  rs::makeDirectories("sdmc:/3ds/RetroSync3DS", error);
  if (rs::fileExists("sdmc:/3ds/RetroSync3DS/cacert.pem"))
    ca = "sdmc:/3ds/RetroSync3DS/cacert.pem";
  bool configured = rs::loadSettings(SettingsPath, settings, status);
  settings.sessionPath = SettingsPath;
  Screen screen = configured ? Screen::Platforms : Screen::Settings,
         settingsReturn = Screen::Platforms;
  std::vector<rs::Platform> platforms;
  rs::RomPage page;
  std::vector<rs::SaveInfo> saves;
  std::set<int> onSd;
  rs::Rom current{};
  rs::Install installed;
  rs::SaveInfo linking{};
  rs::SyncResult conflict;
  rs::SaveType linkType = rs::SaveType::Unknown;
  rs::SaveEncoding linkEncoding = rs::SaveEncoding::Unknown;
  size_t selected = 0, platformIndex = 0, gameIndex = 0;
  unsigned limit = rs::PageSize, tick = 0;
  uint64_t cachedFree = rs::freeBytes("sdmc:/");
  int pageMove = 0;
  std::vector<unsigned> previous;
  std::string filter, version, username = settings.username, password,
                               newSlotName = "autosave";
  bool connected = false, warning = false, newSlot = false, needsDraw = true;
  Thread thread = nullptr;
  std::unique_ptr<Job> job;
  auto launch = [&](Op op, unsigned offset = 0) {
    if (job)
      return;
    if (!network || !curl) {
      status = "Network services unavailable; restart the console";
      warning = true;
      return;
    }
    job = std::make_unique<Job>();
    job->op = op;
    job->settings = settings;
    job->ca = ca;
    job->username = username;
    if (op == Op::Login) {
      job->password = password;
      std::fill(password.begin(), password.end(), '\0');
      password.clear();
    }
    job->rom = current;
    job->offset = offset;
    job->limit = limit;
    job->filter = filter;
    if (platformIndex < platforms.size()) {
      job->selectedPlatform = platforms[platformIndex];
      job->platform = platforms[platformIndex].id;
    }
    job->selectedSave = linking;
    job->type = linkType;
    job->encoding = linkEncoding;
    job->newSlot = newSlot;
    job->slot = newSlotName;
    if (screen == Screen::Conflict) {
      job->choice = selected == 0 ? rs::ConflictChoice::KeepLocal
                                  : rs::ConflictChoice::KeepRemote;
      job->expectedLocal = conflict.localHash;
      job->expectedRemote = conflict.remoteHash;
    }
    thread = threadCreate(run, job.get(), 128 * 1024, 0x30, -2, false);
    if (!thread) {
      job.reset();
      status = "Cannot start worker; not enough memory";
      warning = true;
    } else {
      status = op == Op::Download ? "Downloading and verifying game..."
               : op == Op::Sync || op == Op::Bind ? "Comparing save copies..."
               : op == Op::Login                  ? "Signing in securely..."
                                                  : "Loading...";
      warning = false;
    }
  };
  if (configured)
    launch(Op::Connect);
  while (aptMainLoop()) {
    hidScanInput();
    u32 keys = hidKeysDown(), repeat = hidKeysDownRepeat();
    ++tick;
    if (keys || repeat)
      needsDraw = true;
    if (job && job->done.load(std::memory_order_acquire)) {
      threadJoin(thread, U64_MAX);
      threadFree(thread);
      thread = nullptr;
      needsDraw = true;
      settings = job->settings;
      username = settings.username;
      if (!job->error.empty()) {
        status = job->error;
        warning = true;
      } else {
        warning = false;
        status = job->status;
        if (job->op == Op::Connect || job->op == Op::Login) {
          connected = true;
          version = job->version;
          platforms = std::move(job->platforms);
          screen = Screen::Platforms;
          selected = 0;
          onSd.clear();
        } else if (job->op == Op::Page) {
          if (pageMove == 1)
            previous.push_back(page.offset);
          else if (pageMove == -1 && !previous.empty())
            previous.pop_back();
          page = std::move(job->page);
          limit = page.limit;
          for (const auto &item : page.items)
            onSd.erase(item.id);
          onSd.insert(job->onSd.begin(), job->onSd.end());
          selected = 0;
          screen = Screen::Games;
        } else if (job->op == Op::History) {
          saves = std::move(job->saves);
          installed = job->installed;
          screen = Screen::Saves;
          selected = 0;
        } else {
          installed = job->installed;
          if (installed.romId)
            onSd.insert(installed.romId);
          if (job->sync.conflict) {
            conflict = job->sync;
            screen = Screen::Conflict;
            selected = 2;
            warning = true;
          } else if (job->op == Op::Bind || job->op == Op::Sync) {
            screen = Screen::Detail;
            selected = 2;
          }
        }
      }
      job.reset();
    }
    if (keys & KEY_START) {
      if (job)
        job->progress.cancel.store(true);
      break;
    }
    if (job) {
      if (keys & KEY_B)
        job->progress.cancel.store(true);
    } else {
      size_t count = screen == Screen::Platforms  ? platforms.size()
                     : screen == Screen::Games    ? page.items.size()
                     : screen == Screen::Detail   ? 5
                     : screen == Screen::Saves    ? saves.size()
                     : screen == Screen::Settings ? 8
                     : screen == Screen::Link     ? 5
                                                  : 3;
      if ((repeat & KEY_DDOWN) && count)
        selected = (selected + 1) % count;
      if ((repeat & KEY_DUP) && count)
        selected = (selected + count - 1) % count;
      if (keys & KEY_TOUCH) {
        touchPosition touch;
        hidTouchRead(&touch);
        if (touch.py >= 33 && touch.py < 198) {
          size_t row = (touch.py - 33) / 33, index = (selected / 5) * 5 + row;
          if (index < count) {
            if (index == selected)
              keys |= KEY_A;
            selected = index;
          }
        } else if (touch.py >= 204) {
          if (touch.px < 80)
            keys |= KEY_B;
          else if (touch.px > 240)
            keys |= KEY_A;
          else
            keys |= KEY_Y;
        }
      }
      if (keys & KEY_SELECT) {
        if (screen == Screen::Settings) {
          screen = settingsReturn;
          selected = 0;
        } else {
          settingsReturn = screen;
          screen = Screen::Settings;
          selected = 0;
        }
      } else if (keys & KEY_B) {
        if (screen == Screen::Games) {
          screen = Screen::Platforms;
          selected = platformIndex;
        } else if (screen == Screen::Detail) {
          screen = Screen::Games;
          selected = gameIndex;
        } else if (screen == Screen::Saves || screen == Screen::Conflict) {
          screen = Screen::Detail;
          selected = 0;
          status = "No save copy was overwritten";
        } else if (screen == Screen::Link) {
          screen = newSlot ? Screen::Detail : Screen::Saves;
          selected = 0;
        } else if (screen == Screen::Settings) {
          screen = settingsReturn;
          selected = 0;
        }
      } else if (screen == Screen::Settings && (keys & KEY_A)) {
        if (selected == 0) {
          std::string url = settings.server;
          if (keyboard(url, "RomM server URL (https://...)")) {
            while (!url.empty() && url.back() == '/')
              url.pop_back();
            if (url != settings.server) {
              settings.token.clear();
              settings.refreshToken.clear();
              connected = false;
              platforms.clear();
              onSd.clear();
            }
            settings.server = url;
          }
        }
        if (selected == 1)
          keyboard(username, "RomM username");
        if (selected == 2)
          keyboard(password, "RomM password (not saved)", true);
        if (selected == 3) {
          if (password.empty())
            keyboard(password, "RomM password (not saved)", true);
          if (!password.empty())
            launch(Op::Login);
        }
        if (selected == 4) {
          std::string dir = settings.romDirectory;
          if (keyboard(dir, "GBA folder (sdmc:/roms/gba)") &&
              rs::safeSdDirectory(dir)) {
            settings.romDirectory = dir;
            if (!settings.token.empty())
              rs::saveSettings(SettingsPath, settings, status);
          }
        }
        if (selected == 5) {
          settings.token.clear();
          settings.refreshToken.clear();
          std::fill(password.begin(), password.end(), '\0');
          password.clear();
          bool erased = true;
          for (const char *suffix : {"", ".tmp", ".bak", ".bak.tmp"}) {
            if (std::remove((std::string(SettingsPath) + suffix).c_str()) !=
                    0 &&
                errno != ENOENT)
              erased = false;
          }
          connected = false;
          platforms.clear();
          page = rs::RomPage{};
          saves.clear();
          onSd.clear();
          status =
              erased
                  ? "Signed out locally. Revoke the session in RomM if needed."
                  : "SD credentials could not be removed. Revoke this session "
                    "in RomM.";
          warning = !erased;
        }
        if (selected == 6) {
          settings.allowHttp = !settings.allowHttp;
          status = settings.allowHttp
                       ? "HTTP sends credentials without encryption; trusted "
                         "LAN only"
                       : "HTTPS certificate verification enabled";
          warning = settings.allowHttp;
        }
        if (selected == 7) {
          screen = settingsReturn;
          selected = 0;
        }
      } else if (screen == Screen::Platforms) {
        if (keys & KEY_Y)
          launch(Op::Connect);
        if ((keys & KEY_A) && selected < platforms.size()) {
          platformIndex = selected;
          previous.clear();
          filter.clear();
          limit = rs::PageSize;
          page = rs::RomPage{};
          pageMove = 0;
          launch(Op::Page);
        }
      } else if (screen == Screen::Games) {
        if ((keys & KEY_R) && page.offset + page.items.size() < page.total) {
          pageMove = 1;
          launch(Op::Page, page.offset + page.items.size());
        } else if ((keys & KEY_L) && !previous.empty()) {
          pageMove = -1;
          launch(Op::Page, previous.back());
        } else if ((keys & KEY_Y) && keyboard(filter, "Search this platform")) {
          previous.clear();
          pageMove = 0;
          launch(Op::Page);
        } else if ((keys & (KEY_A | KEY_X)) && selected < page.items.size()) {
          gameIndex = selected;
          current = page.items[selected];
          installed = rs::Install{};
          selected = 0;
          screen = Screen::Detail;
          launch(keys & KEY_X ? Op::History : Op::Info);
        }
      } else if (screen == Screen::Detail && (keys & KEY_A)) {
        if (selected == 0)
          launch(Op::Download);
        if (selected == 1)
          launch(Op::History);
        if (selected == 2)
          launch(Op::Sync);
        if (selected == 3) {
          if (!installed.romId) {
            status = "Install this game before linking saves";
            warning = true;
          } else {
            newSlot = true;
            linkType = installed.saveType;
            linkEncoding = rs::SaveEncoding::RawEmulator;
            screen = Screen::Link;
            selected = 0;
          }
        }
        if (selected == 4) {
          screen = Screen::Games;
          selected = gameIndex;
        }
      } else if (screen == Screen::Saves && (keys & KEY_A) &&
                 selected < saves.size()) {
        if (!installed.romId) {
          status = "Install the game before linking a save";
          warning = true;
        } else if (saves[selected].archival || saves[selected].slot.empty()) {
          status =
              "Historical entries are preserved; choose a named active slot";
          warning = true;
        } else {
          linking = saves[selected];
          newSlot = false;
          linkType = installed.saveType;
          linkEncoding = rs::SaveEncoding::Unknown;
          screen = Screen::Link;
          selected = 0;
          status = "Confirm save chip and remote byte format before syncing";
        }
      } else if (screen == Screen::Link && (keys & KEY_A)) {
        if (selected == 0)
          linkType =
              static_cast<rs::SaveType>((static_cast<int>(linkType) + 1) % 6);
        if (selected == 1)
          linkEncoding = static_cast<rs::SaveEncoding>(
              (static_cast<int>(linkEncoding) + 1) % 3);
        if (selected == 2 && newSlot)
          keyboard(newSlotName, "Remote save slot name");
        if (selected == 3)
          launch(Op::Bind);
        if (selected == 4) {
          screen = newSlot ? Screen::Detail : Screen::Saves;
          selected = 0;
        }
      } else if (screen == Screen::Conflict && (keys & KEY_A)) {
        if (selected == 2) {
          screen = Screen::Detail;
          selected = 2;
          status = "Sync cancelled; both copies preserved";
          warning = false;
        } else
          launch(Op::Sync);
      }
    }
    if (tick % 120 == 0) {
      auto available = rs::freeBytes("sdmc:/");
      if (available != cachedFree) {
        cachedFree = available;
        needsDraw = true;
      }
    }
    if (!needsDraw && !job) {
      gspWaitForVBlank();
      continue;
    }
    rs::UiFrame f;
    f.selected = selected;
    f.connected = connected;
    f.status = status;
    f.warning = warning;
    f.tick = tick;
    f.busy = static_cast<bool>(job);
    f.eyebrow = connected ? "ROMM " + version + "  /  " +
                                std::to_string(cachedFree / (1024 * 1024)) +
                                " MiB FREE"
                          : "YOUR LIBRARY. YOUR PROGRESS.";
    if (job) {
      auto total = job->progress.total.load(),
           now = job->progress.current.load();
      if (total)
        f.progress = static_cast<float>(now) / total;
    }
    f.hint = "B  Back       Y  Search / refresh       A  Open";
    if (screen == Screen::Platforms) {
      f.section = "Library";
      f.title = "Pick your next adventure";
      f.subtitle =
          std::to_string(platforms.size()) + " platforms in your collection";
      f.description = "Browse your games. Bring your progress with you.";
      for (const auto &item : platforms)
        f.rows.push_back(
            {item.name, item.slug, item.slug == "gba" ? "GBA" : ""});
      if (platforms.empty())
        f.rows.push_back({"Sign in to RomM", "SELECT opens Settings", ""});
    } else if (screen == Screen::Games) {
      f.section = "Games  /  " + std::to_string(page.offset) + "-" +
                  std::to_string(page.offset + page.items.size());
      f.title = platformIndex < platforms.size() ? platforms[platformIndex].name
                                                 : "Games";
      f.subtitle = std::to_string(page.total) + " games" +
                   (filter.empty() ? "" : "  /  " + filter);
      f.description = "L / R to change pages. A for game actions.";
      for (const auto &item : page.items)
        f.rows.push_back(
            {item.title, bytes(item.size), onSd.count(item.id) ? "ON SD" : ""});
    } else if (screen == Screen::Detail) {
      f.section = "Game actions";
      f.title = current.title;
      f.subtitle =
          installed.romId
              ? "INSTALLED  /  " + std::string(chip(installed.saveType))
              : bytes(current.size) + "  /  AVAILABLE ON ROMM";
      f.description = installed.slot.empty() ? current.summary
                                             : "Save slot: " + installed.slot;
      f.rows = {{installed.romId ? "Verify installed game" : "Download game",
                 "GBA + ZIP extraction, verified before install", ""},
                {"Remote save history", "Choose the slot you actually use", ""},
                {"Sync saves",
                 installed.slot.empty() ? "Link a save slot first"
                                        : "Compare local and remote progress",
                 ""},
                {"Create a new save link",
                 "Track this game's open_agb_firm battery save", ""},
                {"Back to games", "", ""}};
      f.hint = "B  Games                               A  Select";
    } else if (screen == Screen::Saves) {
      f.section = "Remote saves";
      f.title = current.title;
      f.subtitle = std::to_string(saves.size()) + " history entries";
      f.description = "Choose a named slot. Older versions stay in RomM.";
      for (const auto &save : saves)
        f.rows.push_back(
            {save.archival ? "Archive: " + save.filename : "Slot: " + save.slot,
             save.emulator + "  /  " + bytes(save.size),
             save.archival ? "HISTORY" : "LINK"});
      if (selected < saves.size()) {
        const auto &item = saves[selected];
        f.subtitle =
            (item.archival ? "Historical save" : "Slot: " + item.slot) +
            "  /  " + item.emulator;
        f.description = item.updated + "  /  " + bytes(item.size);
      }
      if (saves.empty())
        f.description =
            "No remote save yet. Create a save link from game actions.";
      f.hint = "B  Game actions                       A  Link slot";
    } else if (screen == Screen::Settings) {
      f.section = "Settings";
      f.title = "Welcome to RetroSync";
      f.subtitle = "Connect your RomM account";
      f.description =
          "Sign in once. Your password is never saved on the SD card.";
      f.rows = {
          {"Server URL",
           settings.server.empty() ? "https://your-romm-server"
                                   : settings.server,
           ""},
          {"Username", username.empty() ? "Your RomM username" : username, ""},
          {"Password",
           password.empty() ? "Enter when signing in"
                            : "Ready - hidden, not saved",
           ""},
          {"Sign in", "Secure login with automatic session renewal", ""},
          {"Games folder", settings.romDirectory, ""},
          {"Sign out", "Remove this session from the SD card", ""},
          {"Allow HTTP on trusted LAN",
           settings.allowHttp ? "ON - credentials are not encrypted"
                              : "OFF - HTTPS required",
           ""},
          {"Back", "", ""}};
      f.hint = "B  Back                                 A  Edit";
    } else if (screen == Screen::Link) {
      f.section = "Link save slot";
      f.title = current.title;
      f.subtitle = "open_agb_firm battery save";
      f.description = "Verify the chip type and producer format. EEPROM size "
                      "must be known.";
      f.rows = {{"Save chip", chip(linkType), "A CYCLE"},
                {"Remote format", encoding(linkEncoding), "A CYCLE"},
                {"Remote slot", newSlot ? newSlotName : linking.slot,
                 newSlot ? "A EDIT" : ""},
                {"Link and sync",
                 "Different existing copies will ask for a choice", ""},
                {"Cancel", "Keep all save copies unchanged", ""}};
      f.hint = "B  Cancel                        A  Change / confirm";
    } else {
      f.section = "Resolve save conflict";
      f.title = current.title;
      f.subtitle = "Both save copies have changed";
      f.description = "Local: " + bytes(conflict.localSize) + "  " +
                      conflict.localHash.substr(0, 12) +
                      "  Remote: " + bytes(conflict.remoteSize) + "  " +
                      conflict.remoteHash.substr(0, 12);
      f.warning = true;
      f.rows = {
          {"Keep local", "Back up remote, then upload this SD save", "LOCAL"},
          {"Keep remote", "Back up local, then download the RomM save", "ROMM"},
          {"Cancel", "Leave both progress copies unchanged", "SAFE"}};
      f.hint = "B  Cancel                              A  Confirm";
    }
    rs::drawUi(f);
    needsDraw = false;
  }
  if (job) {
    job->progress.cancel.store(true);
    threadJoin(thread, U64_MAX);
    threadFree(thread);
    job.reset();
  }
  std::fill(password.begin(), password.end(), '\0');
  if (ac)
    acExit();
  if (curl)
    curl_global_cleanup();
  if (network)
    socExit();
  std::free(sockets);
  romfsExit();
  rs::shutdownUi();
  return 0;
}
