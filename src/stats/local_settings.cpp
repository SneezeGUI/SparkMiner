#include <board_config.h>
#ifdef ADVANCED_LOCAL_CONTROLS
#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>
#include <ui_controls.hpp>
#include <pool_address.hpp>
#include "settings_script.h"
#include "local_settings.h"
#ifdef ADVANCED_MAIL
#include "../mail/mail_service.h"
#endif
#include "../config/nvs_config.h"
namespace {
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
miner_config_t pending;
bool pendingTouch=true;
#ifdef ADVANCED_MAIL
mail::Config pendingMail;
#endif
enum Action { CONFIG, RESTART, STATS_RESET, FACTORY };
Action pendingAction=CONFIG,restartAction=CONFIG;
enum State { IDLE, QUEUED, SAVING, SAVED, FAILED };
State state=IDLE;
uint32_t restartAt=0;
char token[33];
String escape(const char* text) {
 String out; for(;*text;++text) switch(*text){case '&':out+="&amp;";break;case '<':out+="&lt;";break;case '>':out+="&gt;";break;case '"':out+="&quot;";break;case '\'':out+="&#39;";break;default:out+=*text;}return out;
}
State getState(){portENTER_CRITICAL(&mux);State s=state;portEXIT_CRITICAL(&mux);return s;}
void setState(State s){portENTER_CRITICAL(&mux);state=s;portEXIT_CRITICAL(&mux);}
void headers(WebServer& s){s.sendHeader("Cache-Control","no-store");s.sendHeader("X-Frame-Options","DENY");s.sendHeader("X-Content-Type-Options","nosniff");s.sendHeader("Content-Security-Policy","default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'");}
bool localHost(WebServer& s){String host=WiFi.localIP().toString();return s.hostHeader()==host || s.hostHeader()==host+":80";}
void reject(WebServer& s,int code,const char* msg){headers(s);s.send(code,"text/plain; charset=utf-8",msg);}
String pageStart(){return "<!doctype html><html lang='de'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Einstellungen</title><style>body{font:16px system-ui;background:#101820;color:#edf3f7;max-width:700px;margin:24px auto;padding:16px}a{color:#67dfc0}label{display:block;margin:18px 0 6px}input,select,button{box-sizing:border-box;max-width:100%;width:100%;padding:11px;font:inherit;border-radius:8px}input[type=checkbox]{width:auto;margin-right:8px}button{margin:16px 0;background:#67dfc0}p{line-height:1.5}.password{display:flex;gap:8px}.password input{min-width:0;flex:1}.password button{width:auto;margin:0;flex:0 0 auto}.group{padding:16px;border:1px solid #465d70;border-radius:12px;margin:20px 0}.danger{background:#ffd08c}small{display:block;color:#b9c8d3;line-height:1.5}h2{font-size:1.2rem}</style><a href='/'>Zur Statistik</a><h1>Einstellungen</h1>";}
void input(String& page,const char* name,const char* label,const char* value,const char* attrs=""){
 page+="<label for='";page+=name;page+="'>";page+=label;page+="</label><input id='";page+=name;page+="' name='";page+=name;page+="' ";page+=attrs;page+=" value='";page+=escape(value);page+="'>";
}
void check(String& page,const char* name,const char* label,bool value){page+="<label><input type='checkbox' name='";page+=name;page+="' value='1'";if(value)page+=" checked";page+=">";page+=label;page+="</label>";}
void password(String& page,const char* name,const char* label,bool stored,unsigned maximum){
 page+="<label for='";page+=name;page+="'>";page+=label;page+="</label><div class='password'><input type='password' data-secret='yes' autocomplete='new-password' id='";page+=name;page+="' name='";page+=name;page+="' maxlength='";page+=String(maximum);page+="' value=''><button type='button' data-password='";page+=name;page+="' aria-controls='";page+=name;page+="' aria-pressed='false'>Anzeigen</button></div><small>";page+=stored?"Kennwort gespeichert. ":"Kein Kennwort gespeichert. ";page+="Leer lassen = unverändert. Anzeigen zeigt nur deine neue Eingabe.</small>";
 String clear="clear_";clear+=name;check(page,clear.c_str(),"Gespeichertes Kennwort ausdrücklich entfernen",false);
}
#ifdef ADVANCED_MAIL
void mailFields(String&page,const mail::Config&cfg){
 page+="<section class='group'><h2>E-Mail</h2><input type='hidden' name='mail_present' value='1'>";
 check(page,"mail_enabled","Automatischen Mailversand aktivieren",cfg.enabled);
 input(page,"mail_host","SMTP-Server",cfg.host,"maxlength='80' autocomplete='off'");
 input(page,"mail_port","SMTP-Port (TLS)","465","type='number' min='465' max='465' readonly");
 input(page,"mail_user","SMTP-Benutzername",cfg.user,"maxlength='128' autocomplete='off'");
 password(page,"mail_password","SMTP-/App-Passwort",cfg.password[0],128);
 input(page,"mail_from","Absenderadresse",cfg.from,"type='email' maxlength='128'");
 input(page,"mail_to","Empfaengeradresse",cfg.to,"type='email' maxlength='128'");
 check(page,"mail_daily","Taeglichen Statistikbericht senden",cfg.daily);
 input(page,"mail_hour","Bericht um volle Stunde (0–23, eingestellter UTC-Abstand)",String(cfg.hour).c_str(),"type='number' min='0' max='23' required");
 check(page,"mail_warnings","Warnungen senden",cfg.warnings);
 input(page,"mail_low","Warnung unter Hashrate (kH/s, fuer mindestens 5 Minuten)",String(cfg.low_khs).c_str(),"type='number' min='1' max='2000' required");
 input(page,"mail_reject","Warnung ab Ablehnungsquote (%)",String(cfg.reject_percent).c_str(),"type='number' min='1' max='100' required");
 page+="<small>Verschluesseltes SMTP mit Zertifikatspruefung auf Port 465 und Anmeldung per Benutzername/Passwort. Fuer Gmail: smtp.gmail.com, Benutzername und Absender = vollstaendige Gmail-Adresse, Google-App-Passwort verwenden (nicht das normale Google-Passwort; Bestaetigung in zwei Schritten muss aktiviert sein). OAuth und STARTTLS auf Port 587 sind hier nicht enthalten. Nach Aktivierung nach der gewaehlten Uhrzeit wird der heutige Bericht nachgeholt. Tagesberichte enthalten Sitzung, Gesamtwerte und den Zuwachs seit dem letzten bestaetigten Tagesbericht. Warnungen: Pool/WLAN laenger als 5 Minuten unterbrochen, niedrige Hashrate oder zu viele abgelehnte Shares im 10-Minuten-Fenster (mindestens 20 Antworten). WLAN-Ausfaelle koennen erst nach Wiederverbindung gemeldet werden. Automatische Fehlversuche werden begrenzt. Beim Versand pausiert das Mining kurz.</small></section>";
}
#endif
void get(WebServer& s){
 if(!localHost(s)){reject(s,403,"Bitte die IP-Adresse des Miners verwenden.");return;}
 miner_config_t cfg;if(!nvs_config_load(&cfg)){reject(s,503,"Einstellungen konnten nicht geladen werden.");return;}
 String wallet=cfg.wallet,worker=cfg.workerName;
 int dot=wallet.indexOf('.');if(dot>0&&wallet.indexOf('.',dot+1)<0){String suffix=wallet.substring(dot+1);if(worker.length()==0||worker==suffix){worker=suffix;wallet=wallet.substring(0,dot);}}
 String backupWallet=cfg.backupWallet;
 int backupDot=backupWallet.indexOf('.');
 if(backupDot>0&&backupWallet.indexOf('.',backupDot+1)<0&&backupWallet.substring(backupDot+1)==worker)backupWallet=backupWallet.substring(0,backupDot);
 String page=pageStart();page+="<p>Speichern übernimmt deine Änderungen, sichert die Statistik und startet den Miner neu. Neue WLAN-Daten können die erreichbare IP-Adresse ändern.</p>";
 if(getState()==FAILED)page+="<p role='alert'>Speichern fehlgeschlagen. Kein Neustart ausgelöst. Bitte erneut prüfen.</p>";
 page+="<form id='settingsform' method='post' action='/settings'><input type='hidden' name='token' value='";page+=token;page+="'><section class='group'><h2>WLAN</h2>";
 input(page,"ssid","WLAN-Name",cfg.ssid,"required maxlength='32' autocomplete='off'");password(page,"wifi_password","WLAN-Kennwort",cfg.wifiPassword[0],64);
 page+="<small>Für ein offenes WLAN das Entfernen des Kennworts ausdrücklich auswählen. WPA-Kennwörter: 8–63 Zeichen oder 64 Hex-Zeichen.</small></section><section class='group'><h2>Hauptpool</h2>";
 input(page,"pool","Pool-Adresse",cfg.poolUrl,"required maxlength='80'");input(page,"port","Port",String(cfg.poolPort).c_str(),"type='number' min='1' max='65535' required");
 input(page,"wallet","BTC-Empfangsadresse (ohne .Worker)",wallet.c_str(),"required minlength='26' maxlength='120'");input(page,"worker","Workername",worker.c_str(),"maxlength='31' pattern='[A-Za-z0-9_-]*'");
 password(page,"pool_password","Pool-Kennwort",cfg.poolPassword[0],64);
 page+="<small>Pooladresse als Hostname oder mit stratum+tcp://, Port im eigenen Feld. Verschlüsselte Stratum-Verbindungen werden noch nicht unterstützt.</small></section><section class='group'><h2>Ersatzpool</h2>";
 check(page,"backup_enabled","Ersatzpool verwenden",cfg.backupWallet[0]&&cfg.backupPoolUrl[0]&&cfg.backupPoolPort);
 input(page,"backup_pool","Ersatzpool-Adresse",cfg.backupPoolUrl,"maxlength='80'");input(page,"backup_port","Ersatzpool-Port",String(cfg.backupPoolPort).c_str(),"type='number' min='0' max='65535' required");input(page,"backup_wallet","BTC-Adresse am Ersatzpool",backupWallet.c_str(),"maxlength='120'");password(page,"backup_password","Ersatzpool-Kennwort",cfg.backupPoolPassword[0],64);
 page+="<small>Der Workername gilt für beide Pools. Beim Abschalten wird die Ersatz-Wallet geleert; Hauptpool und Kennwörter bleiben erhalten.</small></section><section class='group'><h2>Anzeige und Touch</h2>";
 page+="<label for='rotation'>Bildausrichtung</label><select id='rotation' name='rotation'>";
 const char* labels[]={"0 – Hochformat","1 – Querformat","2 – Hochformat gedreht","3 – Querformat gedreht"};for(int i=0;i<4;++i){page+="<option value='";page+=String(i);page+="'";if(cfg.rotation==i)page+=" selected";page+=">";page+=labels[i];page+="</option>";}page+="</select>";
 input(page,"brightness","Helligkeit (1–100 %)",String(cfg.brightness?cfg.brightness:1).c_str(),"type='number' min='1' max='100' required");input(page,"timeout","Display abschalten nach Sekunden (0 = immer an)",String(cfg.screenTimeout).c_str(),"type='number' min='0' max='65535' required");
 check(page,"display_on","Display beim Start einschalten",cfg.displayEnabled);check(page,"invert","Farben invertieren",cfg.invertColors);check(page,"touch","Touch-Bedienung aktiv",nvs_touch_enabled());
 page+="<small>Antippen weckt auf oder wechselt die Statistikseite. Der BOOT-Taster bleibt auch bei abgeschaltetem Touch verfügbar.</small></section><section class='group'><h2>Mining und Uhrzeit</h2>";
 char difficulty[32];snprintf(difficulty,sizeof(difficulty),"%.10g",cfg.targetDifficulty);input(page,"difficulty","Gewünschte Share-Schwierigkeit",difficulty,"type='number' min='0.000000001' max='1000000' step='any' required");
 page+="<small>Wird dem Pool vorgeschlagen; der Pool bestimmt die tatsächlich verwendete Schwierigkeit.</small><label for='timezone'>Uhrzeit: UTC-Abstand</label><select id='timezone' name='timezone'>";
 for(int i=-12;i<=14;++i){page+="<option value='";page+=String(i);page+="'";if(cfg.timezoneOffset==i)page+=" selected";page+=">UTC";if(i>=0)page+="+";page+=String(i);page+="</option>";}page+="</select><small>Fester Abstand. Für Deutschland UTC+1 im Winter, UTC+2 im Sommer; keine automatische Sommerzeitumschaltung.</small></section>";
#ifdef ADVANCED_MAIL
 mail::Config mailCfg;bool mailLoaded=mail_config_load(mailCfg);if(!mailLoaded)page+="<p role='alert'>Mail-Einstellungen konnten nicht geladen werden. Bitte neu einrichten.</p>";
 mailFields(page,mailCfg);
#endif
 page+="<button>Speichern und neu starten</button></form>";
 page+="<section class='group'><h2>Einstellungen sichern</h2><p>Export und Import enthalten keine Kennwörter. Der Import füllt das Formular; erst Speichern übernimmt die Werte.</p><button type='button' id='export'>Einstellungen herunterladen</button><label for='restore'>Einstellungsdatei laden</label><input type='file' id='restore' accept='.json,application/json'><p id='filemessage' role='status'></p></section>";
 page+="<section class='group'><h2>Geräteaktionen</h2><p>Jede Aktion braucht eine ausdrückliche Bestätigung. Neustart sichert die Statistik. Statistik löschen entfernt nur die Zähler; Werksreset entfernt Einstellungen und Statistik.</p><form id='actionform' method='post' action='/settings/action'><input type='hidden' name='token' value='";page+=token;page+="'><input type='hidden' name='action'><input type='hidden' name='confirmation'></form><button type='button' data-action='restart' data-question='Miner neu starten?'>Neustart</button><button type='button' class='danger' data-action='stats_reset' data-question='Alle gespeicherten Miningzähler löschen?'>Statistik löschen</button><button type='button' class='danger' data-action='factory' data-question='Alle Einstellungen und Miningzähler löschen?'>Werksreset</button></section><p>Externe Markt-/Netzwerkdaten bleiben ausgeschaltet. E-Mail-Berichte werden direkt vom Miner versendet, wenn eingerichtet.</p>";
#ifdef ADVANCED_MAIL
 page+="<section class='group'><h2>Mail pruefen</h2><p>Bitte neue Mail-Einstellungen zuerst oben speichern. Die Testmail verwendet ausschliesslich die gespeicherten Daten und geht an den gespeicherten Empfaenger, auch wenn die Automatik abgeschaltet ist.</p><button type='button' id='mail-probe'>SMTP-Verbindung pruefen (ohne Mail)</button><button type='button' id='mail-test'>Testmail senden</button><p id='mail-status' role='status'>";page+=escape(mail_status());page+="</p></section>";
#endif
 page+=SETTINGS_SCRIPT;page+="</html>";headers(s);s.send(200,"text/html; charset=utf-8",page);
}
bool checked(WebServer& s,const char* name){return s.arg(name)=="1";}
bool safeText(const String& value,unsigned maximum,bool empty=true){return value.length()==strlen(value.c_str())&&advanced::text_field(value.c_str(),maximum,empty);}
bool updatePassword(WebServer& s,const char* name,char* destination,unsigned capacity,bool wifi=false){
 String value=s.arg(name);String clear="clear_";clear+=name;
 bool remove=checked(s,clear.c_str());if(remove&&value.length())return false;
 if(remove){memset(destination,0,capacity);return true;}if(!value.length())return true;
 if(!safeText(value,capacity-1,false)||(wifi&&!advanced::wifi_password_field(value.c_str())))return false;
 memset(destination,0,capacity);strlcpy(destination,value.c_str(),capacity);return true;
}
bool validRequest(WebServer& s){
 if(!localHost(s)||!s.hasArg("token")||s.arg("token")!=token)return false;
 String origin=s.header("Origin");return !origin.length()||origin==String("http://")+s.hostHeader();
}
void accepted(WebServer& s){String page=pageStart();page+="<p id='message' role='status'>Änderungen werden gespeichert …</p><script>let tries=0;const t=setInterval(async()=>{try{const r=await fetch('/api/settings-status',{cache:'no-store'});const x=await r.json();if(x.state==='failed'){clearInterval(t);document.getElementById('message').textContent='Speichern fehlgeschlagen. Bitte Einstellungen erneut öffnen.';}if(x.state==='saved'){clearInterval(t);document.getElementById('message').textContent='Erledigt. Der Miner startet neu. Danach zur Statistik zurückkehren; bei geändertem WLAN die neue IP verwenden.';}}catch(e){}if(++tries>=60)clearInterval(t);},1000);</script></html>";headers(s);s.send(202,"text/html; charset=utf-8",page);}
void post(WebServer& s){
 if(!validRequest(s)){reject(s,403,"Ungültige Anfrage. Einstellungen neu öffnen.");return;}
 if(getState()==QUEUED||getState()==SAVING||getState()==SAVED){reject(s,409,"Speichern läuft bereits.");return;}
 miner_config_t cfg;if(!nvs_config_load(&cfg)){reject(s,503,"Einstellungen nicht verfügbar.");return;}
 String wallet=s.arg("wallet"),worker=s.arg("worker"),ssid=s.arg("ssid"),pool=s.arg("pool"),backup=s.arg("backup_pool"),backupWallet=s.arg("backup_wallet");wallet.trim();worker.trim();pool.trim();backup.trim();backupWallet.trim();
 unsigned rotation,brightness,timeout,port,backupPort,tzAbs;double difficulty;
 String timezone=s.arg("timezone");bool negative=timezone.length()&&timezone.c_str()[0]=='-';String tzDigits=negative?timezone.substring(1):timezone;
 bool ok=advanced::wallet_field(wallet.c_str(),MAX_WALLET_LEN)&&advanced::worker_field(worker.c_str(),31)&&safeText(ssid,32,false)&&advanced::bounded_number(s.arg("rotation").c_str(),3,rotation)&&advanced::bounded_number(s.arg("brightness").c_str(),100,brightness)&&brightness>=1&&advanced::bounded_number(s.arg("timeout").c_str(),65535,timeout)&&advanced::bounded_number(s.arg("port").c_str(),65535,port)&&port>=1&&advanced::bounded_number(s.arg("backup_port").c_str(),65535,backupPort)&&advanced::bounded_number(tzDigits.c_str(),negative?12:14,tzAbs)&&advanced::decimal_field(s.arg("difficulty").c_str(),1e-9,1e6,difficulty);
 const char* poolHost=advanced::tcp_pool_host(pool.c_str());const char* backupHost=advanced::tcp_pool_host(backup.c_str());
 ok=ok&&safeText(wallet,MAX_WALLET_LEN)&&safeText(worker,31)&&safeText(pool,MAX_POOL_URL_LEN,false)&&advanced::pool_host_field(poolHost)&&safeText(backup,MAX_POOL_URL_LEN)&&safeText(backupWallet,MAX_WALLET_LEN);
 bool useBackup=checked(s,"backup_enabled");if(useBackup)ok=ok&&backupPort>=1&&advanced::pool_host_field(backupHost)&&advanced::wallet_field(backupWallet.c_str(),MAX_WALLET_LEN);
 ok=ok&&updatePassword(s,"wifi_password",cfg.wifiPassword,sizeof(cfg.wifiPassword),true)&&updatePassword(s,"pool_password",cfg.poolPassword,sizeof(cfg.poolPassword))&&updatePassword(s,"backup_password",cfg.backupPoolPassword,sizeof(cfg.backupPoolPassword));
#ifdef ADVANCED_MAIL
 mail::Config mailCfg;mail_config_load(mailCfg);
 if(s.arg("mail_present")=="1"){
  const char* names[]={"mail_host","mail_user","mail_from","mail_to"};
  char* destinations[]={mailCfg.host,mailCfg.user,mailCfg.from,mailCfg.to};
  const unsigned sizes[]={sizeof(mailCfg.host),sizeof(mailCfg.user),sizeof(mailCfg.from),sizeof(mailCfg.to)};
  for(unsigned i=0;i<4;++i){String value=s.arg(names[i]);value.trim();ok=ok&&safeText(value,sizes[i]-1);if(value.length()<sizes[i])strlcpy(destinations[i],value.c_str(),sizes[i]);}
  unsigned hour=0,low=0,reject=0,port=0;
  ok=ok&&advanced::bounded_number(s.arg("mail_hour").c_str(),23,hour)&&advanced::bounded_number(s.arg("mail_low").c_str(),2000,low)&&low>=1&&advanced::bounded_number(s.arg("mail_reject").c_str(),100,reject)&&reject>=1&&advanced::bounded_number(s.arg("mail_port").c_str(),465,port)&&port==465&&updatePassword(s,"mail_password",mailCfg.password,sizeof(mailCfg.password));
  if(!strcmp(mailCfg.host,"smtp.gmail.com")&&s.arg("mail_password").length()){
   unsigned at=0;for(unsigned i=0;mailCfg.password[i];++i)if(mailCfg.password[i]!=' ')mailCfg.password[at++]=mailCfg.password[i];memset(mailCfg.password+at,0,sizeof(mailCfg.password)-at);
  }
  mailCfg.hour=hour;mailCfg.low_khs=low;mailCfg.reject_percent=reject;mailCfg.port=port;
  mailCfg.enabled=checked(s,"mail_enabled");mailCfg.daily=checked(s,"mail_daily");mailCfg.warnings=checked(s,"mail_warnings");ok=ok&&mail::valid(mailCfg);
 }
#endif
 if(!ok){reject(s,400,"Ungültige Eingabe. Bitte zurückgehen und Wallet-/Mailadressen, SMTP-Port 465, Kennwortlängen und Zahlenbereiche prüfen. Wallet ohne .Worker; Pool als Hostname oder stratum+tcp:// ohne Port/Pfad.");return;}
 strlcpy(cfg.ssid,ssid.c_str(),sizeof(cfg.ssid));strlcpy(cfg.wallet,wallet.c_str(),sizeof(cfg.wallet));strlcpy(cfg.workerName,worker.c_str(),sizeof(cfg.workerName));strlcpy(cfg.poolUrl,poolHost,sizeof(cfg.poolUrl));cfg.poolPort=port;
 strlcpy(cfg.backupPoolUrl,backupHost,sizeof(cfg.backupPoolUrl));cfg.backupPoolPort=backupPort;strlcpy(cfg.backupWallet,useBackup?backupWallet.c_str():"",sizeof(cfg.backupWallet));cfg.rotation=rotation;cfg.brightness=brightness;cfg.screenTimeout=timeout;cfg.displayEnabled=checked(s,"display_on");cfg.invertColors=checked(s,"invert");cfg.timezoneOffset=negative?-int(tzAbs):int(tzAbs);cfg.targetDifficulty=difficulty;
#ifdef ADVANCED_MAIL
 if(!mail_claim_settings()){reject(s,409,"Mailversand oder Pruefung laeuft. Bitte kurz warten und erneut speichern.");return;}
#endif
 bool requestedTouch=checked(s,"touch");
#ifdef ADVANCED_MAIL
 pendingMail=mailCfg;
#endif
 portENTER_CRITICAL(&mux);pending=cfg;pendingTouch=requestedTouch;pendingAction=CONFIG;state=QUEUED;portEXIT_CRITICAL(&mux);accepted(s);
}
void action(WebServer& s){
 if(!validRequest(s)){reject(s,403,"Ungültige Anfrage.");return;}
 if(getState()==QUEUED||getState()==SAVING||getState()==SAVED){reject(s,409,"Aktion läuft bereits.");return;}
 String name=s.arg("action"),confirmation=s.arg("confirmation");Action next;
 if(name=="restart"&&confirmation=="NEUSTART")next=RESTART;
 else if(name=="stats_reset"&&confirmation=="STATISTIK LOESCHEN")next=STATS_RESET;
 else if(name=="factory"&&confirmation=="WERKSRESET")next=FACTORY;
 else{reject(s,400,"Aktion nicht bestätigt.");return;}
#ifdef ADVANCED_MAIL
 if(!mail_claim_settings()){reject(s,409,"Mailversand oder Pruefung laeuft. Bitte kurz warten und erneut versuchen.");return;}
#endif
 portENTER_CRITICAL(&mux);pendingAction=next;state=QUEUED;portEXIT_CRITICAL(&mux);accepted(s);
}
}
void local_settings_register(WebServer& s){
 snprintf(token,sizeof(token),"%08x%08x%08x%08x",esp_random(),esp_random(),esp_random(),esp_random());
 const char* keys[]={"Origin"};s.collectHeaders(keys,1);
 s.on("/settings",HTTP_GET,[&s]{get(s);});s.on("/settings",HTTP_POST,[&s]{post(s);});s.on("/settings/action",HTTP_POST,[&s]{action(s);});
#ifdef ADVANCED_MAIL
 s.on("/settings/mail-probe",HTTP_POST,[&s]{
  if(!validRequest(s)){reject(s,403,"Ungueltige Anfrage.");return;}
  if(local_settings_blocks_checkpoint()){reject(s,409,"Speichern laeuft.");return;}
  mail::Config cfg;if(!mail_config_load(cfg)||!mail::host(cfg.host)){reject(s,400,"Bitte SMTP-Host zuerst speichern.");return;}
  if(!mail_queue_probe()){reject(s,409,"Mailpruefung laeuft bereits oder ist nicht bereit.");return;}
  headers(s);s.send(202,"text/plain; charset=utf-8","SMTP-Verbindungspruefung angefordert; keine Mail wird versendet.");
 });
 s.on("/settings/mail-test",HTTP_POST,[&s]{
  if(!validRequest(s)||s.arg("confirmation")!="TESTMAIL"){reject(s,403,"Testmail nicht bestaetigt.");return;}
  if(local_settings_blocks_checkpoint()){reject(s,409,"Speichern laeuft; nach Neustart erneut testen.");return;}
  mail::Config cfg;if(!mail_config_load(cfg)||!mail::complete(cfg)){reject(s,400,"Bitte vollstaendige Maildaten eingeben und zuerst speichern.");return;}
  if(!mail_queue_test()){reject(s,409,"Mailversand laeuft bereits oder ist noch nicht bereit.");return;}
  headers(s);s.send(202,"text/plain; charset=utf-8","Testmail angefordert.");
 });
 s.on("/api/mail-status",HTTP_GET,[&s]{if(!localHost(s)){reject(s,403,"Forbidden");return;}headers(s);String body="{\"message\":\"";body+=mail_status();body+="\"}";s.send(200,"application/json",body);});
#endif
 s.on("/api/settings-status",HTTP_GET,[&s]{if(!localHost(s)){reject(s,403,"Forbidden");return;}headers(s);State v=getState();const char* name=v==FAILED?"failed":v==SAVED?"saved":v==QUEUED||v==SAVING?"pending":"idle";String body="{\"state\":\"";body+=name;body+="\"}";s.send(200,"application/json",body);});
}
bool local_settings_blocks_checkpoint(){State s=getState();return s==QUEUED||s==SAVING||s==SAVED;}
void local_settings_process(const advanced::Counters& total){
 miner_config_t cfg;bool touch=true,apply=false;Action requested=CONFIG;
 portENTER_CRITICAL(&mux);if(state==QUEUED){cfg=pending;touch=pendingTouch;requested=pendingAction;state=SAVING;apply=true;}portEXIT_CRITICAL(&mux);
 if(apply){
  bool ok=false;
  if(requested==CONFIG){bool oldTouch=nvs_touch_enabled();
#ifdef ADVANCED_MAIL
   mail::Config oldMail;mail_config_load(oldMail);
   if(nvs_stats_store_totals(total)&&nvs_touch_save(touch)){
    if(mail_config_save(pendingMail)){ok=nvs_config_save(&cfg);if(!ok)mail_config_save(oldMail);}
    if(!ok)nvs_touch_save(oldTouch);
   }
#else
   if(nvs_stats_store_totals(total)&&nvs_touch_save(touch)){ok=nvs_config_save(&cfg);if(!ok)nvs_touch_save(oldTouch);}
#endif
  }
  else if(requested==RESTART)ok=nvs_stats_store_totals(total);
  else if(requested==STATS_RESET){
#ifdef ADVANCED_MAIL
   if(mail_clear_history())ok=nvs_stats_store_totals(advanced::Counters{});
#else
   ok=nvs_stats_store_totals(advanced::Counters{});
#endif
  }
  else if(requested==FACTORY)ok=nvs_factory_clear();
  if(!ok){setState(FAILED);
#ifdef ADVANCED_MAIL
   mail_release_settings();
#endif
   return;
  }
  restartAction=requested;setState(SAVED);restartAt=millis()+3000;Serial.println("[SETTINGS] Action saved; controlled restart pending");
 }
 if(restartAt&&int32_t(millis()-restartAt)>=0){
  if(restartAction==CONFIG||restartAction==RESTART){if(!nvs_stats_store_totals(total)){restartAt=0;setState(FAILED);
#ifdef ADVANCED_MAIL
   mail_release_settings();
#endif
   return;}}
  if(restartAction==FACTORY)WiFi.disconnect(true,true);
  ESP.restart();
 }
}
#endif
