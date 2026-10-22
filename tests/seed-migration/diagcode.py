import pathlib,sys,subprocess,tempfile,re,json,hashlib
w=pathlib.Path(sys.argv[1]).resolve();cc=w/'seed/out/omc';src=w/'seed/native/diagcode.om';exe=w/'build/manual-migration/diagcode';exe.parent.mkdir(parents=True,exist_ok=True)
assert hashlib.sha256(cc.read_bytes()).hexdigest()=='01b1545b23e8ff40cb5691e20b5df910ea4acaefde3495f0d62d270465b98cd6'
source=src.read_bytes() if '--stub' not in sys.argv else b'fn main to int\nret 1\nend\n'
p=subprocess.run([str(cc)],input=source,capture_output=True);assert p.returncode==0,p.stderr;assert p.stdout[:4]==b'\x7fELF' and len(p.stdout)>120;exe.write_bytes(p.stdout);exe.chmod(0o700)
rows=[]
with tempfile.TemporaryDirectory(dir=w/'build/manual-migration') as td:
 expected=pathlib.Path(td)/'code'
 def run(label,code,data,want):
  expected.write_bytes(code);p=subprocess.run(['sh','-c','exec "$1" 3<"$2"','sh',str(exe),str(expected)],input=data,capture_output=True,timeout=10)
  rows.append({'case':label,'pass':p.returncode==want and not p.stdout,'expected':want,'exit':p.returncode})
 for code in [1,3,9,10,12,23,99,100,127]:
  c=str(code).encode()
  samples=[b'',b'om4: line 1: error '+c,b'omc: line 999: error '+c+b'\n',b'line : error '+c,b'prefix line 2: error '+c,b'line 1: error '+c+b'0',b'line 1: error 0'+c,b'line x: error '+c,b'line 3: error '+c+b'\r\n',b'line 1: error '+c+b' trailing\n',b'junk\nline 1: error '+c+b'\nmore',b'\x00line 1: error '+c+b'\n\xff']
  samples.append(b'line 1: error '+(b'2' if len(c)==1 else b'11' if len(c)==2 else b'111'))
  for i,data in enumerate(samples):
   want=0 if re.search(rb'line [0-9]*: error '+c+rb'$',data,re.M) else 1
   grep=subprocess.run(['grep','-q','line [0-9]*: error '+str(code)+'$'],input=data,capture_output=True);assert grep.returncode==want
   run(str(code)+'-'+str(i),c,data,want)
 for code in [b'',b'0',b'01',b'128',b'999',b'1234',b'1\n',b'-1',b'x']:run('bad-code-'+repr(code),code,b'line 1: error 1',4)
 data=b'x'*(65536-len(b'line 1: error 3'))+b'line 1: error 3';run('exact-capacity',b'3',data,0);run('over-capacity',b'3',data+b'\n',3)
 p=subprocess.run([str(exe)],input=b'line 1: error 3',capture_output=True);rows.append({'case':'missing-fd3','pass':p.returncode==2 and not p.stdout,'exit':p.returncode})
print(json.dumps({'cases':len(rows),'failures':sum(not r['pass'] for r in rows),'results':rows,'source_sha256':hashlib.sha256(source).hexdigest(),'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest()},indent=2));sys.exit(any(not r['pass'] for r in rows))
