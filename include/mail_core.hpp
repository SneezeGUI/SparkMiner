#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <advanced_statistics.hpp>
namespace mail {
struct Config {
 uint32_t version=1;
 bool enabled=false,daily=true,warnings=true;
 uint8_t hour=20,reject_percent=5;
 uint16_t port=465,low_khs=500;
 char host[81]="smtp.gmail.com",user[129]={},password[129]={},from[129]={},to[129]={};
 uint32_t checksum=0;
};
inline bool text(const char* s,size_t max,bool empty=true){
 if(!s||strlen(s)>max||(!empty&&!*s))return false;
 for(;*s;++s)if(static_cast<unsigned char>(*s)<32||*s==127)return false;
 return true;
}
inline bool address(const char* s){
 if(!text(s,128,false))return false;
 const char* at=strchr(s,'@');if(!at||at==s||!at[1]||strchr(at+1,'@')||!strchr(at+1,'.'))return false;
 for(const char*p=s;*p;++p)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||strchr(".!#$%&'*+-/=?^_`{|}~@",*p)))return false;
 return s[strlen(s)-1]!='.';
}
inline bool host(const char*s){
 if(!text(s,80,false)||s[0]=='.'||s[0]=='-'||s[strlen(s)-1]=='.'||s[strlen(s)-1]=='-')return false;
 for(;*s;++s)if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||*s=='.'||*s=='-'))return false;
 return true;
}
inline bool complete(const Config& c){return c.version==1&&c.port==465&&c.hour<24&&c.low_khs>=1&&c.low_khs<=2000&&c.reject_percent>=1&&c.reject_percent<=100&&host(c.host)&&text(c.user,128,false)&&text(c.password,128,false)&&address(c.from)&&address(c.to);}
inline bool valid(const Config& c){return c.version==1&&c.port==465&&c.hour<24&&c.low_khs>=1&&c.low_khs<=2000&&c.reject_percent>=1&&c.reject_percent<=100&&text(c.host,80)&&text(c.user,128)&&text(c.password,128)&&text(c.from,128)&&text(c.to,128)&&(!*c.host||host(c.host))&&(!*c.from||address(c.from))&&(!*c.to||address(c.to))&&(!c.enabled||complete(c));}
inline bool base64(const char* in,char*out,size_t cap){
 static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 const size_t n=strlen(in),needed=4*((n+2)/3);if(cap<=needed)return false;
 size_t j=0;for(size_t i=0;i<n;i+=3){uint32_t bits=uint32_t(uint8_t(in[i]))<<16;if(i+1<n)bits|=uint32_t(uint8_t(in[i+1]))<<8;if(i+2<n)bits|=uint8_t(in[i+2]);out[j++]=alphabet[(bits>>18)&63];out[j++]=alphabet[(bits>>12)&63];out[j++]=i+1<n?alphabet[(bits>>6)&63]:'=';out[j++]=i+2<n?alphabet[bits&63]:'=';}out[j]=0;return true;
}
enum class Result {Accepted, Network, Protocol, Authentication, Rejected, Uncertain};
// IO must enforce an overall deadline and reject overlong SMTP lines.
template<class IO> int reply(IO&io,bool* login=nullptr){
 char line[514];int code=-1;
 for(unsigned count=0;count<64;++count){
  if(!io.line(line,sizeof(line)))return -1;
  if(strlen(line)<4||line[0]<'2'||line[0]>'5'||line[1]<'0'||line[1]>'9'||line[2]<'0'||line[2]>'9'||(line[3]!=' '&&line[3]!='-'))return -1;
  int next=(line[0]-'0')*100+(line[1]-'0')*10+line[2]-'0';if(code!=-1&&code!=next)return -1;code=next;
  if(login){for(char*p=line+4;*p;++p)if(*p>='a'&&*p<='z')*p-=32;if(strstr(line+4,"AUTH")&&strstr(line+4,"LOGIN"))*login=true;}
  if(line[3]==' ')return code;
 }
 return -1;
}
template<class IO> Result deliver(IO&io,const Config&c,const char*subject,const char*body,const char*date,uint32_t id){
 if(!complete(c)||!text(subject,120,false))return Result::Protocol;
 if(reply(io)!=220||!io.put("EHLO esp32-miner.local\r\n"))return Result::Protocol;
 bool login=false;if(reply(io,&login)!=250||!login)return Result::Protocol;
 if(!io.put("AUTH LOGIN\r\n")||reply(io)!=334)return Result::Authentication;
 char encoded[176],command[320];
 if(!base64(c.user,encoded,sizeof(encoded)))return Result::Protocol;
 snprintf(command,sizeof(command),"%s\r\n",encoded);if(!io.put(command)||reply(io)!=334)return Result::Authentication;
 base64(c.password,encoded,sizeof(encoded));snprintf(command,sizeof(command),"%s\r\n",encoded);memset(encoded,0,sizeof(encoded));
 if(!io.put(command)){memset(command,0,sizeof(command));return Result::Authentication;}memset(command,0,sizeof(command));if(reply(io)!=235)return Result::Authentication;
 snprintf(command,sizeof(command),"MAIL FROM:<%s>\r\n",c.from);if(!io.put(command)||reply(io)!=250)return Result::Rejected;
 snprintf(command,sizeof(command),"RCPT TO:<%s>\r\n",c.to);if(!io.put(command))return Result::Rejected;int rc=reply(io);if(rc!=250&&rc!=251)return Result::Rejected;
 if(!io.put("DATA\r\n")||reply(io)!=354)return Result::Rejected;
 // From this point the server could accept the message even if the final reply is lost.
 snprintf(command,sizeof(command),"From: <%s>\r\n",c.from);if(!io.put(command))return Result::Uncertain;
 snprintf(command,sizeof(command),"To: <%s>\r\n",c.to);if(!io.put(command))return Result::Uncertain;
 snprintf(command,sizeof(command),"Date: %s\r\nMessage-ID: <%08lx@esp32-miner.local>\r\nSubject: %s\r\n",date,(unsigned long)id,subject);if(!io.put(command)||!io.put("MIME-Version: 1.0\r\nContent-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: 7bit\r\n\r\n"))return Result::Uncertain;
 // Dot-stuff every line. The generated body is ASCII; input lengths are bounded.
 bool start=true;for(const char*p=body;*p;++p){if(start&&*p=='.'&&!io.put("."))return Result::Uncertain;char one[2]={*p,0};if(!io.put(one))return Result::Uncertain;start=*p=='\n';}
 if(!io.put("\r\n.\r\n"))return Result::Uncertain;
 rc=reply(io);if(rc<0)return Result::Uncertain;if(rc!=250)return Result::Rejected;
 io.put("QUIT\r\n");return Result::Accepted;
}
struct DeliveryState {
 uint32_t magic=0x4d414931,last_day=0,attempt_day=0;
 uint8_t report_attempts=0,alert_attempts=0;
 int64_t report_retry=0,alert_retry=0;
 bool baseline=false;
 advanced::Counters last;
 void day(uint32_t today){if(attempt_day!=today){attempt_day=today;report_attempts=alert_attempts=0;}}
 bool report_due(const Config&c,uint32_t today,unsigned hour,int64_t now)const{return c.enabled&&c.daily&&today>last_day&&hour>=c.hour&&now>=report_retry&&report_attempts<3;}
 bool alert_due(const Config&c,uint8_t pending,int64_t now)const{return c.enabled&&c.warnings&&pending&&now>=alert_retry&&alert_attempts<3;}
 void reserve(bool report,int64_t now){if(report){++report_attempts;report_retry=now+600;}else{++alert_attempts;alert_retry=now+600;}}
 void complete(Result result,bool report,bool warning,uint32_t today,int64_t now,const advanced::Counters&counts){
  if(result!=Result::Accepted&&result!=Result::Uncertain)return;
  if(report){last_day=today;report_attempts=0;if(result==Result::Accepted){last=counts;baseline=true;}}
  if(warning){alert_retry=now+3600;alert_attempts=0;}
 }
};
// Monotonic timers; no alert for the temporary, deliberate SMTP mining pause.
struct Alerts {
 uint64_t pool_since=0,low_since=0,window_since=0,quiet_until=0;
 uint32_t accepted=0,rejected=0;bool pool_warned=false,low_warned=false,offline_long=false,pool_tracking=false,low_tracking=false,window_ready=false;
 uint8_t evaluate(const Config&cfg,const advanced::Snapshot&s,uint64_t ms){
  if(ms<quiet_until)return 0;
  uint8_t due=0;
  if(!s.pool_connected||!s.wifi_connected){if(!pool_tracking){pool_since=ms;pool_tracking=true;}if(ms-pool_since>=300000){offline_long=true;if(!pool_warned){due|=1;pool_warned=true;}}}
  else {if(offline_long)due|=8;pool_since=0;pool_tracking=false;pool_warned=false;offline_long=false;}
  if(s.pool_connected&&s.wifi_connected&&s.session.uptime_s>=120&&s.hashrate_hs<cfg.low_khs*1000.0){if(!low_tracking){low_since=ms;low_tracking=true;}if(ms-low_since>=300000&&!low_warned){due|=2;low_warned=true;}}
  else{low_since=0;low_tracking=false;low_warned=false;}
  if(!window_ready){window_ready=true;window_since=ms;accepted=s.session.accepted;rejected=s.session.rejected;}
  if(ms-window_since>=600000){
   if(s.session.accepted>=accepted&&s.session.rejected>=rejected){uint64_t a=s.session.accepted-accepted,r=s.session.rejected-rejected;if(a+r>=20&&r*100>=uint64_t(cfg.reject_percent)*(a+r))due|=4;}
   window_since=ms;accepted=s.session.accepted;rejected=s.session.rejected;
  }
  return due;
 }
 void after_mail(uint64_t ms){quiet_until=ms+60000;low_since=0;low_tracking=false;}
};
}
