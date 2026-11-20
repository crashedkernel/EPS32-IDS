#!/usr/bin/env python3
"""Test actual Stealth handler and RGB output decisions without physical hardware."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'IntrusionMonitor/IntrusionMonitor.ino').read_text()
settings = source[source.index('enum LedState'):source.index('static String ledSettingsSection')]
start = source.index('  server.on("/admin/stealth", HTTP_POST, []() {')
start = source.index('\n', start) + 1
end = source.index('\n  });', start)
handler = 'void toggleStealth() {\n' + source[start:end] + '\n}\n'
renderer = source[source.index('static void updateStatusLed()'):source.index('// GPIO0 is BOOT.')]
stub = r'''
#include <string>
#include <map>
#include <vector>
#include <array>
#include <cstdint>
#include <cassert>
#include <iostream>
struct String: std::string {using std::string::string;String(const std::string&s):std::string(s){}};
struct {std::map<std::string,String> args;int code=0;String body;
  String arg(const char*k){return args[k];}
  void send(int c,const char*,String s){code=c;body=s;}
} server;
struct {bool fail=false;unsigned writes=0;size_t putBytes(const char*,const void*,size_t n){++writes;return fail?0:n;}} prefs;
static bool authenticated=true;
bool adminRequest(bool){if(!authenticated)server.code=401;return authenticated;}
static uint32_t nowMs=1000;
uint32_t millis(){return nowMs;}
uint32_t uptimeSeconds(){return nowMs/1000;}
bool networkReady(){return true;}
constexpr int pdTRUE=1;
static bool canLock=true;
int lock=0;
int xSemaphoreTake(int,int){return canLock?pdTRUE:0;}
void xSemaphoreGive(int){}
enum Policy{Unknown,Allowed,Acknowledged};
struct Device{bool online=true;Policy policy=Allowed;};
Device devices[1];unsigned deviceCount=1;
Policy effectivePolicy(const Device &d,uint32_t){return d.policy;}
bool completed=true,scanning=false;
std::vector<std::array<unsigned,4>> output;
void rgbLedWrite(unsigned pin,unsigned r,unsigned g,unsigned b){output.push_back({pin,r,g,b});}
'''
tests = r'''
int main(){
  server.args["action"]="toggle";
  toggleStealth();assert(server.code==200 && !leds.enabled && server.body.find("false")!=std::string::npos);
  server.args["enabled"]="no"; // A stale checkbox must not override a toggle.
  toggleStealth();assert(leds.enabled && server.body.find("true")!=std::string::npos);
  prefs.fail=true;toggleStealth();assert(server.code==500 && leds.enabled);prefs.fail=false;
  server.args.clear();toggleStealth();assert(server.code==400 && leds.enabled);
  authenticated=false;toggleStealth();assert(server.code==401 && leds.enabled);authenticated=true;
  updateStatusLed();assert((output.back()==std::array<unsigned,4>{21,0,51,0}));
  server.args["action"]="toggle";toggleStealth();nowMs+=40;canLock=false;
  updateStatusLed();assert((output.back()==std::array<unsigned,4>{21,0,0,0}));
  toggleStealth();nowMs+=40;canLock=true;
  updateStatusLed();assert((output.back()==std::array<unsigned,4>{21,0,51,0}));
  std::cout<<"LED checks passed: saved-state toggle, stale form, save failure, invalid requests, authentication, RGB off and restore.\n";
}
'''
compiler = shutil.which('clang++') or shutil.which('g++')
if not compiler:
    raise SystemExit('Install a C++17 compiler to run LED checks.')
with tempfile.TemporaryDirectory(prefix='watchtower-leds-') as directory:
    file = Path(directory) / 'test.cpp'
    file.write_text(stub + settings + handler + renderer + tests)
    binary = Path(directory) / 'test'
    subprocess.run([compiler, '-std=c++17', str(file), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
