import pathlib,sys,subprocess,hashlib,json
w=pathlib.Path(sys.argv[1]);cc=w/'seed/out/omc';expected={}
for label,stem in [('check','elfcheck'),('literal','literal'),('equal','byteequal'),('hash','sha256')]:
 src=w/('seed/native/'+stem+'.om');p=subprocess.run([str(cc)],input=src.read_bytes(),capture_output=True);assert p.returncode==0,p.stderr
 expected[label]=hashlib.sha256(p.stdout).hexdigest();expected['seed/native/'+stem+'.om']=hashlib.sha256(src.read_bytes()).hexdigest()
for label in ['stage1','stage2','stage3']:expected[label]=hashlib.sha256(cc.read_bytes()).hexdigest()
p=subprocess.run(['sh',str(w/'tests/seed-migration/native-closure.sh')],capture_output=True,text=True,timeout=60)
assert p.returncode==0,(p.stdout,p.stderr)
observed={}
for line in p.stdout.splitlines():
 if line.startswith('OMEGA_SHA256 '):
  _,digest,label=line.split();assert label not in observed;observed[label]=digest
assert observed==expected,(observed,expected)
print(p.stdout,end='');print(json.dumps({'fingerprints_checked':len(observed),'independent_hashlib_matches':True,'status':'PASS'}))
