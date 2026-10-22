import pathlib,sys,subprocess,hashlib,tempfile,os,json,threading
w=pathlib.Path(sys.argv[1]);cc=w/'seed/out/omc';src=w/'seed/native/byteequal.om';exe=w/'build/manual-migration/byteequal';exe.parent.mkdir(parents=True,exist_ok=True)
assert hashlib.sha256(cc.read_bytes()).hexdigest()=='01b1545b23e8ff40cb5691e20b5df910ea4acaefde3495f0d62d270465b98cd6'
p=subprocess.run([str(cc)],input=src.read_bytes(),capture_output=True);assert p.returncode==0,p.stderr
assert p.stdout[:4]==b'\x7fELF' and len(p.stdout)>120
exe.write_bytes(p.stdout);exe.chmod(0o700)
# Shell establishes the documented descriptor interface; it is not the comparator.
def run(a,b):
 with tempfile.TemporaryDirectory(dir=exe.parent) as d:
  pa=pathlib.Path(d)/'a';pb=pathlib.Path(d)/'b';pa.write_bytes(a);pb.write_bytes(b)
  return subprocess.run(['sh','-c','exec "$1" 3<"$2" 4<"$3"','sh',str(exe),str(pa),str(pb)],capture_output=True,timeout=15)
cases=[]
for n in [0,1,7,4095,4096,4097,8192,16385]:
 a=bytes((i*37)%256 for i in range(n));cases.append(('equal-'+str(n),a,a,0))
 if n:
  for pos in sorted(set([0,n//2,n-1])):
   b=bytearray(a);b[pos]^=1;cases.append(('different-'+str(n)+'-'+str(pos),a,bytes(b),1))
 cases.extend([('longer-'+str(n),a,a+b'x',1),('shorter-'+str(n),a+b'x',a,1)])
fail=[]
for name,a,b,rc in cases:
 p=run(a,b)
 if (p.returncode,p.stdout,p.stderr)!=(rc,b'',b''):fail.append({'case':name,'wanted':rc,'actual':p.returncode})
# Explicit missing fd (shell closes both even if inherited).
p=subprocess.run(['sh','-c','exec "$1" 3<&- 4<&-','sh',str(exe)],capture_output=True)
if p.returncode!=2:fail.append({'case':'missing-fds','wanted':2,'actual':p.returncode})
# Two independent FIFOs write the same bytes with different fragmentation.
with tempfile.TemporaryDirectory(dir=exe.parent) as d:
 paths=[pathlib.Path(d)/'a',pathlib.Path(d)/'b']
 for path in paths:os.mkfifo(path)
 data=bytes(range(256))*40
 def writer(path,chunk):
  try:
   with open(path,'wb',buffering=0) as f:
    for pos in range(0,len(data),chunk):f.write(data[pos:pos+chunk])
  except BrokenPipeError:pass
 threads=[threading.Thread(target=writer,args=(path,chunk),daemon=True) for path,chunk in zip(paths,[7,101])]
 for t in threads:t.start()
 p=subprocess.run(['sh','-c','exec "$1" 3<"$2" 4<"$3"','sh',str(exe),str(paths[0]),str(paths[1])],capture_output=True,timeout=15)
 for t in threads:t.join(timeout=2)
 if p.returncode!=0:fail.append({'case':'fragmented-streams','wanted':0,'actual':p.returncode})
# Sparse zero files exercise the fixed 64 MiB boundary without a giant fixture.
with tempfile.TemporaryDirectory(dir=exe.parent) as d:
 pa=pathlib.Path(d)/'a';pb=pathlib.Path(d)/'b'
 for n,expected in [(67108864,0),(67108865,3)]:
  for path in [pa,pb]:
   with open(path,'wb') as f:f.truncate(n)
  p=subprocess.run(['sh','-c','exec "$1" 3<"$2" 4<"$3"','sh',str(exe),str(pa),str(pb)],capture_output=True,timeout=30)
  if p.returncode!=expected:fail.append({'case':'capacity-'+str(n),'wanted':expected,'actual':p.returncode})
print(json.dumps({'cases':len(cases)+4,'failures':len(fail),'examples':fail,'source_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest()},indent=2));sys.exit(bool(fail))
