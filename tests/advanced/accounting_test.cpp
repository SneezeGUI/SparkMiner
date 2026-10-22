#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../../include/advanced_statistics.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace advanced;
int main(int argc, char** argv) {
    Counters rates;
    assert(success_percent(rates) == -1);
    rates.submitted=100; assert(success_percent(rates) == -1);
    rates.accepted=1; assert(success_percent(rates) == 100);
    rates.accepted=0; rates.rejected=1; assert(success_percent(rates) == 0);
    rates.accepted=3; assert(success_percent(rates) == 75);
    rates.accepted=2; assert(success_percent(rates) == 66.67);
    rates.accepted=1; rates.rejected=UINT32_MAX; assert(success_percent(rates) == 0.0);
    rates.accepted=UINT32_MAX; rates.rejected=UINT32_MAX;
    assert(success_percent(rates) == 50); // Denominator must not overflow 32 bits.
    Counters boot; boot.hashes = 100; boot.accepted = 5; boot.uptime_s = 100;
    boot.best_difficulty = 10;
    Ledger ledger(boot); Counters session; session.hashes=10; session.accepted=1; session.uptime_s=10;
    Counters disk=boot;
    auto save=[&](const Counters& total){disk=total;return true;};
    assert(ledger.total(session).hashes==110);
    for(int i=0;i<10;++i) {
        assert(ledger.checkpoint(session,save));
        assert(disk.hashes==110 && ledger.total(session).hashes==110);
        assert(disk.uptime_s==110 && ledger.total(session).uptime_s==110);
    }
    session.hashes=30; session.accepted=3; session.uptime_s=30;
    assert(!ledger.checkpoint(session,[](const Counters&){return false;}));
    assert(disk.hashes==110 && disk.uptime_s==110);
    assert(ledger.total(session).hashes==130);
    assert(ledger.checkpoint(session,save));
    assert(disk.hashes==130 && disk.accepted==8 && disk.uptime_s==130);
    // Reboot: zero session, previous successfully persisted total as new baseline.
    Ledger reboot(disk); Counters next; next.hashes=7; next.accepted=1; next.uptime_s=2;
    assert(reboot.total(next).hashes==137 && reboot.total(next).uptime_s==132);
    assert(reboot.total(next).accepted==9 && reboot.total(next).best_difficulty==10);
    // Failed checkpoint followed by power loss retains the last successful checkpoint.
    next.hashes=99;
    assert(!reboot.checkpoint(next,[](const Counters&){return false;}));
    assert(Ledger(disk).total(Counters()).hashes==130);
    assert(add<uint32_t>(UINT32_MAX-1,3)==UINT32_MAX);
    assert(add<uint64_t>(UINT64_MAX-1,3)==UINT64_MAX);
    Counters huge; huge.hashes=UINT64_MAX; huge.accepted=UINT32_MAX;
    assert(ledger.total(huge).hashes==UINT64_MAX && limited(ledger.total(huge)));
    session.best_difficulty=std::numeric_limits<double>::quiet_NaN();
    assert(ledger.total(session).best_difficulty==10);
    SessionClock clock; clock.start(UINT32_MAX-499);
    assert(clock.tick(500)==1); assert(clock.tick(2500)==3);
    SavePolicy policy;
    assert(!policy.due(0,0)); assert(policy.due(1,1));
    policy.result(1,1,false); assert(!policy.due(30,1)); assert(policy.due(31,1));
    policy.result(31,1,true); assert(!policy.due(100,1)); assert(policy.due(300,1));
    policy.result(300,1,true); assert(!policy.due(3899,1)); assert(policy.due(3900,1));
    Rate rate; assert(rate.sample(900,1000)==0); assert(rate.sample(1900,2000)==1000);
    assert(rate.sample(2900,3000)==1000); assert(rate.sample(0,4000)==0);
    Snapshot snapshot; snapshot.session.hashes=9007199254740993ULL;
    snapshot.lifetime.hashes=UINT64_MAX; snapshot.sampled_ms=100; snapshot.sequence=42;
    snapshot.hashrate_hs=1000; char buf[1024];
    assert(json(snapshot,125,buf,sizeof buf));
    assert(std::strstr(buf,"\"success_percent\":null"));
    assert(std::strstr(buf,"9007199254740993")); assert(std::strstr(buf,"18446744073709551615"));
    assert(std::strstr(buf,"\"sample_age_ms\":\"25\""));
    assert(!json(snapshot,99,buf,sizeof buf));
    assert(!json(snapshot,125,buf,2) && buf[0]=='\0');
    snapshot.hashrate_hs=std::numeric_limits<double>::infinity();
    assert(!json(snapshot,125,buf,sizeof buf));
    if (argc == 2) {
        snapshot.boot_id=42; snapshot.hashrate_hs=1023456.75;
        snapshot.session.hashes=123456789;
        snapshot.session.accepted=24; snapshot.session.rejected=1; snapshot.session.submitted=26;
        snapshot.session.uptime_s=300;
        snapshot.lifetime.hashes=9007199254740993ULL;
        snapshot.lifetime.accepted=1024; snapshot.lifetime.rejected=12; snapshot.lifetime.submitted=1038;
        snapshot.lifetime.uptime_s=175620; snapshot.lifetime.best_difficulty=1234.5;
        snapshot.pool_connected=true; snapshot.wifi_connected=true;
        snapshot.save_attempted=true; snapshot.last_save_ok=true;
        assert(json(snapshot,125,buf,sizeof buf));
        FILE* file=std::fopen(argv[1], "w"); assert(file);
        assert(std::fputs(buf,file)>=0); assert(std::fputc('\n',file)!=EOF); assert(std::fclose(file)==0);
    }
    std::puts("Accounting regression scenarios passed");
}
