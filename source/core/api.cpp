#include "retrosync/api.hpp"
#include "cJSON.h"
#include <algorithm>
#include <cmath>
#include <climits>
#include <memory>

namespace rs {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
static Json parse(const std::string& s, std::string& error) {
    if (s.empty() || s.size() > MaxJsonBytes || s.find('\0') != std::string::npos) {
        error = "JSON response empty or exceeds 512 KiB"; return Json(nullptr,cJSON_Delete);
    }
    Json root(cJSON_ParseWithLengthOpts(s.c_str(),s.size()+1,nullptr,1),cJSON_Delete);
    if (!root) error = "Invalid JSON response";
    return root;
}
static const cJSON* field(const cJSON* j, const char* k) { return cJSON_GetObjectItemCaseSensitive(j,k); }
static std::string str(const cJSON* j, const char* k, size_t cap = 256) {
    const cJSON* v=field(j,k);
    return cJSON_IsString(v) ? std::string(v->valuestring).substr(0,cap) : "";
}
static bool number(const cJSON* j, const char* k, double& out, double max) {
    const cJSON* v=field(j,k);
    if (!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble) || v->valuedouble < 0 ||
        v->valuedouble > max || std::floor(v->valuedouble) != v->valuedouble) return false;
    out=v->valuedouble; return true;
}
static bool id(const cJSON* j, const char* k, int& out) {
    double value;
    if (!number(j,k,value,INT_MAX) || value < 1) return false;
    out=static_cast<int>(value); return true;
}
bool parseHeartbeat(const std::string& s, std::string& version, std::string& error) {
    auto j=parse(s,error); if (!j) return false;
    std::string v=str(field(j.get(),"SYSTEM"),"VERSION",64);
    if (v.empty()) { error="Unsupported heartbeat schema: SYSTEM.VERSION missing"; return false; }
    version=v; return true;
}
bool parsePlatforms(const std::string& s, std::vector<Platform>& out, std::string& error) {
    auto j=parse(s,error); if (!j) return false;
    if (!cJSON_IsArray(j.get()) || cJSON_GetArraySize(j.get()) > 512) { error="Unsupported platform schema or more than 512 platforms"; return false; }
    std::vector<Platform> result;
    const cJSON* item;
    cJSON_ArrayForEach(item,j.get()) {
        Platform p;
        if (!id(item,"id",p.id)) { error="Platform ID missing or invalid"; return false; }
        p.name=str(item,"display_name"); if (p.name.empty()) p.name=str(item,"custom_name");
        if (p.name.empty()) p.name=str(item,"name");
        p.slug=str(item,"slug");
        if (p.name.empty()) { error="Platform name missing"; return false; }
        for (const auto& prior:result) if (prior.id == p.id) { error="Duplicate platform ID"; return false; }
        result.push_back(p);
    }
    std::sort(result.begin(),result.end(),[](const Platform& a,const Platform& b){ return a.name < b.name; });
    out=std::move(result); return true;
}
bool parseRoms(const std::string& s, RomPage& out, std::string& error) {
    auto j=parse(s,error); if (!j) return false;
    const cJSON* items=field(j.get(),"items"); double total,offset;
    if (!cJSON_IsArray(items) || cJSON_GetArraySize(items) > static_cast<int>(PageSize) ||
        !number(j.get(),"total",total,INT_MAX) || !number(j.get(),"offset",offset,INT_MAX)) {
        error="Unsupported ROM page schema or server ignored page limit"; return false;
    }
    RomPage result; result.total=static_cast<unsigned>(total); result.offset=static_cast<unsigned>(offset);
    const cJSON* item;
    cJSON_ArrayForEach(item,items) {
        Rom r; double size;
        if (!id(item,"id",r.id) || !id(item,"platform_id",r.platformId) || !number(item,"fs_size_bytes",size,9007199254740991.0)) {
            error="Invalid ROM identity or size"; return false;
        }
        r.filename=str(item,"fs_name"); r.title=str(item,"name"); if (r.title.empty()) r.title=r.filename;
        r.summary=str(item,"summary",1024); r.size=static_cast<uint64_t>(size);
        if (r.title.empty()) { error="ROM title missing"; return false; }
        for (const auto& prior:result.items) if (prior.id == r.id) { error="Duplicate ROM ID"; return false; }
        result.items.push_back(r);
    }
    if (result.offset > result.total || result.items.size() > result.total-result.offset) { error="Inconsistent ROM page counts"; return false; }
    out=std::move(result); return true;
}
bool parseSaves(const std::string& s,std::vector<SaveInfo>& out,std::string& error) {
    auto j=parse(s,error); if (!j) return false;
    if (!cJSON_IsArray(j.get()) || cJSON_GetArraySize(j.get()) > 512) { error="Unsupported save list or excessive history"; return false; }
    std::vector<SaveInfo> result; const cJSON* item;
    cJSON_ArrayForEach(item,j.get()) {
        SaveInfo save; double size;
        if (!id(item,"id",save.id) || !number(item,"file_size_bytes",size,9007199254740991.0)) { error="Invalid save ID or size"; return false; }
        save.filename=str(item,"file_name"); save.size=static_cast<uint64_t>(size);
        save.slot=str(item,"slot",64); save.emulator=str(item,"emulator",64);
        save.archival=!cJSON_IsString(field(item,"slot"));
        save.hash=str(item,"content_hash",128); save.updated=str(item,"updated_at",64);
        result.push_back(save);
    }
    out=std::move(result); return true;
}
std::string romListPath(int platform,unsigned offset,const std::string& search) {
    return "/api/roms?platform_ids="+std::to_string(platform)+"&limit="+std::to_string(PageSize)+
        "&offset="+std::to_string(offset)+"&order_by=name&order_dir=asc&with_char_index=false&with_filter_values=false&with_rom_id_index=false&with_files=false&search_term="+urlEncode(search);
}
}
