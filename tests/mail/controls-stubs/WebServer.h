#pragma once
#include <Arduino.h>
#include <map>
#include <functional>
constexpr int HTTP_GET=0,HTTP_POST=1;
class WebServer{
 std::map<std::pair<std::string,int>,std::function<void()>> routes;
public:
 int code=0;std::string body;std::map<std::string,String> args;std::map<std::string,String> requestHeaders;std::string host="192.0.2.43";
 explicit WebServer(int){}
 String hostHeader(){return host;}String header(const char*k){return requestHeaders[k];}
 bool hasArg(const char*k){return args.count(k);}String arg(const char*k){return args[k];}
 void collectHeaders(const char**,int){}void sendHeader(const char*,const char*){}
 void send(int status,const char*,const String&s){code=status;body=s.c_str();}
 void on(const char*path,int method,std::function<void()> fn){routes[{path,method}]=fn;}
 void request(const char*path,int method=HTTP_GET){code=0;body.clear();auto i=routes.find({path,method});if(i==routes.end()){code=404;return;}i->second();}
};
