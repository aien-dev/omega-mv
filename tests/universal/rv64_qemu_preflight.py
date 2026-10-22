#!/usr/bin/env python3
"""Test-only Linux RV64I ELF controls for QEMU preflight, not an Omega backend."""
from pathlib import Path
import argparse,hashlib,json,struct,subprocess,re,platform
ap=argparse.ArgumentParser();ap.add_argument('--qemu',required=True);ap.add_argument('--out',required=True);a=ap.parse_args();out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=False);qemu=Path(a.qemu).resolve()
header=Path('/usr/include/elf.h').read_text()
def elf_constant(name):
 m=re.search(r'^#define\s+'+name+r'\s+(\d+)',header,re.M);assert m,name;return int(m.group(1))
isa=elf_constant('EM_RISCV');wrongisa=elf_constant('EM_AARCH64')
def addi(rd,rs1,imm):
 assert 0<=rd<32 and 0<=rs1<32 and -2048<=imm<2048
 return ((imm&4095)<<20)|(rs1<<15)|(rd<<7)|0x13
# Independent QEMU execution below checks that these actual encoded fields
# produce distinct Linux syscall exit observations, not just expected hashes.
def elf(exit_code,machine):
 code=struct.pack('<III',addi(10,0,exit_code),addi(17,0,93),0x73)
 size=4096+len(code);base=0x10000
 ident=b'\x7fELF'+bytes([2,1,1,0])+bytes(8)
 eh=struct.pack('<16sHHIQQQIHHHHHH',ident,2,machine,1,base+4096,64,0,0,64,56,1,0,0,0)
 ph=struct.pack('<IIQQQQQQ',1,5,0,base,base,size,size,4096)
 return eh+ph+bytes(4096-len(eh)-len(ph))+code
records=[]
for name,value,machine,want in [('exit42',42,isa,42),('exit43',43,isa,43),('wrong-isa',42,wrongisa,255)]:
 p=out/(name+'.elf');p.write_bytes(elf(value,machine));p.chmod(0o700)
 cmd=[str(qemu),'-cpu','rv64',str(p)];r=subprocess.run(cmd,capture_output=True,timeout=10)
 assert r.returncode==want,(cmd,r.returncode,r.stderr)
 if name=='wrong-isa':assert b'Invalid ELF image for this architecture' in r.stderr,r.stderr
 records.append({'case':name,'command':cmd,'exit':r.returncode,'stdout_hex':r.stdout.hex(),'stderr':r.stderr.decode(errors='replace'),'image_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'instruction_words_le':p.read_bytes()[4096:].hex()})
version=subprocess.check_output([str(qemu),'--version'],text=True)
report={'schema':'omega.universal.rv64-emulator-preflight.v1','result':'PASS_TEST_TOOL_PREFLIGHT_ONLY','execution':'EMULATED','guest':'RV64I Linux userspace syscall smoke, not complete RV64GC qualification','qemu_version':version,'qemu_binary_sha256':hashlib.sha256(qemu.read_bytes()).hexdigest(),'host':platform.uname()._asdict(),'cases':records,'ELF_EM_RISCV':isa,'ELF_EM_AARCH64':wrongisa,'system_elf_header_sha256':hashlib.sha256(Path('/usr/include/elf.h').read_bytes()).hexdigest(),'source_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'omega_backend_implemented':False,'native_riscv_silicon':'NOT_RUN','system_boot':'NOT_RUN'}
(out/'receipt.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS: RV64I exit42/exit43 plus wrong-ISA refusal under QEMU; EMULATED test-tool preflight only')
