#include "retrosync/api.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <unistd.h>

static unsigned checks=0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); std::exit(1); } } while(0)
static std::string fixture(const char* name) {
    std::ifstream f(std::string("tests/fixtures/")+name);
    CHECK(f.good()); return std::string(std::istreambuf_iterator<char>(f),{});
}
int main() {
    using namespace rs;
    const Copy absent{false,""},a{true,"a"},b{true,"b"},c{true,"c"},unknown{true,""};
    struct Case { Copy l,r,base; Decision expected; };
    const Case cases[]={
        {absent,absent,absent,Decision::Unchanged},{absent,absent,a,Decision::BothDeleted},
        {a,absent,absent,Decision::Upload},{absent,a,absent,Decision::Download},
        {a,a,absent,Decision::Unchanged},{a,a,b,Decision::Unchanged},
        {a,b,absent,Decision::Conflict},{a,b,a,Decision::Download},
        {a,b,b,Decision::Upload},{a,b,c,Decision::Conflict},
        {absent,a,a,Decision::LocalDeleted},{a,absent,a,Decision::RemoteDeleted},
        {absent,b,a,Decision::LocalDeleted},{b,absent,a,Decision::RemoteDeleted},
        {unknown,a,a,Decision::Unsupported},{a,unknown,a,Decision::Unsupported},
        {a,b,unknown,Decision::Unsupported}};
    for (const auto& test:cases) {
        CHECK(reconcile(test.l,test.r,test.base,true)==test.expected);
        CHECK(reconcile(test.l,test.r,test.base,false)==Decision::Unsupported);
    }
    // Exhaust all presence/hash combinations; no differing first-contact pair may auto-resolve.
    for (const auto& l:{absent,a,b,c,unknown}) for (const auto& r:{absent,a,b,c,unknown}) {
        auto d=reconcile(l,r,absent,true);
        if (l.exists && r.exists && !l.hash.empty() && !r.hash.empty() && l.hash!=r.hash) CHECK(d==Decision::Conflict);
    }
    for (const auto& bad:{"",".","..","../a","a/b","a\\b","a:","x\n","x\x1b","x.","x ","x?"}) CHECK(!safeFilename(bad));
    CHECK(!safeFilename(std::string("bad\0name",8)));
    CHECK(safeFilename("Game (Europe) (Rev 2).gba"));
    CHECK(safeSdDirectory("sdmc:/roms/gba")); CHECK(safeSdDirectory("sdmc:/"));
    for (const auto& bad:{"/roms/gba","sdmc:/../x","sdmc:/a//b","sdmc:/a/","sdmc:/a/../b"}) CHECK(!safeSdDirectory(bad));
    CHECK(validServerUrl("https://romm.example/romm"));
    for (const auto& bad:{"https://","https:///romm","https://host/","https://user:pass@host","https://host?token=x","https://host\r\nX:x","file:///a","http://:80"}) CHECK(!validServerUrl(bad));
    CHECK(urlEncode("a b&?#/é")=="a%20b%26%3F%23%2F%C3%A9");
    CHECK(displayText("Title\x1b[2J\n",50)=="Title [2J ");
    OafConfig cfg; std::string error,path;
    CHECK(parseOafConfig("[general]\nuseSavesFolder=false\n[game]\nsaveSlot=9\n",cfg,error));
    CHECK(!cfg.useSavesFolder && cfg.slot==9);
    CHECK(oafSavePath("sdmc:/roms/gba/Game.gba",cfg,path)); CHECK(path=="sdmc:/roms/gba/Game.9.sav");
    CHECK(!parseOafConfig("[game]\nsaveSlot=10",cfg,error)); CHECK(cfg.slot==9);
    CHECK(!parseOafConfig("[general]\nuseSavesFolder=maybe",cfg,error));
    cfg=OafConfig{}; CHECK(oafSavePath("sdmc:/roms/gba/Game.gba",cfg,path));
    CHECK(path=="sdmc:/3ds/open_agb_firm/saves/Game.sav");
    CHECK(parseOafConfig("\xef\xbb\xbf[general] ; comment\nuseSavesFolder: false ; inline\n",cfg,error));
    CHECK(!cfg.useSavesFolder); cfg=OafConfig{};
    for (unsigned slot=1;slot<=9;++slot) { cfg.slot=slot; CHECK(oafSavePath("sdmc:/roms/gba/G.gba",cfg,path)); CHECK(path=="sdmc:/3ds/open_agb_firm/saves/G."+std::to_string(slot)+".sav"); }
    CHECK(!oafSavePath("sdmc:/roms/gba/../Game.gba",cfg,path)); CHECK(!oafSavePath("sdmc:/roms/gba/Game.zip",cfg,path));
    cfg.slot=10; CHECK(!oafSavePath("sdmc:/roms/gba/Game.gba",cfg,path));
    std::vector<uint8_t> save(512),converted,roundtrip;
    for (size_t i=0;i<save.size();++i) save[i]=static_cast<uint8_t>(i);
    CHECK(convertSave(save,SaveType::Eeprom512,SaveEncoding::RawEmulator,SaveEncoding::OpenAgbFirm,converted,error));
    CHECK(converted[0]==7 && converted[7]==0 && converted[8]==15);
    CHECK(convertSave(converted,SaveType::Eeprom512,SaveEncoding::OpenAgbFirm,SaveEncoding::RawEmulator,roundtrip,error));
    CHECK(save==roundtrip);
    auto preserved=converted;
    CHECK(!convertSave(save,SaveType::Unknown,SaveEncoding::RawEmulator,SaveEncoding::OpenAgbFirm,converted,error)); CHECK(converted==preserved);
    CHECK(!convertSave(save,SaveType::Eeprom8K,SaveEncoding::RawEmulator,SaveEncoding::OpenAgbFirm,converted,error));
    CHECK(!convertSave(save,SaveType::Eeprom512,SaveEncoding::Unknown,SaveEncoding::OpenAgbFirm,converted,error));
    for (const auto& pair:{std::make_pair(SaveType::Sram32K,32768),std::make_pair(SaveType::Flash64K,65536),std::make_pair(SaveType::Flash128K,131072),std::make_pair(SaveType::Eeprom8K,8192)}) {
        std::vector<uint8_t> data(pair.second,0xAA); data[10]=0x42;
        CHECK(convertSave(data,pair.first,SaveEncoding::RawEmulator,SaveEncoding::OpenAgbFirm,converted,error));
        CHECK(convertSave(converted,pair.first,SaveEncoding::OpenAgbFirm,SaveEncoding::RawEmulator,roundtrip,error)); CHECK(data==roundtrip);
    }
    save[0]='P';save[1]='K';save[2]=3;save[3]=4;
    CHECK(!convertSave(save,SaveType::Eeprom512,SaveEncoding::RawEmulator,SaveEncoding::OpenAgbFirm,converted,error));
    std::string version; CHECK(parseHeartbeat(fixture("heartbeat.json"),version,error)); CHECK(version=="5.4.0");
    CHECK(!parseHeartbeat("{}",version,error)); CHECK(!parseHeartbeat("<html>login</html>",version,error));
    std::vector<Platform> platforms; CHECK(parsePlatforms(fixture("platforms.json"),platforms,error));
    CHECK(platforms.size()==2 && platforms[0].id==2 && platforms[1].id==1);
    CHECK(!parsePlatforms("[{\"id\":-1,\"name\":\"x\"}]",platforms,error)); CHECK(platforms.size()==2);
    RomPage page; CHECK(parseRoms(fixture("roms.json"),page,error)); CHECK(page.total==21 && page.items[0].id==123);
    CHECK(!parseRoms("{\"items\":[],\"total\":null,\"offset\":0}",page,error)); CHECK(page.total==21);
    CHECK(!parseRoms("{\"items\":[],\"total\":0,\"offset\":1}",page,error));
    CHECK(!parseRoms("{} trailing",page,error));
    CHECK(!parseHeartbeat(std::string(MaxJsonBytes+1,'x'),version,error));
    CHECK(!parseHeartbeat(std::string("{}\0extra",8),version,error));
    std::vector<SaveInfo> saves; CHECK(parseSaves(fixture("saves.json"),saves,error));
    CHECK(saves.size()==3 && saves[0].slot=="autosave" && saves[1].slot.empty());
    CHECK(!saves[0].archival && saves[1].archival);
    CHECK(parseSaves("[{\"id\":1,\"file_size_bytes\":1,\"slot\":\"\"}]",saves,error) && !saves[0].archival && saves[0].slot.empty());
    auto query=romListPath(2,20,"a&b"); CHECK(query.find("search_term=a%26b")!=std::string::npos); CHECK(query.find("limit=20")!=std::string::npos);
    Settings settings; settings.server="https://romm.example"; settings.token="rmm_example_fake_token";
    CHECK(validateSettings(settings,error)); settings.token="x\r\nAuthorization: bad"; CHECK(!validateSettings(settings,error));
    settings.token="fake"; settings.server="http://localhost"; CHECK(!validateSettings(settings,error)); settings.allowHttp=true;
    char temp[]="/tmp/retrosync-test-XXXXXX"; char* dir=mkdtemp(temp); CHECK(dir);
    std::string file=std::string(dir)+"/settings.ini";
    CHECK(saveSettings(file,settings,error)); Settings loaded;
    CHECK(loadSettings(file,loaded,error)); CHECK(loaded.server==settings.server && loaded.token==settings.token && loaded.allowHttp);
    settings.token="bad\nvalue"; CHECK(!saveSettings(file,settings,error)); CHECK(loadSettings(file,loaded,error) && loaded.token=="fake");
    CHECK(!saveSettings(std::string(dir)+"/absent/settings.ini",loaded,error));
    loaded.token="second"; CHECK(saveSettings(file,loaded,error));
    CHECK(loadSettings(file,loaded,error) && loaded.token=="second");
    std::remove(file.c_str());
    CHECK(loadSettings(file,loaded,error) && loaded.token=="fake");
    CHECK(error.find("Recovered")!=std::string::npos);
    std::remove((file+".bak").c_str()); rmdir(dir);
    std::printf("PASS: %u core checks (synthetic fixtures; no hardware)\n",checks);
}
