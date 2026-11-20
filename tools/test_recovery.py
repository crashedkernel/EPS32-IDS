#!/usr/bin/env python3
"""Exercise actual recovery monitor using simulated time, resets, tasks and NVS."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[1]
stub = r'''
#include <cassert>
#include <cstring>
#include <iostream>
#include <cstdint>
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
using TaskHandle_t=void*;
#define pdPASS 1
#define pdMS_TO_TICKS(x) x
static uint32_t nowMs=0; uint32_t millis(){return nowMs;}
void vTaskDelay(unsigned){}
static bool taskFail=false;
int xTaskCreate(void(*)(void*),const char*,unsigned,void*,int,TaskHandle_t*out){if(taskFail)return 0;*out=(void*)1;return 1;}
enum {ESP_RST_POWERON,ESP_RST_BROWNOUT,ESP_RST_SW};
static int resetReason=ESP_RST_POWERON;
int esp_reset_reason(){return resetReason;}
struct {int restarts=0;void restart(){++restarts;}} ESP;
struct {bool present=false;unsigned char data[64]{};
 size_t getBytesLength(const char*){return present?16:0;}
 size_t getBytes(const char*,void*out,size_t n){memcpy(out,data,n);return n;}
} prefs;
#include "RecoveryMonitor.h"
void boot(){recoveryTask=nullptr;setupRecovery();}
int main(){
 boot();assert(!recovery.settings.enabled);assert(!recoveryTask);
 RecoverySettings s;s.enabled=1;assert(validRecoverySettings(s));
 s.enabled=2;assert(!validRecoverySettings(s));s.enabled=1;
 taskFail=true;assert(!applyRecoverySettings(s));assert(!recovery.state.ready);
 taskFail=false;assert(applyRecoverySettings(s));assert(recovery.state.ready);
 nowMs=119999;recoveryTick();assert(ESP.restarts==0);
 nowMs=120000;recoveryLoopBeat();recoveryTick();assert(ESP.restarts==0);
 {RecoverySSHScope scope;nowMs+=119999;recoveryLoopBeat();recoveryTick();assert(ESP.restarts==0);
 ++nowMs;recoveryLoopBeat();recoveryTick();assert(ESP.restarts==1);assert(recoveryBoot.reason==2);}
 assert(!recovery.state.sshBusy);recoveryTick();assert(ESP.restarts==1);
 prefs.present=true;memcpy(prefs.data,&s,sizeof(s));resetReason=ESP_RST_SW;
 for(unsigned count=1;count<=3;++count){boot();assert(recovery.state.restarts==count);
 if(count<3){nowMs+=120000;recoveryTick();assert(recoveryBoot.pending);}}
 assert(recovery.state.paused);assert(!recoveryTask);
 nowMs+=1000000;recoveryTick();assert(ESP.restarts==3);
 boot();assert(!recovery.state.paused);assert(recovery.state.restarts==0);
 // Healthy runtime clears the consecutive-recovery budget.
 recovery.state.restarts=2;nowMs+=300000;recoveryLoopBeat();recoveryTick();assert(!recovery.state.restarts);
 // Routine SSH failure/scope exit and an idle reusable session are safe.
 {RecoverySSHScope scope;}nowMs+=900000;recoveryLoopBeat();recoveryTick();assert(ESP.restarts==3);
 s.enabled=0;applyRecoverySettings(s);nowMs+=900000;recoveryTick();assert(ESP.restarts==3);
 // Millisecond rollover preserves grace and timeout arithmetic.
 s.enabled=1;nowMs=UINT32_MAX-60000;applyRecoverySettings(s);nowMs+=120000;
 recoveryLoopBeat();recoveryTick();assert(ESP.restarts==3);
 nowMs+=60000;recoveryTick();assert(ESP.restarts==4);
 // Corrupt persistent data safely defaults off; power-on discards retained history.
 prefs.data[0]=99;resetReason=ESP_RST_POWERON;boot();assert(!recovery.settings.enabled);assert(!recovery.state.restarts);
 std::cout<<"Recovery monitor tests passed\n";
}
'''
with tempfile.TemporaryDirectory(prefix='watchtower-recovery-') as tmp:
 p=Path(tmp);(p/'esp_attr.h').write_text('#define RTC_NOINIT_ATTR\n');(p/'test.cpp').write_text(stub)
 subprocess.run(['clang++','-std=c++17','-fsanitize=undefined','-I'+str(p),'-I'+str(root/'IntrusionMonitor'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
