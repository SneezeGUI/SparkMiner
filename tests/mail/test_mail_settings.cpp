#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <cassert>
#include <fstream>
#include <board_config.h>
#include "src/config/nvs_config.h"
#include "src/stats/local_settings.h"
#include "src/mail/mail_service.h"
TaskFunction_t test_task=nullptr;TestSerial Serial;TestESP ESP;TestWiFi WiFi;uint32_t test_now=100;
static miner_config_t stored;static mail::Config mailCfg,mailSaved;static bool failConfig=false,failMail=false;static int configSaves=0,mailSaves=0,testQueued=0,probeQueued=0;static bool touch=true;
bool nvs_config_load(miner_config_t*c){*c=stored;return true;}bool nvs_config_save(const miner_config_t*c){(void)c;++configSaves;return !failConfig;}
bool nvs_stats_store_totals(const advanced::Counters&){return true;}bool nvs_touch_enabled(){return touch;}bool nvs_touch_save(bool b){touch=b;return true;}bool nvs_factory_clear(){return true;}
bool mail_config_load(mail::Config&c){c=mailCfg;return true;}bool mail_config_save(const mail::Config&c){++mailSaves;if(failMail)return false;mailSaved=c;return true;}
bool mail_claim_settings(){return true;}void mail_release_settings(){}bool mail_clear_history(){return true;}
bool mail_queue_test(){++testQueued;return true;}bool mail_queue_probe(){++probeQueued;return true;}const char* mail_status(){return "Idle";}
int main(int argc,char**argv){
 std::string mode=argc>1?argv[1]:"save";strcpy(stored.wallet,"bc1qabcdefghijklmnopqrstuvwxyz0123456789");strcpy(stored.ssid,"Test");strcpy(stored.wifiPassword,"SECRET_WIFI_123");strcpy(stored.poolUrl,"solobtc.nmminer.com");stored.poolPort=3333;stored.brightness=50;stored.targetDifficulty=.0014;
 strcpy(mailCfg.user,"user@example.com");strcpy(mailCfg.from,"user@example.com");strcpy(mailCfg.to,"recipient@example.com");strcpy(mailCfg.password,"SECRET_SMTP_123");
 strcpy(stored.backupWallet,"bc1qabcdefghijklmnopqrstuvwxyz0123456789.testworker");strcpy(stored.workerName,"testworker");
 WebServer s(80);local_settings_register(s);s.request("/settings");assert(s.code==200&&s.body.find("SECRET_")==std::string::npos);assert(s.body.find("id='mail_password'")!=std::string::npos);assert(s.body.find("mail-probe")!=std::string::npos);assert(s.body.find("value='bc1qabcdefghijklmnopqrstuvwxyz0123456789.testworker'")==std::string::npos);std::ofstream(".test-output/mail-settings-fixture.html")<<s.body;
 std::string key="name='token' value='";auto start=s.body.find(key)+key.size();std::string token=s.body.substr(start,32);
 if(mode=="routes"){
  s.request("/settings/mail-test",HTTP_POST);assert(s.code==403&&testQueued==0);s.args["token"]=token;s.args["confirmation"]="WRONG";s.request("/settings/mail-test",HTTP_POST);assert(s.code==403);s.args["confirmation"]="TESTMAIL";s.requestHeaders["Origin"]="http://evil.test";s.request("/settings/mail-test",HTTP_POST);assert(s.code==403);s.requestHeaders["Origin"]="http://192.0.2.43";s.request("/settings/mail-test",HTTP_GET);assert(s.code==404);s.request("/settings/mail-test",HTTP_POST);assert(s.code==202&&testQueued==1);s.request("/settings/mail-probe",HTTP_POST);assert(s.code==202&&probeQueued==1);assert(configSaves==0&&mailSaves==0);return 0;
 }
 s.args={{"token",token},{"wallet",stored.wallet},{"worker","testworker"},{"rotation","1"},{"brightness","75"},{"timeout","30"},{"ssid","Test"},{"pool","solobtc.nmminer.com"},{"port","3333"},{"backup_pool",""},{"backup_wallet",""},{"backup_port","0"},{"difficulty","0.0014"},{"timezone","2"},{"display_on","1"},{"touch","1"},{"mail_present","1"},{"mail_enabled","1"},{"mail_host","smtp.gmail.com"},{"mail_user","user@example.com"},{"mail_from","user@example.com"},{"mail_to","recipient@example.com"},{"mail_port","465"},{"mail_hour","7"},{"mail_low","400"},{"mail_reject","5"},{"mail_daily","1"},{"mail_warnings","1"}};
 if(mode=="invalid"){
  const std::pair<const char*,const char*> bad[]={{"mail_host","smtp://gmail.com"},{"mail_to","victim@a.com\r\nBcc: bad@a.com"},{"mail_port","587"},{"mail_hour","24"},{"mail_low","0"},{"mail_reject","0"}};
  for(auto pair:bad){String old=s.args[pair.first];s.args[pair.first]=pair.second;s.request("/settings",HTTP_POST);assert(s.code==400&&mailSaves==0&&configSaves==0);s.args[pair.first]=old;}s.args["clear_mail_password"]="1";s.args["mail_password"]="new-pass";s.request("/settings",HTTP_POST);assert(s.code==400);return 0;
 }
 if(mode=="password")s.args["mail_password"]="abcd efgh ijkl mnop";
 if(mode=="rollback")failConfig=true;if(mode=="mailfail")failMail=true;
 s.request("/settings",HTTP_POST);assert(s.code==202);local_settings_process({});assert(testQueued==0&&probeQueued==0&&!ESP.restarted);
 if(failConfig){assert(mailSaves==2&&!strcmp(mailSaved.password,mailCfg.password));assert(!local_settings_blocks_checkpoint());return 0;}
 if(failMail){assert(configSaves==0&&mailSaves==1&&!local_settings_blocks_checkpoint());return 0;}
 assert(configSaves==1&&mailSaves==1&&mailSaved.enabled&&mailSaved.hour==7&&mailSaved.low_khs==400);assert(!strcmp(mailSaved.password,mode=="password"?"abcdefghijklmnop":"SECRET_SMTP_123"));
 puts("PASS: mail UI validation, secret preservation, explicit test route and save integration");
}
