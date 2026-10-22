import pathlib,sys,subprocess,json,re,random,hashlib
w=pathlib.Path(sys.argv[1]).resolve();cc=w/'seed/out/omc';src=w/'seed/native/diagprefix.om';out=w/'build/manual-migration/diagprefix';out.parent.mkdir(parents=True,exist_ok=True)
assert hashlib.sha256(cc.read_bytes()).hexdigest()=='01b1545b23e8ff40cb5691e20b5df910ea4acaefde3495f0d62d270465b98cd6'
p=subprocess.run([str(cc)],input=src.read_bytes(),capture_output=True);assert p.returncode==0,p.stderr;assert p.stdout[:4]==b'\x7fELF' and len(p.stdout)>120;out.write_bytes(p.stdout);out.chmod(0o700)
inputs=[b'',b'o',b'om',b'om4',b'om4:',b'omc:',b'om4: line 7: error 9\n',b'prefix om4: error\n',b'om4:x\nomc:y\nomq:z\n',b'\x00om4:\nom4:\x00\xff',b'\nom4:\r\nomc:\r\n',b'om4:omc:',b' om4:\n\tomc:',b'x'*65536,b'om4:'+b'x'*65532]
rng=random.Random(550)
for n in [1,2,3,4,5,31,32,255,4095,4096,4097,8191,8192,8193]:
 data=rng.randbytes(n);inputs.extend([data,b'om4:'+data,b'omc:'+data,b'\n'+data+b'\nomc:end'])
rows=[]
for i,data in enumerate(inputs):
 expected=re.sub(rb'(?m)^om[4c]:',b'X:',data)
 sed=subprocess.run(['sed','s/^om[4c]:/X:/'],input=data,capture_output=True,check=True);assert sed.stdout==expected,(i,sed.stdout,expected)
 r=subprocess.run([str(out)],input=data,capture_output=True,timeout=10);rows.append({'case':i,'length':len(data),'pass':r.returncode==0 and r.stdout==expected and not r.stderr,'exit':r.returncode})
for label,argv,data,rc in [('capacity',[str(out)],bytes(65537),3),('closed-input',['sh','-c','exec "$1" 0<&-','sh',str(out)],b'',2),('full-output',['sh','-c','exec "$1" > /dev/full','sh',str(out)],b'om4:x',2)]:
 r=subprocess.run(argv,input=data,capture_output=True,timeout=10);rows.append({'case':label,'pass':r.returncode==rc and not r.stdout,'exit':r.returncode})
print(json.dumps({'cases':len(rows),'failures':sum(not r['pass'] for r in rows),'results':rows,'source_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'binary_sha256':hashlib.sha256(out.read_bytes()).hexdigest()},indent=2));sys.exit(any(not r['pass'] for r in rows))
