#!/usr/bin/env python3
"""Replay the actual panel adapter, including initialization before parameters exist."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'WhateverXe/kern_host.cpp').read_text()
body=s[s.index('bool XeHost::wrapPanelSetDisplay'):s.index('void XeHost::wrapFramebufferInit')]
code=r'''
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <map>
#include <string>
#include <vector>
#include "kern_backlight_profile.hpp"
struct Object {
 unsigned refs=1;bool alive=true;
 virtual ~Object()=default;virtual void dispose(){}
 void retain(){assert(alive && refs);++refs;}
 void release(){assert(alive && refs);if(!--refs){alive=false;dispose();}}
};
struct OSString:Object {std::string value;explicit OSString(const char *s):value(s){}bool isEqualTo(const char *s){return value==s;}const char *getCStringNoCopy(){return value.c_str();}};
struct OSNumber:Object {unsigned value;explicit OSNumber(unsigned v):value(v){}unsigned unsigned32BitValue(){return value;}};
struct OSData:Object {std::vector<uint8_t> data;OSData(const void *p,size_t n):data((const uint8_t*)p,(const uint8_t*)p+n){}static OSData *withBytes(const void *p,size_t n){return new OSData(p,n);}};
struct OSDictionary:Object {
 std::map<std::string,Object*> entries;
 Object *getObject(const char *key){auto it=entries.find(key);return it==entries.end()?nullptr:it->second;}
 bool setObject(const char *key,Object *o){assert(alive);o->retain();auto old=getObject(key);entries[key]=o;if(old)old->release();return true;}
 void removeObject(const char *key){auto old=getObject(key);entries.erase(key);if(old)old->release();}
 void dispose() override {for(auto &e:entries)e.second->release();entries.clear();}
 static OSDictionary *withDictionary(OSDictionary *o){auto copy=new OSDictionary;for(auto &e:o->entries)copy->setObject(e.first.c_str(),e.second);return copy;}
};
struct IOService:Object {
 IOService *parent=nullptr;bool internal=false;OSDictionary props;
 IOService *getProvider(){return parent;}Object *getProperty(const char *s){return props.getObject(s);}
 bool setProperty(const char *s,Object *v){return props.setObject(s,v);}
 bool setProperty(const char *s,const char *v){return props.setObject(s,new OSString(v));}
 bool setProperty(const char *s,const void *v,unsigned n){return props.setObject(s,new OSData(v,n));}
 virtual IOService *metaCast(const char *s){return internal && !strcmp(s,"AppleBacklightDisplay")?this:nullptr;}
};
using IOPCIDevice=IOService;
constexpr unsigned kIOReturnSuccess=0;
struct IOFramebuffer:IOService {
 IOService *metaCast(const char *s) override {return !strcmp(s,"IOFramebuffer")?this:IOService::metaCast(s);}
 uintptr_t low=0,high=65535;unsigned status=0,queries=0;
 unsigned getAttributeForConnection(unsigned index,unsigned key,uintptr_t *v){assert(index==0 && key==0x626b6c74);++queries;v[0]=20000;v[1]=low;v[2]=high;return status;}
};
#define OSDynamicCast(type,p) dynamic_cast<type *>(p)
#define FunctionCast(type,p) p
unsigned calls=0;bool consume=false;
OSData *selected=nullptr;
bool native(IOService *panel,IOService *){
 ++calls;
 if(consume){
  auto profiles=dynamic_cast<OSDictionary*>(panel->getProperty("ApplePanels"));
  selected=dynamic_cast<OSData*>(profiles->getObject("F19T1234"));assert(selected);
  panel->props.removeObject("ApplePanels");
  // AppleBacklight's actual order: remove the candidate dictionary, then
  // retain the previously borrowed curve in the selected profile.
  assert(selected->alive);
  auto chosen=new OSDictionary;chosen->setObject("F19T1234",selected);
  panel->setProperty("ApplePanel",chosen);chosen->release();
 }
 return true;
}
struct XeHost {static XeHost *callbackHost;IOPCIDevice *backlightGPU=nullptr;bool backlightPanelEDP=false;bool (*orgPanelSetDisplay)(IOService*,IOService*)=native;static bool wrapPanelSetDisplay(IOService*,IOService*);};XeHost *XeHost::callbackHost;
'''+body+r'''
int main(){
 for(unsigned test=0;test<12;++test) {
  XeHost host;XeHost::callbackHost=&host;IOService gpu,other,pnlf,panel,display,connect;IOFramebuffer fb;
  host.backlightGPU=&gpu;panel.parent=&pnlf;display.parent=&connect;connect.parent=&fb;fb.parent=test==1?&other:&gpu;
  display.internal=test!=2;OSString uid(test==3?"16":"19");pnlf.props.setObject("_UID",&uid);
  OSNumber product(0x1234);display.props.setObject("DisplayProductID",&product);
  OSDictionary profiles;OSData custom("custom",6);panel.props.setObject("ApplePanels",&profiles);
  if(test==4)fb.high=4095;if(test==5)fb.status=1;if(test==6)fb.low=1;
  if(test==7)profiles.setObject("F19T1234",&custom);
  if(test==8)profiles.setObject("F19",&custom);
  if(test==9)panel.props.setObject("ApplePanel",&profiles);
  if(test==10)pnlf.props.entries.clear();if(test==11)display.props.entries.clear();
  calls=0;consume=test==0;selected=nullptr;assert(XeHost::wrapPanelSetDisplay(&panel,&display));assert(calls==1);
  auto result=dynamic_cast<OSDictionary*>(panel.getProperty("ApplePanels"));
  if(test==0) {
   assert(!result);assert(!profiles.getObject("F19T1234"));
   auto data=selected;assert(data->alive && data->refs==1);
   assert(data && data->data.size()==sizeof(XeBacklightProfile::curve));
   assert(!memcmp(data->data.data(),XeBacklightProfile::curve,data->data.size()));
   assert(panel.getProperty("WhateverXeBacklightProfile"));
  } else {assert(result==&profiles);assert(!panel.getProperty("WhateverXeBacklightProfile"));}
  if(test==1 || test==2)assert(!fb.queries);
 }
 puts("PASS: actual backlight adapter before IODisplayParameters, ownership, external/UID/range exclusions, native ownership handoff, custom profile preservation");
}
'''
with tempfile.TemporaryDirectory(prefix='wex-backlight-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
