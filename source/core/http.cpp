#include "retrosync/api.hpp"
#include <curl/curl.h>

namespace rs {
struct Transfer { HttpResponse* response; std::atomic<bool>* cancel; std::atomic<unsigned>* received; bool capped=false; };
static size_t receive(char* data,size_t size,size_t count,void* user) {
    auto& t=*static_cast<Transfer*>(user);
    if (t.cancel->load()) return 0;
    if (size && count > MaxJsonBytes/size) { t.capped=true; return 0; }
    size_t n=size*count;
    if (n > MaxJsonBytes-t.response->body.size()) { t.capped=true; return 0; }
    t.response->body.append(data,n); t.received->store(static_cast<unsigned>(t.response->body.size())); return n;
}
static int progress(void* user,curl_off_t,curl_off_t,curl_off_t,curl_off_t) {
    return static_cast<Transfer*>(user)->cancel->load() ? 1 : 0;
}
HttpResponse getJson(const Settings& s,const std::string& path,const std::string& ca,
                     std::atomic<bool>& cancel,std::atomic<unsigned>& received) {
    HttpResponse response;
    if (!validateSettings(s,response.error)) return response;
    if (path.compare(0,5,"/api/") != 0 || path.find_first_of("\r\n") != std::string::npos) { response.error="Invalid API path"; return response; }
    CURL* curl=curl_easy_init();
    if (!curl) { response.error="Cannot allocate HTTP client"; return response; }
    Transfer t{&response,&cancel,&received,false};
    std::string auth="Authorization: Bearer "+s.token, url=s.server+path;
    curl_slist* headers=curl_slist_append(nullptr,auth.c_str());
    if (!headers) { curl_easy_cleanup(curl); response.error="Cannot allocate authorization header"; return response; }
    curl_easy_setopt(curl,CURLOPT_URL,url.c_str());
    curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers);
    curl_easy_setopt(curl,CURLOPT_USERAGENT,"RetroSync3DS/0.1.0");
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl,CURLOPT_PROTOCOLS_STR,"http,https");
#else
    curl_easy_setopt(curl,CURLOPT_PROTOCOLS,static_cast<long>(CURLPROTO_HTTP|CURLPROTO_HTTPS));
#endif
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,0L);
    curl_easy_setopt(curl,CURLOPT_PROXY,"");
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);
    if (!ca.empty()) curl_easy_setopt(curl,CURLOPT_CAINFO,ca.c_str());
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,8L);
    curl_easy_setopt(curl,CURLOPT_TIMEOUT,25L);
    curl_easy_setopt(curl,CURLOPT_LOW_SPEED_LIMIT,128L);
    curl_easy_setopt(curl,CURLOPT_LOW_SPEED_TIME,10L);
    curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(curl,CURLOPT_NOPROGRESS,0L);
    curl_easy_setopt(curl,CURLOPT_XFERINFOFUNCTION,progress);
    curl_easy_setopt(curl,CURLOPT_XFERINFODATA,&t);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,receive);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,&t);
    CURLcode result=curl_easy_perform(curl);
    curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&response.status);
    // Never include a response body, request URL, header or curl error buffer in errors/logs.
    if (cancel.load()) response.error="Request cancelled";
    else if (t.capped) response.error="Response exceeds 512 KiB; reduce server metadata";
    else if (result == CURLE_PEER_FAILED_VERIFICATION) response.error="TLS certificate verification failed; check CA, hostname and console clock";
    else if (result == CURLE_SSL_CACERT_BADFILE) response.error="TLS CA file missing or invalid";
    else if (result != CURLE_OK) response.error=std::string("Network: ")+curl_easy_strerror(result);
    else if (response.status == 401) response.error="Authentication failed; check client API token";
    else if (response.status == 403) response.error="Permission denied; check token scopes and account policy";
    else if (response.status == 404 || response.status == 405) response.error="Endpoint unavailable on this server/version";
    else if (response.status >= 300 && response.status < 400) response.error="Redirect refused; configure the final server URL";
    else if (response.status != 200) response.error="Server returned HTTP "+std::to_string(response.status);
    curl_slist_free_all(headers); curl_easy_cleanup(curl);
    if (!response.error.empty()) response.body.clear();
    return response;
}
}
