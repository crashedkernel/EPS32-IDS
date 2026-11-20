#!/usr/bin/env python3
"""Test heatmap preferences using the actual firmware save handler; no live settings change."""
from pathlib import Path
import shutil
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'IntrusionMonitor/HeatmapSettings.h').read_text()
code = source[source.index('constexpr unsigned HeatStateCount'):source.index('static String heatmapSettingsForm')]
code += source[source.index('static void loadHeatmapSettings'):]
stub = r'''
#include <string>
#include <map>
#include <vector>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
struct String: std::string {
  using std::string::string;
  String(const std::string&s):std::string(s){}
  String(unsigned n):std::string(std::to_string(n)){}
  String(int n):std::string(std::to_string(n)){}
  long toInt()const{return strtol(c_str(),nullptr,10);}
};
String json(String value){return "\""+value+"\"";}
struct {std::map<std::string,String> args;int code=0;String body;
 String arg(const String&k){return args[k];}
 void send(int c,const char*,String b){code=c;body=b;}
}server;
struct {
 bool fail=false;unsigned writes=0;std::map<std::string,std::vector<unsigned char>> blobs;
 size_t putBytes(const char*k,const void*p,size_t n){++writes;if(fail)return 0;const auto*b=static_cast<const unsigned char*>(p);blobs[k]={b,b+n};return n;}
 size_t getBytesLength(const char*k){return blobs[k].size();}
 size_t getBytes(const char*k,void*p,size_t n){auto&b=blobs[k];if(b.size()!=n)return 0;memcpy(p,b.data(),n);return n;}
 bool isKey(const char*k){return blobs.count(k);}
 bool remove(const char*k){return blobs.erase(k);}
}prefs;
bool allowed=true;
bool adminRequest(bool){if(!allowed)server.code=401;return allowed;}
'''
tests = r'''
int main(){
 assert(heatmapSettings.blink[1]&&heatmapSettings.blink[2]&&!heatmapSettings.blink[4]);
 for(unsigned i=0;i<5;++i){
  server.args["heatColor"+std::to_string(i)]="#123456";
  server.args["heatSpeed"+std::to_string(i)]=i?"25":"2";
  server.args["heatBlink"+std::to_string(i)]="yes";
 }
 server.args["heatRandomOnline"]="yes";
 saveHeatmapSettings();assert(server.code==200&&heatmapSettings.randomOnline&&heatmapSettings.version==2);
 for(unsigned i=0;i<5;++i)assert(heatmapSettings.colors[i]==0x123456&&heatmapSettings.blink[i]&&heatmapSettings.speed[i]==(i?25:2));
 assert(server.body.find("#123456")!=std::string::npos);
 for(const char*bad:{"1","26","2.5","025","abc"}){
  server.args["heatSpeed4"]=bad;saveHeatmapSettings();assert(server.code==400&&heatmapSettings.speed[4]==25);
 }
 server.args["heatSpeed4"]="10";
 server.args["heatColor0"]="#12xxzz";saveHeatmapSettings();assert(server.code==400);
 server.args["heatColor0"]="#ffffff";prefs.fail=true;saveHeatmapSettings();assert(server.code==500&&heatmapSettings.colors[0]==0x123456);
 prefs.fail=false;allowed=false;saveHeatmapSettings();assert(server.code==401&&heatmapSettings.colors[0]==0x123456);
 allowed=true;
 struct Legacy {uint32_t version;uint32_t colors[5];bool blink[5];uint8_t speed[5];} legacy={1,{0x654321,1,2,3,4},{true,false,true,false,false},{10,20,9,10,10}};
 prefs.remove("heatmap-v2");prefs.putBytes("heatmap-v1",&legacy,sizeof(legacy));heatmapSettings=HeatmapSettings();
 loadHeatmapSettings();assert(heatmapSettings.colors[0]==0x654321&&heatmapSettings.speed[2]==9&&!heatmapSettings.blink[1]&&!heatmapSettings.randomOnline&&heatmapSettings.version==2);
 std::cout<<"Heatmap checks passed: all five colors, blink switches, slider bounds, malformed input, failed writes, and authentication.\n";
}
'''
compiler = shutil.which('clang++') or shutil.which('g++')
if not compiler: raise SystemExit('Install a C++17 compiler to run heatmap checks.')
with tempfile.TemporaryDirectory(prefix='watchtower-heatmap-') as directory:
 path=Path(directory);file=path/'test.cpp';file.write_text(stub+code+tests)
 subprocess.run([compiler,'-std=c++17',str(file),'-o',str(path/'test')],check=True)
 subprocess.run([str(path/'test')],check=True)
