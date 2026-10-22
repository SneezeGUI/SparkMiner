#pragma once
#include <advanced_statistics.hpp>
#include <cstdio>
namespace advanced {
struct MiningPanel { char hashes[24],submitted[16],accepted[16],rejected[16],uptime[32],success[16]; };
inline MiningPanel mining_panel(const Counters& c) {
 MiningPanel p={};
 std::snprintf(p.hashes,sizeof(p.hashes),"%llu",static_cast<unsigned long long>(c.hashes));
 std::snprintf(p.submitted,sizeof(p.submitted),"%u",static_cast<unsigned>(c.submitted));
 std::snprintf(p.accepted,sizeof(p.accepted),"%u",static_cast<unsigned>(c.accepted));
 std::snprintf(p.rejected,sizeof(p.rejected),"%u",static_cast<unsigned>(c.rejected));
 std::snprintf(p.uptime,sizeof(p.uptime),"%ud %02u:%02u:%02u",unsigned(c.uptime_s/86400),unsigned(c.uptime_s/3600%24),unsigned(c.uptime_s/60%60),unsigned(c.uptime_s%60));
 double rate=success_percent(c);
 if(rate<0)std::snprintf(p.success,sizeof(p.success),"--");else std::snprintf(p.success,sizeof(p.success),"%.2f%%",rate);
 return p;
}
}
