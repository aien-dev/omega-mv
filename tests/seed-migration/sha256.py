import pathlib,subprocess,sys,json,hashlib,random,tempfile,os,threading
w=pathlib.Path(sys.argv[1]);cc=w/'seed/out/omc';src=w/'seed/native/sha256.om';exe=w/'build/manual-migration/sha256';exe.parent.mkdir(parents=True,exist_ok=True)
assert hashlib.sha256(cc.read_bytes()).hexdigest()=='01b1545b23e8ff40cb5691e20b5df910ea4acaefde3495f0d62d270465b98cd6'
r=subprocess.run([str(cc)],input=src.read_bytes(),capture_output=True);assert r.returncode==0,r.stderr
assert r.stdout[:4]==b'\x7fELF' and len(r.stdout)>120
exe.write_bytes(r.stdout);exe.chmod(0o700)
rng=random.Random(550)
cases=[b'',b'abc',b'a'*1000000,bytes(range(256))]
for n in list(range(130))+[255,256,511,512,8191,8192,8193,16384]:cases.append(rng.randbytes(n))
fail=[]
for i,b in enumerate(cases):
 p=subprocess.run([str(exe)],input=b,capture_output=True,timeout=30)
 expected=hashlib.sha256(b).hexdigest().encode()+b'\n'
 if (p.returncode,p.stdout)!=(0,expected):fail.append({'case':i,'length':len(b),'rc':p.returncode,'expected':expected.decode().strip(),'actual':p.stdout.hex()})
# Closed input is a named error and must not emit an empty-input digest.
p=subprocess.run(['sh','-c','exec "$1" 0<&-','sh',str(exe)],capture_output=True)
if p.returncode!=2 or p.stdout:fail.append({'case':'closed-input','rc':p.returncode,'actual':p.stdout.hex()})
# Independently fragmented writes must hash the same byte stream.
data=rng.randbytes(17003)
p=subprocess.Popen([str(exe)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
def writer():
 try:
  for i in range(0,len(data),13):p.stdin.write(data[i:i+13]);p.stdin.flush()
 except BrokenPipeError:pass
 finally:
  try:p.stdin.close()
  except BrokenPipeError:pass
t=threading.Thread(target=writer);t.start();out=p.stdout.read();err=p.stderr.read();rc=p.wait(timeout=30);t.join()
if rc or out!=hashlib.sha256(data).hexdigest().encode()+b'\n':fail.append({'case':'fragmented','rc':rc,'actual':out.hex()})
for n,expected_rc in [(8388608,0),(8388609,3)]:
 data=bytes(n);p=subprocess.run([str(exe)],input=data,capture_output=True,timeout=60)
 expected_out=hashlib.sha256(data).hexdigest().encode()+b'\n' if expected_rc==0 else b''
 if (p.returncode,p.stdout)!=(expected_rc,expected_out):fail.append({'case':'capacity-'+str(n),'rc':p.returncode})
print(json.dumps({'cases':len(cases)+4,'failures':len(fail),'examples':fail[:15],'source_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest()},indent=2));sys.exit(bool(fail))
