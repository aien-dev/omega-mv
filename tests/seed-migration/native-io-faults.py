import pathlib,sys,subprocess,tempfile,os,fcntl,hashlib,json
w=pathlib.Path(sys.argv[1]).resolve();cc=w/'seed/out/omc'
assert hashlib.sha256(cc.read_bytes()).hexdigest()=='01b1545b23e8ff40cb5691e20b5df910ea4acaefde3495f0d62d270465b98cd6'
base=w/'build/manual-migration';base.mkdir(parents=True,exist_ok=True)
payload=b'input consumed before EAGAIN\x00\xff'
oracle=json.loads((w/'evidence/OSC-MANUAL-MIGRATION/native-io-wolfram.json').read_text())['result']
assert len(payload)==oracle['readPrefixBytes']
rows=[];mutants=[]
with tempfile.TemporaryDirectory(prefix='io-faults-',dir=base) as td:
 d=pathlib.Path(td)
 def build(name,source):
  r=subprocess.run([str(cc)],input=source,capture_output=True);assert r.returncode==0,r.stderr;assert r.stdout[:4]==b'\x7fELF' and len(r.stdout)>120
  p=d/name;p.write_bytes(r.stdout);p.chmod(0o700);return p
 def read_fault(exe):
  rd,wr=os.pipe2(os.O_NONBLOCK)
  try:
   assert os.write(wr,payload)==len(payload)
   # Keep the writer open: after the first real read, the next read gets EAGAIN, not EOF.
   p=subprocess.run([str(exe)],stdin=rd,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=10)
   return p.returncode,p.stdout
  finally:os.close(rd);os.close(wr)
 def partial_write(exe):
  rd,wr=os.pipe2(os.O_NONBLOCK)
  try:
   capacity=fcntl.fcntl(wr,fcntl.F_SETPIPE_SZ,4096);assert capacity==4096
   data=bytes(range(256))*32;assert len(data)>capacity
   with tempfile.TemporaryFile() as source:
    source.write(data);source.seek(0)
    # Regular input gives a complete first buffer. Do not drain stdout until exit.
    p=subprocess.run([str(exe)],stdin=source,stdout=wr,stderr=subprocess.PIPE,timeout=10)
   out=os.read(rd,len(data));return p.returncode,out,data[:capacity],capacity
  finally:os.close(rd);os.close(wr)
 sha_source=(w/'seed/native/sha256.om').read_bytes();copy_source=(w/'seed/native/streamcopy.om').read_bytes()
 sha=build('sha',sha_source);copy=build('copy',copy_source)
 for name,exe,expected in [('sha-post-progress-read-error',sha,b''),('copy-post-progress-read-error',copy,payload)]:
  rc,out=read_fault(exe);assert rc==2 and out==expected,(name,rc,out)
  rows.append({'case':name,'exit':rc,'output_bytes':len(out),'status':'PASS'})
 with open('/dev/full','wb',buffering=0) as full:
  p=subprocess.run([str(sha)],input=b'abc',stdout=full,stderr=subprocess.PIPE,timeout=10)
 assert p.returncode==2;rows.append({'case':'sha-output-enospc','exit':p.returncode,'status':'PASS'})
 rc,out,expected,capacity=partial_write(copy);assert rc==2 and out==expected,(rc,len(out))
 assert capacity==oracle['pipeCapacity'] and hashlib.sha256(out).hexdigest()==oracle['partialPrefixSHA256']
 rows.append({'case':'copy-positive-partial-write-then-eagain','exit':rc,'pipe_capacity':capacity,'output_bytes':len(out),'output_sha256':hashlib.sha256(out).hexdigest(),'status':'PASS'})
 for name,source,old,new,probe,wanted in [
  ('sha-ignores-read-error',sha_source,b'if lt n 0\nret 2',b'if false\nret 2',read_fault,b''),
  ('copy-ignores-read-error',copy_source,b'if lt n 0\nret 2',b'if lt n 0\nret 0',read_fault,payload),
  ('copy-assumes-full-write',copy_source,b'sent = add sent ulong w',b'sent = ulong n',partial_write,None)]:
  assert source.count(old)==1;exe=build(name,source.replace(old,new));result=probe(exe)
  accepts=(result[0]==2 and result[1]==(result[2] if wanted is None else wanted))
  assert not accepts,(name,result)
  mutants.append({'mutant':name,'compiled':True,'caught':True,'exit':result[0],'output_bytes':len(result[1])})
 print(json.dumps({'cases':len(rows),'failures':0,'results':rows,'compiled_mutants':mutants,'source_sha256':{'sha256':hashlib.sha256(sha_source).hexdigest(),'streamcopy':hashlib.sha256(copy_source).hexdigest()},'scope':'real Linux nonblocking pipe failures; not EINTR or recovery after transient errors'},indent=2))
