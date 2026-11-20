#!/usr/bin/env python3
"""Exercise actual session/auth code without a live Admin account or network."""
from pathlib import Path
import shutil
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'IntrusionMonitor/IntrusionMonitor.ino').read_text()
sessions=(root/'IntrusionMonitor/AdminSession.h').read_text().replace('#pragma once','')
auth=source[source.index('bool auth() {'):source.index('void dashboard()')]
def handler(route,method,name):
 start=source.index('  server.on("'+route+'", '+method+', []() {');start=source.index('\n',start)+1
 return 'void '+name+'(){\n'+source[start:source.index('\n  });',start)]+'\n}\n'
stub=r'''
#include <string>
#include <map>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cassert>
#include <iostream>
struct String:std::string {
 using std::string::string;String(const std::string&s):std::string(s){}
 String(unsigned n,int):std::string(std::to_string(n)){}
 int indexOf(const String&s,int start=0)const{auto n=find(s,start);return n==npos?-1:int(n);}
 int indexOf(char c,int start=0)const{auto n=find(c,start);return n==npos?-1:int(n);}
 String substring(int a,int b)const{return substr(a,b-a);}
 String substring(int a)const{return substr(a);}
 bool startsWith(const String&s)const{return rfind(s,0)==0;}
 void trim(){auto a=find_first_not_of(" \t\r\n");if(a==npos){clear();return;}*this=substr(a,find_last_not_of(" \t\r\n")-a+1);}
};
#define HEX 16
#define DIGEST_AUTH 1
#define HTTP_GET 0
#define HTTP_POST 1
uint64_t nowMs=1000;
uint64_t esp_timer_get_time(){return nowMs*1000;}
unsigned esp_random(){static unsigned n=100;return ++n;}
String adminRealm="Watchtower Admin fixture",adminPassword="test-only";
struct {
 std::map<std::string,String> incoming,args;std::vector<std::pair<String,String>> outgoing;
 int code=0,verb=HTTP_GET;String path="/admin",body;bool valid=true;unsigned authCalls=0,challenges=0;
 String header(const char*k){return incoming[k];}String arg(const char*k){return args[k];}
 String uri(){return path;}int method(){return verb;}
 void sendHeader(const char*k,String v){outgoing.push_back({k,v});}
 void send(int c,const char*,String v){code=c;body=v;}
 bool authenticate(const char*,const char*){++authCalls;return valid;}
 void requestAuthentication(int,const char*){code=401;++challenges;}
}server;
'''
request=r'''
bool adminRequest(bool write){if(!auth())return false;if(write&&server.arg("csrf")!="fixture"){server.code=403;return false;}return true;}
'''
tests=r'''
int main(){
 auto authorize=[](){server.path="/admin";server.verb=HTTP_GET;server.incoming["Authorization"]="Digest username=\"admin\", realm=\""+adminRealm+"\", response=\"fixture\"";};
 auto cookie=[](){for(auto i=server.outgoing.rbegin();i!=server.outgoing.rend();++i)if(i->first=="Set-Cookie"&&i->second.startsWith("watchtower_session=")){auto end=i->second.indexOf(';');return i->second.substring(19,end);}return String();};
 server.path="/admin/health";assert(!auth()&&server.code==401&&server.challenges==0);
 server.path="/admin";assert(!auth()&&server.challenges==1);
 authorize();assert(auth());String first=cookie();assert(first.length()==32);
 unsigned challenges=server.challenges,calls=server.authCalls;
 server.incoming.clear();server.incoming["Cookie"]="watchtower_session="+first;server.path="/admin/health";
 for(unsigned i=0;i<1000;++i){++nowMs;assert(auth());}
 assert(server.challenges==challenges&&server.authCalls==calls);
 server.incoming.clear();authorize();assert(auth());String second=cookie();assert(second!=first);
 server.incoming.clear();server.incoming["Cookie"]="watchtower_session="+first;server.args["csrf"]="fixture";
 String realm=adminRealm;logoutAdmin();assert(server.code==200&&realm==adminRealm);
 server.incoming["Cookie"]="watchtower_logout=1; watchtower_session="+first;
 assert(!auth()&&server.challenges==challenges);
 server.incoming["Cookie"]="watchtower_session="+second;server.path="/admin/ap";assert(auth());
 server.args["csrf"]="bad";logoutAdmin();assert(server.code==403);assert(auth());
 nowMs+=adminIdleMs+1;assert(!auth()&&server.code==401&&server.challenges==challenges);
 loginAdmin();assert(server.code==303);
 for(unsigned i=0;i<12;++i){server.incoming.clear();authorize();++nowMs;assert(auth());}
 unsigned count=0;for(auto&s:adminSessions)count+=s.token[0]!=0;assert(count==8);
 server.incoming.clear();server.path="/admin/ap";assert(!auth()&&server.challenges==challenges);
 server.incoming.clear();authorize();server.path="/admin/health";calls=server.authCalls;assert(!auth()&&server.authCalls==calls&&server.challenges==challenges);
 authorize();server.valid=false;assert(!auth()&&server.challenges==challenges+1);
 std::cout<<"Auth checks passed: 1000 polls without new challenges, isolated browsers, logout, CSRF, idle expiry, and bounded session storage.\n";
}
'''
compiler=shutil.which('clang++') or shutil.which('g++')
if not compiler:raise SystemExit('Install a C++17 compiler to run auth checks.')
with tempfile.TemporaryDirectory(prefix='watchtower-auth-') as directory:
 path=Path(directory);file=path/'test.cpp';file.write_text(stub+sessions+auth+request+handler('/admin/logout','HTTP_POST','logoutAdmin')+handler('/admin/login','HTTP_GET','loginAdmin')+tests)
 subprocess.run([compiler,'-std=c++17','-Wall','-Wextra',str(file),'-o',str(path/'test')],check=True)
 subprocess.run([str(path/'test')],check=True)
