#pragma once
#include <cstdint>
#include <cstring>
namespace advanced {
inline bool bounded_number(const char* s, unsigned maximum, unsigned& value) {
 if (!s || !*s) return false;
 unsigned n=0;
 for (;*s;++s) { if (*s<'0'||*s>'9') return false; unsigned d=*s-'0'; if(d>maximum || n>(maximum-d)/10) return false; n=n*10+d; }
 value=n;return true;
}
inline bool wallet_field(const char* s, unsigned capacity) {
 if (!s) return false; unsigned n=0;
 for (;*s;++s) {char c=*s;if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9'))) return false;if(++n>capacity)return false;}
 return n>=26; // Format guard only; this does not verify the Bitcoin checksum.
}
inline bool worker_field(const char* s, unsigned capacity) {
 if(!s)return false;unsigned n=0;
 for(;*s;++s){char c=*s;if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;if(++n>capacity)return false;}return true;
}
class TouchDebounce {
 bool candidate_=false,stable_=false;uint32_t since_=0;
public:
 bool sample(bool down,uint32_t now) {
  if(down!=candidate_){candidate_=down;since_=now;}
  if(candidate_!=stable_ && uint32_t(now-since_)>=30){stable_=candidate_;return stable_;}
  return false;
 }
};
}

#include <cstdlib>
#include <cmath>
namespace advanced {
inline bool decimal_field(const char* s,double low,double high,double& value){
 if(!s||!*s)return false;
 for(const char*p=s;*p;++p)if(!((*p>='0'&&*p<='9')||*p=='.'||*p=='e'||*p=='E'||*p=='+'||*p=='-'))return false;
 char* end=nullptr;double n=std::strtod(s,&end);if(!end||*end||!std::isfinite(n)||n<low||n>high)return false;value=n;return true;
}
inline bool text_field(const char* s,unsigned max,bool empty=true){if(!s)return false;unsigned n=0;for(;*s;++s){unsigned char c=*s;if(c<32||c==127||++n>max)return false;}return empty||n>0;}
inline bool pool_host_field(const char* s){if(!s||!*s)return false;unsigned n=0;for(;*s;++s){char c=*s;if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-')||++n>80)return false;}return true;}
inline bool wifi_password_field(const char*s){unsigned n=std::strlen(s);if(n>=8&&n<=63)return text_field(s,63);if(n!=64)return false;for(;*s;++s)if(!((*s>='0'&&*s<='9')||(*s>='a'&&*s<='f')||(*s>='A'&&*s<='F')))return false;return true;}
}
