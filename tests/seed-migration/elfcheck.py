import pathlib,subprocess,struct,json,hashlib,sys
w=pathlib.Path(sys.argv[1]); src=w/'seed/native/elfcheck.om'; cc=w/'seed/out/omc'; dst=w/'build/manual-migration/elfcheck'
assert hashlib.sha256(cc.read_bytes()).hexdigest()=='01b1545b23e8ff40cb5691e20b5df910ea4acaefde3495f0d62d270465b98cd6'
p=subprocess.run([str(cc)],input=src.read_bytes(),capture_output=True)
assert p.returncode==0,p.stderr
assert p.stdout[:4]==b'\x7fELF' and len(p.stdout)>120
dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(p.stdout);dst.chmod(0o700)
# Fixtures independently assembled from ELF64 ABI fields, not production parser.
def elf(headers=None):
 if headers is None:headers=[(1,5,0,0x400000,0,120,120,4096)]
 size=64+56*len(headers)
 ident=b'\x7fELF'+bytes([2,1,1,0])+bytes(8)
 hdr=struct.pack('<HHIQQQIHHHHHH',2,183,1,0x400000,64,0,0,64,56,len(headers),0,0,0)
 return ident+hdr+b''.join(struct.pack('<IIQQQQQQ',*h) for h in headers)
def change(b,off,fmt,v):
 b=bytearray(b);struct.pack_into(fmt,b,off,v);return bytes(b)
base=elf();cases=[('minimal-static',base,0),('real-compiler',cc.read_bytes(),0),('self',dst.read_bytes(),0)]
for n in range(120):cases.append(('truncated-'+str(n),base[:n],2))
for name,off,fmt,value in [('magic',0,'B',0),('class',4,'B',1),('endian',5,'B',2),('ident-version',6,'B',0),('type',16,'H',3),('machine',18,'H',62),('version',20,'I',0),('ehsize',52,'H',63),('phentsize',54,'H',55),('zero-phnum',56,'H',0),('extended-phnum',56,'H',65535),('phoff-wrap',32,'Q',2**64-1),('phoff-in-header',32,'Q',1),('filesz-past-eof',96,'Q',121),('filesz-over-mem',104,'Q',119),('offset-wrap',72,'Q',2**64-1),('vaddr-wrap',80,'Q',2**64-1)]:cases.append((name,change(base,off,fmt,value),2))
cases.extend([('entry-outside',change(base,24,'Q',0x500000),2),('nonexecutable-load',change(base,68,'I',4),2),('entry-in-zero-fill',change(change(base,104,'Q',4096),24,'Q',0x400100),2)])
for typ in [2,3]:cases.append(('dynamic-'+str(typ),elf([(1,5,0,0x400000,0,176,176,4096),(typ,4,0,0,0,0,0,1)]),3))
cases.extend([('no-load',elf([(4,4,0,0,0,0,0,1)]),2),('huge-input',base+bytes(8388609-len(base)),4),('empty',b'',2)])
fail=[]
for name,data,expected in cases:
 p=subprocess.run([str(dst)],input=data,capture_output=True,timeout=5)
 if p.returncode!=expected or p.stdout:fail.append({'case':name,'expected':expected,'actual':p.returncode,'stdout':p.stdout.hex()})
r={'cases':len(cases),'failures':len(fail),'examples':fail[:20],'source_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'binary_sha256':hashlib.sha256(dst.read_bytes()).hexdigest(),'scope':'ELF format and dynamic-segment rejection, not complete static dependency ancestry'}
print(json.dumps(r,indent=2));sys.exit(bool(fail))
