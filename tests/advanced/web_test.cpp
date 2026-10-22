#ifdef NDEBUG
#undef NDEBUG
#endif
#include <Arduino.h>
#include <WebServer.h>
#include <esp_timer.h>
#include "../../src/stats/advanced_web.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <thread>
#include <atomic>
TaskFunction_t test_task=nullptr;
TestSerial Serial;
WebServer* test_server=nullptr;
static int64_t now_ms=125;
int64_t esp_timer_get_time() {return now_ms*1000;}
int main() {
    assert(advanced_stats_web_start());assert(test_task);
    try {test_task(nullptr);} catch(const StopTask&) {}
    test_server->request("/");assert(test_server->code==200);
    assert(test_server->body.find("Advanced Statistics")!=std::string::npos);
    test_server->request("/api/advanced-stats");assert(test_server->code==503);
    assert(test_server->headers["Retry-After"]=="1");
    advanced::Snapshot value;value.sampled_ms=100;value.sequence=17;
    value.session.hashes=9007199254740993ULL;value.lifetime=value.session;
    advanced_stats_publish(value);
    test_server->request("/api/advanced-stats");assert(test_server->code==200);
    assert(test_server->headers["Cache-Control"]=="no-store");
    assert(test_server->body.find("\"sample_age_ms\":\"25\"")!=std::string::npos);
    assert(test_server->body.find("9007199254740993")!=std::string::npos);
    assert(test_server->body.find("password")==std::string::npos);
    test_server->request("/api/advanced-stats",HTTP_POST);assert(test_server->code==404);
    test_server->request("/config");assert(test_server->code==404);
    value.hashrate_hs=-1;advanced_stats_publish(value);
    test_server->request("/api/advanced-stats");assert(test_server->code==500);
    value.hashrate_hs=0;value.sequence=0;value.session.hashes=0;value.lifetime.hashes=0;advanced_stats_publish(value);
    // Exercise the production cache publication/copy path under concurrent access.
    std::thread producer([]{
        for(uint64_t i=1;i<=20000;++i) {
            advanced::Snapshot s;s.sequence=i;s.session.hashes=i;s.lifetime.hashes=i;s.sampled_ms=100;
            advanced_stats_publish(s);
            if ((i & 31) == 0) std::this_thread::yield();
        }
    });
    for(int i=0;i<5000;++i) {
        test_server->request("/api/advanced-stats");assert(test_server->code==200);
        const auto& b=test_server->body;
        const std::string key="\"sequence\":\"";
        auto begin=b.find(key)+key.size();auto end=b.find('"',begin);
        std::string number=b.substr(begin,end-begin);
        const std::string field="\"hashes\":\""+number+"\"";
        auto first=b.find(field);assert(first!=std::string::npos);
        assert(b.find(field,first+field.size())!=std::string::npos);
    }
    producer.join();
    std::puts("Production HTTP handlers and cache stress tests passed");
}
