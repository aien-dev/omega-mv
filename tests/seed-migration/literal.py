#!/usr/bin/env python3
"""Independent host oracle for the handwritten seeded integer reader; no C oracle."""
import hashlib, json, pathlib, random, struct, subprocess, sys
root=pathlib.Path(sys.argv[1]); compiler=root/'seed/out/omc'
assert hashlib.sha256(compiler.read_bytes()).hexdigest()=='01b1545b23e8ff40cb5691e20b5df910ea4acaefde3495f0d62d270465b98cd6'
source=root/'seed/native/literal.om'; out=root/'build/manual-migration';out.mkdir(parents=True,exist_ok=True)
binary=out/'literal'
p=subprocess.run([str(compiler)],input=source.read_bytes(),capture_output=True)
assert p.returncode==0, p.stderr
assert p.stdout[:4]==b'\x7fELF' and len(p.stdout)>120
binary.write_bytes(p.stdout);binary.chmod(0o700)
def oracle(s):
 if not s or len(s)>4096: return 2,b''
 base=10; digits=s
 if s.startswith(b'0x'): base=16;digits=s[2:]
 elif s.startswith(b'0b'): base=2;digits=s[2:]
 alphabet=b'0123456789abcdef'[:base]
 if not digits or any(c not in alphabet for c in digits.lower()):return 2,b''
 n=int(digits,base)
 if n>2**64-1:return 3,b''
 return 0,struct.pack('<Q',n)
cases=[b'',b'0',b'00',b'0x',b'0b',b'0X1',b'0B1',b'-1',b'+1',b'1 ',b' 1',b'1\n',b'0xG',b'0b2',b'12z',b'1_0',b'\x00',b'\xff',b'18446744073709551615',b'18446744073709551616',b'0xffffffffffffffff',b'0x10000000000000000',b'0b'+b'1'*64,b'0b1'+b'0'*64,b'0'*4096,b'0'*4097,b'9'*4096,b'9'*4095+b'z']
rng=random.Random(550)
for _ in range(256):
 n=rng.getrandbits(65)
 cases.extend([str(n).encode(),hex(n).encode(),bin(n).encode()])
fail=[]
for i,s in enumerate(cases):
 expected=oracle(s);p=subprocess.run([str(binary)],input=s,capture_output=True,timeout=3)
 if (p.returncode,p.stdout)!=expected:fail.append({'case':i,'input':s[:80].hex(),'wanted_rc':expected[0],'actual_rc':p.returncode,'wanted':expected[1].hex(),'actual':p.stdout.hex()})
report={'cases':len(cases),'failures':len(fail),'examples':fail[:12],'compiler_sha256':hashlib.sha256(compiler.read_bytes()).hexdigest(),'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'scope':'isolated integer literal reader, NOT whole lexer/compiler or production retirement'}
print(json.dumps(report,indent=2));sys.exit(bool(fail))
