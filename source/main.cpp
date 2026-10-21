#include <3ds.h>
#include <curl/curl.h>
#include <malloc.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>
#include "retrosync/api.hpp"
#include "retrosync/strings.hpp"

namespace {
constexpr const char* SettingsPath="sdmc:/3ds/RetroSync3DS/settings.ini";
enum class Screen { Platforms, Games, Settings, Saves };
enum class Operation { Connect, Games, Saves };
struct Job {
    Operation op=Operation::Connect;
    rs::Settings settings;
    std::string path,ca,error,version;
    std::vector<rs::Platform> platforms;
    rs::RomPage page;
    std::vector<rs::SaveInfo> saves;
    std::atomic<bool> cancel{false},done{false};
    std::atomic<unsigned> received{0};
};
void runJob(void* arg) {
    auto& job=*static_cast<Job*>(arg);
    if (job.op == Operation::Connect) {
        auto r=rs::getJson(job.settings,"/api/heartbeat",job.ca,job.cancel,job.received);
        job.error=r.error;
        if (job.error.empty() && rs::parseHeartbeat(r.body,job.version,job.error)) {
            r=rs::getJson(job.settings,"/api/platforms",job.ca,job.cancel,job.received);
            job.error=r.error;
            if (job.error.empty()) rs::parsePlatforms(r.body,job.platforms,job.error);
        }
    } else {
        auto r=rs::getJson(job.settings,job.path,job.ca,job.cancel,job.received);
        job.error=r.error;
        if (job.error.empty()) {
            if (job.op == Operation::Games) rs::parseRoms(r.body,job.page,job.error);
            else rs::parseSaves(r.body,job.saves,job.error);
        }
    }
    if (job.cancel.load()) job.error="Request cancelled";
    job.done.store(true,std::memory_order_release);
}
void printAt(PrintConsole& console,int row,const std::string& text) {
    consoleSelect(&console);
    std::printf("\x1b[%d;1H%s",row+1,rs::displayText(text,console.consoleWidth-1).c_str());
}
bool keyboard(std::string& text,const char* hint,bool secret=false) {
    SwkbdState keyboard; swkbdInit(&keyboard,SWKBD_TYPE_NORMAL,2,512);
    swkbdSetHintText(&keyboard,hint);
    if (!secret) swkbdSetInitialText(&keyboard,text.c_str());
    if (secret) swkbdSetPasswordMode(&keyboard,SWKBD_PASSWORD_HIDE);
    char buffer[513]={0};
    if (swkbdInputText(&keyboard,buffer,sizeof(buffer)) != SWKBD_BUTTON_RIGHT) return false;
    text=buffer; std::memset(buffer,0,sizeof(buffer)); return true;
}
void readOaf(rs::OafConfig& config,std::string& status) {
    FILE* f=std::fopen("sdmc:/3ds/open_agb_firm/config.ini","rb");
    if (!f) { status="OAF config absent; upstream defaults"; return; }
    std::vector<char> buffer(65537); size_t n=std::fread(buffer.data(),1,buffer.size(),f);
    bool bad=std::ferror(f); std::fclose(f);
    if (bad || n>65536 || !rs::parseOafConfig(std::string(buffer.data(),n),config,status)) {
        status="OAF config unreadable; mapping disabled"; config.slot=10; return;
    }
    status=config.useSavesFolder ? "OAF: shared saves folder" : "OAF: saves beside ROM";
}
}
int main() {
    gfxInitDefault();
    PrintConsole top,bottom; consoleInit(GFX_TOP,&top); consoleInit(GFX_BOTTOM,&bottom);
    romfsInit();
    void* socketBuffer=memalign(0x1000,1024*1024);
    bool socReady=socketBuffer && R_SUCCEEDED(socInit(static_cast<u32*>(socketBuffer),1024*1024));
    bool curlReady=curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK;
    bool acReady=R_SUCCEEDED(acInit());
    rs::Settings settings;
    std::string status,version="unknown",filter;
    rs::loadSettings(SettingsPath,settings,status);
    rs::OafConfig oaf; std::string oafStatus; readOaf(oaf,oafStatus);
    Screen screen=Screen::Platforms;
    std::vector<rs::Platform> platforms;
    rs::RomPage page;
    std::vector<rs::SaveInfo> saves;
    size_t selected=0,platformIndex=0;
    Thread worker=nullptr; Job* job=nullptr;
    bool dirty=true,quitting=false; unsigned frame=0;
    auto launch=[&](Operation op,const std::string& path) {
        if (job) return;
        if (!socReady || !curlReady) { status="Network initialization failed; restart app"; return; }
        if (!rs::validateSettings(settings,status)) return;
        u32 wifi=0;
        if (!acReady || R_FAILED(ACU_GetWifiStatus(&wifi)) || !wifi) { status="Wi-Fi disconnected; connect in system settings"; return; }
        job=new Job; job->op=op; job->path=path; job->settings=settings;
        struct stat st;
        job->ca=stat("sdmc:/3ds/RetroSync3DS/cacert.pem",&st)==0 ? "sdmc:/3ds/RetroSync3DS/cacert.pem" : "romfs:/cacert.pem";
        worker=threadCreate(runJob,job,128*1024,0x30,-2,false);
        if (!worker) { delete job; job=nullptr; status="Cannot start network worker"; }
        else status=rs::text::busy;
        dirty=true;
    };
    while (aptMainLoop()) {
        hidScanInput(); u32 keys=hidKeysDown(); u32 repeat=hidKeysDownRepeat();
        if (keys&KEY_START) { quitting=true; if (job) job->cancel.store(true); else break; }
        if (job && job->done.load(std::memory_order_acquire)) {
            threadJoin(worker,U64_MAX); threadFree(worker); worker=nullptr;
            status=job->error;
            if (status.empty()) {
                if (job->op == Operation::Connect) {
                    platforms=std::move(job->platforms); version=job->version; screen=Screen::Platforms; selected=0;
                    page=rs::RomPage{}; saves.clear(); filter.clear();
                    status="Connected. Save writes are disabled.";
                } else if (job->op == Operation::Games) {
                    page=std::move(job->page); screen=Screen::Games; selected=0;
                    status="A: game info   X: remote save info";
                } else { saves=std::move(job->saves); screen=Screen::Saves; selected=0; status="History listed; no active slot assumed"; }
            }
            delete job; job=nullptr; dirty=true;
            if (quitting) break;
        }
        if (job) {
            if (keys&KEY_B) { job->cancel.store(true); status="Cancelling request..."; dirty=true; }
        } else if (!quitting) {
            if (keys&KEY_SELECT) { screen=screen==Screen::Settings ? Screen::Platforms : Screen::Settings; selected=0; dirty=true; }
            size_t count=screen==Screen::Platforms ? platforms.size() : screen==Screen::Games ? page.items.size() : screen==Screen::Saves ? saves.size() : 7;
            if ((repeat&KEY_DDOWN) && count) { selected=(selected+1)%count; dirty=true; }
            if ((repeat&KEY_DUP) && count) { selected=(selected+count-1)%count; dirty=true; }
            if (keys&KEY_TOUCH) {
                touchPosition touch; hidTouchRead(&touch);
                if (touch.py>=32 && touch.py<192) {
                    size_t row=(touch.py-32)/16, first=(selected/10)*10;
                    if (first+row<count) { selected=first+row; dirty=true; }
                }
                if (touch.py>=208) {
                    if (touch.px<106) keys|=KEY_B;
                    else if (touch.px<213) keys|=KEY_Y;
                    else keys|=KEY_A;
                }
            }
            if (keys&KEY_B) {
                if (screen==Screen::Saves) { screen=Screen::Games; selected=0; }
                else { screen=Screen::Platforms; selected=platformIndex<platforms.size()?platformIndex:0; }
                dirty=true;
            }
            if (screen==Screen::Settings && (keys&KEY_A)) {
                if (selected==0) { if (keyboard(settings.server,"RomM URL (https://...)")) {
                    while (!settings.server.empty() && settings.server.back()=='/') settings.server.pop_back();
                } }
                if (selected==1) keyboard(settings.token,"Client API token (hidden)",true);
                if (selected==2) keyboard(settings.romDirectory,"ROM directory: sdmc:/roms/gba");
                if (selected==3) { settings.allowHttp=!settings.allowHttp; status=settings.allowHttp?"HTTP exposes your token on the network":"HTTPS required"; }
                if (selected==4) {
                    if (mkdir("sdmc:/3ds",0777)!=0) { /* Usually already exists. */ }
                    mkdir("sdmc:/3ds/RetroSync3DS",0777);
                    if (rs::saveSettings(SettingsPath,settings,status)) status="Settings saved. Token is readable on SD.";
                }
                if (selected==5) launch(Operation::Connect,"");
                if (selected==6) {
                    settings.token.clear();
                    bool erased=std::remove(SettingsPath)==0 || errno==ENOENT;
                    for (const char* suffix:{".tmp",".bak",".bak.tmp"}) {
                        std::string extra=std::string(SettingsPath)+suffix;
                        if (std::remove(extra.c_str())!=0 && errno!=ENOENT) erased=false;
                    }
                    platforms.clear(); page=rs::RomPage{}; saves.clear(); version="unknown";
                    status=erased?"Disconnected locally; revoke token in RomM":"SD token erase failed; revoke token in RomM";
                }
                dirty=true;
            } else if (screen==Screen::Platforms) {
                if (keys&KEY_Y) launch(Operation::Connect,"");
                if ((keys&KEY_A) && selected<platforms.size()) {
                    platformIndex=selected; filter.clear();
                    launch(Operation::Games,rs::romListPath(platforms[selected].id,0,filter));
                }
            } else if (screen==Screen::Games && platformIndex<platforms.size()) {
                int id=platforms[platformIndex].id;
                if ((keys&KEY_R) && page.offset+rs::PageSize<page.total) launch(Operation::Games,rs::romListPath(id,page.offset+rs::PageSize,filter));
                if ((keys&KEY_L) && page.offset>=rs::PageSize) launch(Operation::Games,rs::romListPath(id,page.offset-rs::PageSize,filter));
                if ((keys&KEY_Y) && keyboard(filter,"Search this platform")) launch(Operation::Games,rs::romListPath(id,0,filter));
                if ((keys&KEY_X) && selected<page.items.size()) launch(Operation::Saves,"/api/saves?rom_id="+std::to_string(page.items[selected].id));
                if (keys&KEY_A) { status="Downloads/sync not enabled in this build"; dirty=true; }
            }
        }
        if (dirty || (job && (++frame % 15 == 0))) {
            consoleSelect(&top); consoleClear();
            printAt(top,0,rs::text::title); printAt(top,1,rs::text::subtitle);
            printAt(top,3,"Server version: "+version);
            printAt(top,4,settings.allowHttp?"WARNING: HTTP LAN opt-in enabled":"TLS peer and hostname verification ON");
            printAt(top,6,oafStatus); printAt(top,7,rs::text::states);
            if (screen==Screen::Games && selected<page.items.size()) {
                const auto& rom=page.items[selected];
                printAt(top,9,rom.title); printAt(top,10,"RomM ID: "+std::to_string(rom.id));
                printAt(top,11,rom.filename); printAt(top,12,"Size: "+std::to_string(rom.size)+" bytes");
                std::string summary=rs::displayText(rom.summary,350);
                for (size_t n=0;n<summary.size();n+=49) printAt(top,14+n/49,summary.substr(n,49));
                printAt(top,22,"Installation: not tracked in prototype");
            } else if (screen==Screen::Saves && selected<saves.size()) {
                const auto& save=saves[selected];
                printAt(top,9,save.filename); printAt(top,10,"Save ID: "+std::to_string(save.id));
                printAt(top,11,"Slot: "+(save.archival?"archival (null)":save.slot.empty()?"(empty string slot)":save.slot));
                printAt(top,12,"Emulator: "+save.emulator); printAt(top,13,save.updated);
                printAt(top,14,"Size: "+std::to_string(save.size)+" bytes"); printAt(top,15,"MD5: "+save.hash);
                printAt(top,17,"Compatibility has not been established.");
                printAt(top,18,"History entry is not necessarily active.");
            }
            FS_ArchiveResource resource;
            if (R_SUCCEEDED(FSUSER_GetSdmcArchiveResource(&resource)))
                printAt(top,24,"SD free: "+std::to_string(static_cast<uint64_t>(resource.freeClusters)*resource.clusterSize/1024/1024)+" MiB");
            printAt(top,25,rs::text::readonly);
            for (size_t n=0;n<status.size() && n<147;n+=49) printAt(top,26+n/49,status.substr(n,49));
            printAt(top,29,"START: exit   SELECT: settings");
            consoleSelect(&bottom); consoleClear();
            printAt(bottom,0,screen==Screen::Platforms ? rs::text::platforms : screen==Screen::Games ? rs::text::games : screen==Screen::Settings ? rs::text::settings : "Remote save history (read only)");
            if (screen==Screen::Games) printAt(bottom,1,"Offset "+std::to_string(page.offset)+" / "+std::to_string(page.total)+"  L/R: page");
            if (screen==Screen::Settings) {
                const std::string labels[]={"Server: "+settings.server,"Token: "+std::string(settings.token.empty()?"not set":"[hidden]"),"ROM path: "+settings.romDirectory,
                    "Allow HTTP (unsafe LAN): "+std::string(settings.allowHttp?"ON":"OFF"),"Save settings to SD","Test connection / load library","Disconnect / erase local token"};
                for (size_t i=0;i<7;++i) printAt(bottom,4+i*2,(selected==i?"> ":"  ")+labels[i]);
                printAt(bottom,20,"Token is stored as plaintext on SD.");
                printAt(bottom,21,"SELECT: return to platforms");
            } else {
                size_t count=screen==Screen::Platforms ? platforms.size() : screen==Screen::Games ? page.items.size() : saves.size();
                size_t first=(selected/10)*10;
                for (size_t i=first;i<count && i<first+10;++i) {
                    std::string label=screen==Screen::Platforms ? platforms[i].name : screen==Screen::Games ? page.items[i].title : saves[i].filename;
                    printAt(bottom,4+(i-first)*2,(i==selected?"> ":"  ")+label);
                }
                if (!count) printAt(bottom,5,"No entries. Y: connect/retry");
            }
            if (job) printAt(bottom,24,"Received "+std::to_string(job->received.load())+" bytes; B cancels");
            else printAt(bottom,24,status);
            printAt(bottom,26,"[ B:Back ] [ Y:Load/Search ] [ A:Open ]");
            printAt(bottom,28,screen==Screen::Games?"X: save history  SELECT: settings":"D-pad/touch: select   A: open/edit");
            dirty=false;
        }
        gfxFlushBuffers(); gfxSwapBuffers(); gspWaitForVBlank();
    }
    if (job) { job->cancel.store(true); threadJoin(worker,U64_MAX); threadFree(worker); delete job; }
    if (acReady) acExit();
    if (curlReady) curl_global_cleanup();
    if (socReady) socExit();
    std::free(socketBuffer); romfsExit(); gfxExit(); return 0;
}
