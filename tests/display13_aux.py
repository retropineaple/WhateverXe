#!/usr/bin/env python3
"""Actual AUX wrappers + native bridge; optional exact donor caller replay."""
from pathlib import Path
import hashlib,re,subprocess,sys,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
functions='\n'.join(re.search(re.escape(sig)+r'.*?\n\}',s,re.S).group() for sig in [
 'uint8_t *XE::display13SSTPort(', 'bool XE::display13HDMIPort(', 'bool XE::display13MainLinkAUX(',
 'uint32_t XE::wrapDisplay13EnablePort(', 'uint32_t XE::wrapDisplay13DisablePort(',
 'uint32_t XE::wrapDisplay13SignalLevels(', 'uint32_t XE::wrapDisplay13TrainingPattern(',
 'bool XE::display13AUXReady(', 'uint32_t XE::wrapDisplay13ReadAUX(', 'uint32_t XE::wrapDisplay13WriteAUX(',
 'uint32_t XE::wrapDisplay13I2CAUX(', 'uint32_t XE::wrapDisplay13RunAUX('])
binding=re.search(r'static TGLFBBinding::Readout adlnInspectPathBinding\(.*?\n\}',s,re.S).group()
objects=(r/'tests/tgl_fb_binding.cpp').read_text().split('int main()')[0].replace('port[0x580]','port[0x700]')
base=(r/'tests/display13_sst_fixture.cpp').read_text().split('int main(')[0]
base=base.replace('// NATIVE_OBJECTS',objects).replace('// BINDING',binding).replace('// FUNCTIONS',functions)
base=base.replace('struct OSObject {','void *nativePortObject=nullptr;\nstruct OSObject {').replace('return size;','return this==nativePortObject?0x700:size;')
base=base.replace('bool adlnWriterMappingBound()', 'void adlnFault(bool &fault,unsigned){fault=true;}\nbool adlnWriterMappingBound()')
header=(r/'WhateverXe/kern_xe.hpp').read_text()
extra=header[header.index(' bool display13FBAUXHooksReady'):header.index(' mach_vm_address_t orgDisplay13TrainingPattern')]
extra=extra.replace(' bool processDisplay13FramebufferAUX(KernelPatcher &,size_t,mach_vm_address_t,size_t);','').replace('mach_vm_address_t','uintptr_t')
base=base.replace('struct XE {','struct XE {\n'+extra)
base=base.replace('struct XE {','static int display13PanelRateIndex(void*,uint8_t,uint32_t(*)(void*,uint32_t,void*,uint32_t)){return -1;}\nstruct XE {\n bool display13PanelDP=false;static void display13AUXFailRecord(void*,uint32_t,uint32_t){}\n',1)
code=base+(r/'tests/display13_aux_fixture.cpp').read_text()
assert 'c.bridge.suspend(io)' in s
engine=s[s.index('void XE::wrapAlderLakeNEngineDisable('):s.index('void XE::wrapAlderLakeNPowerOverride(')]
assert '!c.bridge.auxBusy()' in engine
if len(sys.argv)>1:
 b=Path(sys.argv[1]).read_bytes();assert hashlib.sha256(b).hexdigest()=='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
 install=s[s.index('bool XE::processDisplay13FramebufferAUX('):s.index('bool XE::display13AUXReady(')]
 entry=bytes(int(x,0) for x in re.search(r'entry\[\]=\{([^}]+)',install).group(1).split(','))
 for off in [0x1cc72,0x1ceb0,0x1d2b6,0x61748]:assert b[off:off+len(entry)]==entry
 # AUX bank coordinates in both native Combo init branches.
 for start,ctl in [(0x5f0e2,0x64010),(0x5f2c2,0x64110)]:
  assert (ctl.to_bytes(4,'little')) in b[start:start+130]
 print('PASS exact donor AUX hooks, Combo bank coordinates and runtime suspend wiring')
with tempfile.TemporaryDirectory(prefix='display13-aux-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-arch','x86_64','-std=c++17','-O1','-g','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')]+sys.argv[1:],check=True)
