#pragma once
// All calls occur under the mining-state critical section.
namespace mail {
struct MiningGate {
 bool requested=false,working=false,parked=false;
 bool claim(){if(requested)return false;working=true;parked=false;return true;}
 bool request(){if(requested)return false;requested=true;return true;}
 bool safe()const{return !working||parked;}
 void park(){parked=true;}
 void leave_park(){parked=false;}
 void finish(){working=false;parked=false;}
 void resume(){requested=false;}
};
}
