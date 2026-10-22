#pragma once
#include <functional>
#include <map>
#include <string>
#include <utility>
constexpr int HTTP_GET=0, HTTP_POST=1;
class WebServer;
extern WebServer* test_server;
class WebServer {
    std::map<std::pair<std::string,int>,std::function<void()>> routes;
    std::function<void()> notFound;
public:
    int code=0;
    std::string body,type;
    std::map<std::string,std::string> headers;
    explicit WebServer(int) { test_server=this; }
    void on(const char* path,int method,std::function<void()> handler) {routes[{path,method}]=handler;}
    void onNotFound(std::function<void()> handler) {notFound=handler;}
    void begin() {}
    void handleClient() {}
    void sendHeader(const char* key,const char* value) {headers[key]=value;}
    void send(int status,const char* contentType,const char* content) {code=status;type=contentType;body=content;}
    void send_P(int status,const char* contentType,const char* content) {send(status,contentType,content);}
    void request(const std::string& path,int method=HTTP_GET) {
        code=0;body.clear();headers.clear();
        auto it=routes.find({path,method});if(it==routes.end())notFound();else it->second();
    }
};
