#include <mail_core.hpp>
#include <mining_mail_gate.hpp>
#include <cassert>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
struct IO {
 std::vector<std::string> lines;size_t at=0;std::string written;
 bool put(const char*s){written+=s;return true;}
 bool line(char*out,size_t cap){if(at>=lines.size()||lines[at].size()>=cap)return false;strcpy(out,lines[at++].c_str());return true;}
};
static IO server(){IO io;io.lines={"220 smtp.test","250-smtp.test","250-AUTH LOGIN PLAIN","250 SIZE 123","334 Username","334 Password","235 Authenticated","250 Sender","250 Recipient","354 Data","250 Accepted"};return io;}
static mail::Config config(){mail::Config c;c.enabled=true;strcpy(c.user,"user@example.com");strcpy(c.password,"test-secret");strcpy(c.from,"user@example.com");strcpy(c.to,"recipient@example.com");return c;}
int main(){
 auto c=config();assert(mail::complete(c)&&mail::valid(c));auto disabled=mail::Config{};assert(mail::valid(disabled)&&!mail::complete(disabled));
 char b[180];assert(mail::base64("foobar",b,sizeof(b))&&!strcmp(b,"Zm9vYmFy"));assert(mail::base64("f",b,sizeof(b))&&!strcmp(b,"Zg=="));assert(mail::base64("fo",b,sizeof(b))&&!strcmp(b,"Zm8="));assert(!mail::base64("foo",b,4));
 assert(!mail::address("a@example.com\r\nBcc: x@bad.com"));assert(!mail::address("two@a.com,other@b.com"));assert(!mail::host("smtp://gmail.com"));c.port=587;assert(!mail::valid(c));c=config();strcpy(c.password,"bad\nsecret");assert(!mail::valid(c));c=config();
 IO ok=server();assert(mail::deliver(ok,c,"Subject","Hello\r\n.hidden\r\n","Wed, 21 Oct 2026 20:00:00 +0000",42)==mail::Result::Accepted);assert(ok.written.find("\r\n..hidden\r\n")!=std::string::npos);assert(ok.written.find("QUIT\r\n")!=std::string::npos);assert(ok.written.find("test-secret")==std::string::npos);
 IO bad=server();bad.lines[6]="535 Authentication failed";assert(mail::deliver(bad,c,"Subject","Body","Date",1)==mail::Result::Authentication);assert(bad.written.find("MAIL FROM")==std::string::npos);
 bad=server();bad.lines[8]="550 Recipient refused";assert(mail::deliver(bad,c,"Subject","Body","Date",1)==mail::Result::Rejected);assert(bad.written.find("DATA\r\n")==std::string::npos);
 bad=server();bad.lines.pop_back();assert(mail::deliver(bad,c,"Subject","Body","Date",1)==mail::Result::Uncertain);
 bad=server();bad.lines[3]="550 Wrong multiline status";assert(mail::deliver(bad,c,"Subject","Body","Date",1)==mail::Result::Protocol);assert(bad.written.find("AUTH LOGIN")==std::string::npos);
 bad=server();bad.lines[2]="250-STARTTLS";assert(mail::deliver(bad,c,"Subject","Body","Date",1)==mail::Result::Protocol);assert(bad.written.find("AUTH LOGIN")==std::string::npos);
 bad=server();bad.lines[0]=std::string(600,'x');assert(mail::deliver(bad,c,"Subject","Body","Date",1)==mail::Result::Protocol);
 mail::DeliveryState state;state.day(20261021);assert(!state.report_due(c,20261021,19,1000));assert(state.report_due(c,20261021,20,1000));state.reserve(true,1000);assert(!state.report_due(c,20261021,20,1599));assert(state.report_due(c,20261021,20,1600));
 state.reserve(true,1600);state.reserve(true,2200);assert(!state.report_due(c,20261021,20,9999));state.day(20261022);assert(state.report_due(c,20261022,20,9999));advanced::Counters counts;counts.hashes=1234;state.complete(mail::Result::Accepted,true,false,20261022,9999,counts);assert(state.baseline&&state.last.hashes==1234);auto reboot=state;assert(!reboot.report_due(c,20261022,23,99999));state.day(20261023);state.complete(mail::Result::Uncertain,true,false,20261023,99999,{});assert(!state.report_due(c,20261023,23,999999)&&state.last.hashes==1234);
 state.complete(mail::Result::Accepted,false,true,20261023,100000,{});assert(!state.alert_due(c,1,103599));assert(state.alert_due(c,1,103600));
 advanced::Snapshot snap;snap.session.uptime_s=300;mail::Alerts alerts;assert(alerts.evaluate(c,snap,0)==0);assert(alerts.evaluate(c,snap,299999)==0);assert(alerts.evaluate(c,snap,300000)==1);assert(alerts.evaluate(c,snap,301000)==0);snap.pool_connected=snap.wifi_connected=true;snap.hashrate_hs=920000;assert(alerts.evaluate(c,snap,302000)==8);
 mail::Alerts low;snap.hashrate_hs=100;assert(low.evaluate(c,snap,0)==0);assert(low.evaluate(c,snap,300000)==2);low.after_mail(301000);assert(low.evaluate(c,snap,320000)==0);assert(low.evaluate(c,snap,361000)==0);
 mail::Alerts rejected;snap.hashrate_hs=920000;snap.session.accepted=snap.session.rejected=0;assert(rejected.evaluate(c,snap,0)==0);snap.session.accepted=19;snap.session.rejected=1;assert(rejected.evaluate(c,snap,600000)==4);
 mail::MiningGate gate;assert(gate.claim()&&!gate.safe());assert(gate.request()&&!gate.safe()&&!gate.claim());gate.park();assert(gate.safe());gate.resume();gate.leave_park();assert(!gate.safe());gate.finish();assert(gate.safe());assert(gate.request()&&!gate.claim()&&gate.safe());gate.resume();assert(gate.claim());gate.finish();
 puts("PASS: SMTP success/auth/recipient/unknown-ack/protocol, credential exclusion, injection rejection, report/retry/reboot policy, warning timing and exclusive mining gate");
}
