#pragma once
#include <cstdint>
#include <mutex>
using portMUX_TYPE = std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(p) (p)->lock()
#define portEXIT_CRITICAL(p) (p)->unlock()
#define pdMS_TO_TICKS(n) (n)
#define pdPASS 1
using TaskHandle_t = void*;
using TaskFunction_t = void(*)(void*);
extern TaskFunction_t test_task;
struct StopTask {};
inline int xTaskCreatePinnedToCore(TaskFunction_t fn, const char*, int, void*, int, TaskHandle_t* handle, int) {
    test_task=fn;*handle=reinterpret_cast<void*>(1);return pdPASS;
}
inline void vTaskDelay(int) { throw StopTask(); }
struct TestSerial { void println(const char*) {} };
extern TestSerial Serial;
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdarg>
class String {
 std::string v;
public:
 String()=default;String(const char*s):v(s?s:""){}String(const std::string&s):v(s){}String(unsigned n):v(std::to_string(n)){}String(int n):v(std::to_string(n)){}
 const char*c_str()const{return v.c_str();}size_t length()const{return v.size();}
 int indexOf(char c,int from=0)const{auto n=v.find(c,from);return n==std::string::npos?-1:int(n);}
 String substring(int from)const{return v.substr(from);}String substring(int from,int to)const{return v.substr(from,to-from);}
 void trim(){auto a=v.find_first_not_of(" \r\n\t");if(a==std::string::npos){v.clear();return;}v=v.substr(a,v.find_last_not_of(" \r\n\t")-a+1);}
 String&operator+=(const String&s){v+=s.v;return *this;}String&operator+=(const char*s){v+=s;return *this;}String&operator+=(char c){v+=c;return *this;}
 friend String operator+(const String&a,const String&b){return a.v+b.v;}
 friend bool operator==(const String&a,const String&b){return a.v==b.v;}friend bool operator!=(const String&a,const String&b){return !(a==b);}
};
inline size_t strlcpy(char*d,const char*s,size_t n){size_t len=strlen(s);if(n){size_t k=len<n-1?len:n-1;memcpy(d,s,k);d[k]=0;}return len;}
extern uint32_t test_now;
inline uint32_t millis(){return test_now;}
struct TestESP {bool restarted=false;void restart(){restarted=true;}};
extern TestESP ESP;
