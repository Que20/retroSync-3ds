#include "retrosync/api.hpp"
#include <curl/curl.h>
#include <chrono>
#include <cstdio>
#include <thread>
int main(int argc,char** argv) {
    if (argc<3) return 2;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    rs::Settings s; s.server=argv[1]; s.token="rmm_mock_token"; s.allowHttp=true;
    std::atomic<bool> cancel{false}; std::atomic<unsigned> received{0};
    std::thread timer;
    if (argc>4) timer=std::thread([&] { std::this_thread::sleep_for(std::chrono::milliseconds(100)); cancel.store(true); });
    auto response=rs::getJson(s,argv[2],argc>3?argv[3]:"",cancel,received);
    if (timer.joinable()) timer.join();
    std::printf("%ld\n%s\n%s",response.status,response.error.c_str(),response.body.c_str());
    curl_global_cleanup();
}
