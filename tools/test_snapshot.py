#!/usr/bin/env python3
"""Check real Device array ownership and snapshot growth with a tracked host allocator."""
from pathlib import Path
import shutil
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
sdk=Path.home()/'Library/Arduino15/packages/esp32/tools/esp32s3-libs/3.3.12/include'
headers=list(sdk.glob('**/cJSON.h'))
compiler=shutil.which('clang++') or shutil.which('g++')
if not compiler or not headers:raise SystemExit('A C++17 compiler and Arduino ESP32 3.3.12 headers are required.')
allocator=r'''
#pragma once
#include <cstdlib>
#include <map>
#include <cassert>
constexpr unsigned MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
inline std::map<void*,size_t> owned;
inline bool failAll=false,failPSRAM=false;
inline unsigned allocations=0,frees=0;
inline void*heap_caps_malloc(size_t bytes,unsigned flags){
 if(failAll||(failPSRAM&&(flags&MALLOC_CAP_SPIRAM)))return nullptr;
 void*p=malloc(bytes);if(p){assert(!owned.count(p));owned[p]=bytes;++allocations;}return p;
}
inline void heap_caps_free(void*p){if(!p)return;assert(owned.erase(p)==1);++frees;free(p);}
'''
code=r'''
#include <iostream>
#include "OpenWrtData.h"
int main(){
 for(unsigned cycle=0;cycle<1000;++cycle){
  failPSRAM=cycle%2;auto*rows=new(std::nothrow)Device[16];assert(rows);
  RouterSnapshot snapshot{rows,0,16,0,UINT32_MAX};snapshot.canGrow=true;
  for(unsigned i=0;i<254;++i){
   if(snapshot.count==snapshot.capacity)assert(snapshot.expand());
   snapshot.rows[snapshot.count++].ip=i+1;
  }
  assert(snapshot.count==254&&snapshot.capacity==254&&!snapshot.expand());
  for(unsigned i=0;i<254;++i)assert(snapshot.rows[i].ip==i+1);
  delete[]snapshot.rows;assert(owned.empty());
 }
 auto*rows=new(std::nothrow)Device[16];assert(rows);rows[0].ip=42;
 RouterSnapshot snapshot{rows,16,16,0,UINT32_MAX};snapshot.canGrow=true;
 failAll=true;assert(!snapshot.expand()&&snapshot.allocationFailed&&snapshot.rows==rows&&rows[0].ip==42);
 failAll=false;delete[]snapshot.rows;assert(owned.empty()&&allocations==frees);
 std::cout<<"Snapshot checks passed: 1000 full-capacity cycles, PSRAM fallback, failed growth preservation, "<<allocations<<" allocations released.\n";
}
'''
with tempfile.TemporaryDirectory(prefix='watchtower-snapshot-') as directory:
 path=Path(directory);(path/'esp_heap_caps.h').write_text(allocator);file=path/'test.cpp';file.write_text(code)
 subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-fsanitize=undefined','-fno-sanitize-recover=all','-I',str(path),'-I',str(root/'IntrusionMonitor'),'-I',str(headers[0].parent),str(file),'-o',str(path/'test')],check=True)
 subprocess.run([str(path/'test')],check=True)
