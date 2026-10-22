import pathlib,sys,subprocess,hashlib,json,random
w=pathlib.Path(sys.argv[1]);cc=w/'seed/out/omc';src=w/'seed/native/streamcopy.om';exe=w/'build/manual-migration/streamcopy';exe.parent.mkdir(parents=True,exist_ok=True)
assert hashlib.sha256(cc.read_bytes()).hexdigest()=='01b1545b23e8ff40cb5691e20b5df910ea4acaefde3495f0d62d270465b98cd6'
r=subprocess.run([str(cc)],input=src.read_bytes(),capture_output=True);assert r.returncode==0,r.stderr;assert r.stdout[:4]==b'\x7fELF' and len(r.stdout)>120
exe.write_bytes(r.stdout);exe.chmod(0o700);rng=random.Random(550);rows=[]
for n in [0,1,2,7,63,64,255,256,4095,4096,4097,8191,8192,8193,16383,16384,16385,1000000,8388608]:
 data=rng.randbytes(n);r=subprocess.run([str(exe)],input=data,capture_output=True,timeout=30)
 rows.append({'case':'bytes-'+str(n),'pass':r.returncode==0 and r.stdout==data and not r.stderr,'exit':r.returncode})
data=bytes(8388609);r=subprocess.run([str(exe)],input=data,capture_output=True,timeout=30)
rows.append({'case':'over-capacity','pass':r.returncode==3 and len(r.stdout)<=8388608 and r.stdout==data[:len(r.stdout)],'exit':r.returncode})
for name,shell in [('closed-input','exec "$1" 0<&-'),('closed-output','exec "$1" 1>&-'),('full-output','exec "$1" > /dev/full')]:
 r=subprocess.run(['sh','-c',shell,'sh',str(exe)],input=b'nonempty',capture_output=True,timeout=10)
 rows.append({'case':name,'pass':r.returncode==2 and not r.stdout,'exit':r.returncode})
print(json.dumps({'cases':len(rows),'failures':sum(not x['pass'] for x in rows),'results':rows,'source_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest()},indent=2));sys.exit(any(not x['pass'] for x in rows))
