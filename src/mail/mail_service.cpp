#ifdef ADVANCED_MAIL
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <esp_timer.h>
#include <time.h>
#include "mail_service.h"
#include "../mining/miner.h"
extern const uint8_t mail_ca_start[] asm("_binary_src_mail_x509_crt_bundle_start");
extern const char gmail_ca_start[] asm("_binary_src_mail_gmail_roots_pem_start");
namespace {
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
advanced::Snapshot latest;
bool ready=false,queued=false,probeQueued=false,busy=false,settingsPending=false;
TaskHandle_t task=nullptr;
const char* status="Mailversand ausgeschaltet oder noch nicht eingerichtet.";
using Persistent=mail::DeliveryState;

uint32_t checksum(const mail::Config&c){
 uint32_t hash=2166136261u;
 auto add=[&](const void*p,size_t n){const uint8_t*b=(const uint8_t*)p;while(n--)hash=(hash^*b++)*16777619u;};
 add(&c.version,sizeof(c.version));add(&c.enabled,sizeof(c.enabled));add(&c.daily,sizeof(c.daily));add(&c.warnings,sizeof(c.warnings));add(&c.hour,sizeof(c.hour));add(&c.reject_percent,sizeof(c.reject_percent));add(&c.port,sizeof(c.port));add(&c.low_khs,sizeof(c.low_khs));
 add(c.host,sizeof(c.host));add(c.user,sizeof(c.user));add(c.password,sizeof(c.password));add(c.from,sizeof(c.from));add(c.to,sizeof(c.to));return hash;
}
bool terminated(const mail::Config&c){return memchr(c.host,0,sizeof(c.host))&&memchr(c.user,0,sizeof(c.user))&&memchr(c.password,0,sizeof(c.password))&&memchr(c.from,0,sizeof(c.from))&&memchr(c.to,0,sizeof(c.to));}
void dropQueued(){portENTER_CRITICAL(&mux);queued=false;portEXIT_CRITICAL(&mux);}
void setStatus(const char*s){portENTER_CRITICAL(&mux);status=s;portEXIT_CRITICAL(&mux);}
bool stateStore(const Persistent&state){Preferences p;if(!p.begin("sparkminer",false))return false;bool ok=p.putBytes("mailstate",&state,sizeof(state))==sizeof(state);p.end();return ok;}
Persistent stateLoad(){Persistent state;Preferences p;if(p.begin("sparkminer",true)){if(p.getBytesLength("mailstate")==sizeof(state)){Persistent saved;if(p.getBytes("mailstate",&saved,sizeof(saved))==sizeof(saved)&&saved.magic==state.magic&&saved.report_attempts<=3&&saved.alert_attempts<=3)state=saved;}p.end();}return state;}
class Transport {
 WiFiClientSecure&client;uint64_t deadline;char buffer[512];size_t used=0;
 bool expired(){return uint64_t(esp_timer_get_time()/1000)>=deadline;}
public:
 Transport(WiFiClientSecure&c,uint64_t end):client(c),deadline(end){}
 bool flush(){if(!used)return true;if(expired())return false;size_t n=client.write((const uint8_t*)buffer,used);bool ok=n==used;used=0;return ok;}
 bool put(const char*s){for(;*s;++s){if(expired())return false;buffer[used++]=*s;if(used==sizeof(buffer)&&!flush())return false;}return true;}
 bool line(char*out,size_t capacity){
  if(!flush())return false;size_t n=0;uint64_t until=uint64_t(esp_timer_get_time()/1000)+5000;
  while(!expired()&&uint64_t(esp_timer_get_time()/1000)<until){
   if(!client.available()){if(!client.connected())return false;vTaskDelay(1);continue;}
   int x=client.read();if(x<0){if(!client.connected())return false;vTaskDelay(1);continue;}
   if(x=='\n'){out[n]=0;return true;}if(x=='\r')continue;
   if(x<32||x>126||n+1>=capacity)return false;out[n++]=char(x);
  }return false;
 }
};
void bodyFor(char*body,size_t capacity,const advanced::Snapshot&s,const Persistent&state,const char*kind,uint8_t alerts){
 char success[48],totalSuccess[48];auto percent=[](const advanced::Counters&c,char*out,size_t n){double p=advanced::success_percent(c);if(p<0)snprintf(out,n,"noch keine Poolantworten");else snprintf(out,n,"%.2f %%",p);};percent(s.session,success,sizeof(success));percent(s.lifetime,totalSuccess,sizeof(totalSuccess));
 int written=snprintf(body,capacity,"SparkMiner - %s\r\n\r\nHashrate: %.3f MH/s\r\nPool: %s\r\nWLAN: %s\r\n\r\nSitzung\r\nHashes: %llu\r\nShares gesendet: %lu\r\nAkzeptiert: %lu\r\nAbgelehnt: %lu\r\nErfolgsquote: %s\r\nLaufzeit: %lu Sekunden\r\n\r\nGesamt\r\nHashes: %llu\r\nShares gesendet: %lu\r\nAkzeptiert: %lu\r\nAbgelehnt: %lu\r\nErfolgsquote: %s\r\nLaufzeit: %lu Sekunden\r\n",kind,s.hashrate_hs/1e6,s.pool_connected?"verbunden":"getrennt",s.wifi_connected?"verbunden":"getrennt",(unsigned long long)s.session.hashes,(unsigned long)s.session.submitted,(unsigned long)s.session.accepted,(unsigned long)s.session.rejected,success,(unsigned long)s.session.uptime_s,(unsigned long long)s.lifetime.hashes,(unsigned long)s.lifetime.submitted,(unsigned long)s.lifetime.accepted,(unsigned long)s.lifetime.rejected,totalSuccess,(unsigned long)s.lifetime.uptime_s);
 if(written<0||size_t(written)>=capacity){body[0]=0;return;}
 size_t n=size_t(written);
 if(state.baseline&&s.lifetime.hashes>=state.last.hashes&&s.lifetime.accepted>=state.last.accepted&&s.lifetime.rejected>=state.last.rejected){n+=snprintf(body+n,capacity-n,"\r\nSeit letztem bestaetigten Tagesbericht\r\nHashes: %llu\r\nAkzeptiert: %lu\r\nAbgelehnt: %lu\r\n",(unsigned long long)(s.lifetime.hashes-state.last.hashes),(unsigned long)(s.lifetime.accepted-state.last.accepted),(unsigned long)(s.lifetime.rejected-state.last.rejected));}
 const char*notes[]={"Pool/WLAN seit mindestens 5 Minuten unterbrochen.","Hashrate seit mindestens 5 Minuten unter Grenzwert.","Ablehnungsquote im 10-Minuten-Fenster zu hoch (mindestens 20 Antworten).","Verbindung nach laengerer Unterbrechung wiederhergestellt."};
 for(unsigned i=0;i<4&&n<capacity;++i)if(alerts&(1<<i))n+=snprintf(body+n,capacity-n,"\r\nHinweis: %s\r\n",notes[i]);
 if(n>=capacity)body[0]=0;
}
mail::Result send(const mail::Config&cfg,const advanced::Snapshot&snapshot,const Persistent&state,bool test,bool report,uint8_t alerts,bool probe){
 if(!WiFi.isConnected()||time(nullptr)<1704067200){setStatus("WLAN oder gueltige Uhrzeit fehlt; keine Mail versendet.");return mail::Result::Network;}
 if(ESP.getFreeHeap()<60000||ESP.getMaxAllocHeap()<18000){setStatus("Zu wenig freier Speicher fuer sicheren TLS-Versand; bitte neu starten.");return mail::Result::Network;}
 if(!miner_mail_pause()){setStatus("Mining konnte nicht sicher pausiert werden; keine Mail versendet.");return mail::Result::Network;}
 // Destruction order matters: close TLS before allowing SHA mining to resume.
 struct Resume {~Resume(){miner_mail_resume();}} resume;
 WiFiClientSecure client;
 // Use full CA path building for Gmail's cross-signed server chain. The old
 // compact-bundle callback cannot anchor that chain at the trusted GTS root.
 // Both paths retain mandatory certificate, hostname and validity checks.
 if(strcasecmp(cfg.host,"smtp.gmail.com")==0)client.setCACert(gmail_ca_start);
 else client.setCACertBundle(mail_ca_start);
 client.setHandshakeTimeout(8);client.setTimeout(3);
 uint64_t end=uint64_t(esp_timer_get_time()/1000)+45000;
 if(!client.connect(cfg.host,cfg.port,5000)){client.stop();setStatus("SMTP-Verbindung oder Zertifikatspruefung fehlgeschlagen.");return mail::Result::Network;}
 if(probe){
  Transport io(client,end);bool good=mail::reply(io)==220&&io.put("EHLO esp32-miner.local\r\n")&&mail::reply(io)==250;
  io.put("QUIT\r\n");io.flush();client.stop();
  setStatus(good?"TLS-Zertifikat und SMTP-Verbindung erfolgreich geprueft; keine Mail versendet.":"TLS verbunden, SMTP-Begruessung fehlgeschlagen; keine Mail versendet.");
  return good?mail::Result::Accepted:mail::Result::Protocol;
 }
 char body[1800],date[80];bodyFor(body,sizeof(body),snapshot,state,test?"Testmail":report?"Tagesbericht":"Warnung",alerts);
 time_t now=time(nullptr);tm utc;gmtime_r(&now,&utc);strftime(date,sizeof(date),"%a, %d %b %Y %H:%M:%S +0000",&utc);
 mail::Result result=mail::Result::Protocol;
 if(body[0]){Transport io(client,end);result=mail::deliver(io,cfg,test?"SparkMiner Testmail":report?"SparkMiner Tagesbericht":"SparkMiner Warnung",body,date,esp_random());io.flush();}
 client.stop();
 switch(result){
 case mail::Result::Accepted:setStatus("SMTP-Server hat die Nachricht angenommen. Bitte Postfach pruefen.");break;
 case mail::Result::Uncertain:setStatus("Annahme unklar. Bitte Postfach pruefen; keine automatische Wiederholung dieser Nachricht.");break;
 case mail::Result::Authentication:setStatus("SMTP-Anmeldung fehlgeschlagen. Benutzername/App-Passwort pruefen.");break;
 case mail::Result::Rejected:setStatus("SMTP-Server hat Absender, Empfaenger oder Nachricht abgelehnt.");break;
 default:setStatus("SMTP-Antwort ungueltig oder Zeitlimit erreicht.");break;
 }
 return result;
}
void run(void*){
 mail::Config cfg;bool loaded=mail_config_load(cfg);Persistent state=stateLoad();mail::Alerts alerts;uint8_t pending=0;int64_t manualAt=0;
 if(!loaded)setStatus("Mail-Einstellungen konnten nicht geladen werden; Versand gesperrt.");
 else if(cfg.enabled)setStatus("Mailversand bereit. Warte auf Bericht, Warnung oder Testmail.");
 for(;;){
  advanced::Snapshot snapshot;bool available,test=false,probe=false;
  portENTER_CRITICAL(&mux);available=ready;snapshot=latest;if(queued){test=true;probe=probeQueued;}portEXIT_CRITICAL(&mux);
  time_t now=time(nullptr);tm local{};localtime_r(&now,&local);uint32_t day=(local.tm_year+1900)*10000+(local.tm_mon+1)*100+local.tm_mday;
  uint64_t ms=uint64_t(esp_timer_get_time()/1000);
  if(!available||ms<snapshot.sampled_ms||ms-snapshot.sampled_ms>5000||!loaded){if(test){dropQueued();setStatus("Statistik noch nicht bereit; bitte erneut testen.");}vTaskDelay(pdMS_TO_TICKS(1000));continue;}
  bool sane=now>=1704067200;
  if(sane)state.day(day);
  if(cfg.enabled&&cfg.warnings)pending|=alerts.evaluate(cfg,snapshot,ms);
  bool report=sane&&state.report_due(cfg,day,local.tm_hour,now);
  bool warning=sane&&state.alert_due(cfg,pending,now);
  if(test&&now<manualAt){dropQueued();setStatus("Bitte vor der naechsten Testmail eine Minute warten.");test=false;}
  if(!test&&(!report&&!warning)){vTaskDelay(pdMS_TO_TICKS(1000));continue;}
  if(!snapshot.wifi_connected||!sane){if(test){dropQueued();setStatus("WLAN oder gueltige Uhrzeit fehlt; keine Testmail versendet.");}vTaskDelay(pdMS_TO_TICKS(1000));continue;}
  if(!mail::complete(cfg)&&!(probe&&mail::host(cfg.host))){if(test)dropQueued();setStatus("Mail-Einstellungen unvollstaendig; bitte speichern und erneut pruefen.");vTaskDelay(pdMS_TO_TICKS(1000));continue;}
  portENTER_CRITICAL(&mux);bool allowed=!settingsPending&&!busy;if(allowed){busy=true;if(test)queued=false;}portEXIT_CRITICAL(&mux);
  if(!allowed){vTaskDelay(pdMS_TO_TICKS(1000));continue;}
  // Persist attempts before network I/O. Reboots must not cause mail storms.
  if(test)manualAt=now+60;
  else state.reserve(report,now);
  if(!test&&!stateStore(state)){portENTER_CRITICAL(&mux);busy=false;portEXIT_CRITICAL(&mux);cfg.enabled=false;setStatus("Versandstatus konnte nicht gespeichert werden; automatischer Versand gesperrt.");vTaskDelay(pdMS_TO_TICKS(10000));continue;}
  setStatus("Nachricht wird versendet; Mining pausiert kurz.");
  auto result=send(cfg,snapshot,state,test,report,pending,probe);now=time(nullptr);
  if(result==mail::Result::Accepted||result==mail::Result::Uncertain){
   if(!test){state.complete(result,report,warning,day,now,snapshot.lifetime);if(warning)pending=0;}
   if(!test&&!stateStore(state)){
    // Keep the in-memory suppression. Persistent pre-attempt cooldown is still in force.
    cfg.enabled=false;setStatus("Nachricht angenommen/unklar, Statusspeichern fehlgeschlagen; Automatik bis Neustart gesperrt.");
   }
  }
  alerts.after_mail(uint64_t(esp_timer_get_time()/1000));portENTER_CRITICAL(&mux);busy=false;portEXIT_CRITICAL(&mux);
  vTaskDelay(pdMS_TO_TICKS(1000));
 }
}
}
bool mail_config_load(mail::Config&c){
 c=mail::Config{};Preferences p;if(!p.begin("sparkminer",true))return false;
 size_t n=p.getBytesLength("mailcfg");if(!n){p.end();return true;}if(n!=sizeof(c)||p.getBytes("mailcfg",&c,sizeof(c))!=sizeof(c)){p.end();c=mail::Config{};return false;}p.end();
 if(!terminated(c)||c.checksum!=checksum(c)||!mail::valid(c)){c=mail::Config{};return false;}return true;
}
bool mail_config_save(const mail::Config&cfg){if(!mail::valid(cfg))return false;mail::Config c=cfg;c.checksum=checksum(c);Preferences p;if(!p.begin("sparkminer",false))return false;bool ok=p.putBytes("mailcfg",&c,sizeof(c))==sizeof(c);p.end();return ok;}
void mail_publish(const advanced::Snapshot&s){portENTER_CRITICAL(&mux);latest=s;ready=true;portEXIT_CRITICAL(&mux);}
bool mail_queue_test(){portENTER_CRITICAL(&mux);bool ok=task&&!busy&&!queued&&!settingsPending;if(ok){queued=true;probeQueued=false;}portEXIT_CRITICAL(&mux);if(ok)setStatus("Testmail angefordert. Gespeicherte Einstellungen werden verwendet.");return ok;}
bool mail_queue_probe(){portENTER_CRITICAL(&mux);bool ok=task&&!busy&&!queued&&!settingsPending;if(ok){queued=true;probeQueued=true;}portEXIT_CRITICAL(&mux);if(ok)setStatus("SMTP-Verbindungspruefung angefordert; keine Mail wird versendet.");return ok;}
bool mail_claim_settings(){portENTER_CRITICAL(&mux);bool ok=!busy&&!queued&&!settingsPending;if(ok)settingsPending=true;portEXIT_CRITICAL(&mux);return ok;}
void mail_release_settings(){portENTER_CRITICAL(&mux);settingsPending=false;portEXIT_CRITICAL(&mux);}
bool mail_clear_history(){Preferences p;if(!p.begin("sparkminer",false))return false;bool ok=!p.isKey("mailstate")||p.remove("mailstate");p.end();return ok;}
const char* mail_status(){portENTER_CRITICAL(&mux);const char*s=status;portEXIT_CRITICAL(&mux);return s;}
bool mail_start(){if(task)return true;return xTaskCreatePinnedToCore(run,"Mail",12288,nullptr,1,&task,0)==pdPASS;}
#endif
