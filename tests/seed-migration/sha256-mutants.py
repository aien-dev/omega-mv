import pathlib,subprocess,json,sys
w=pathlib.Path(sys.argv[1]);p=w/'seed/native/sha256.om';original=p.read_bytes();results=[]
mutants=[('padding','hbyte 128','hbyte 0'),('stream','update 0 ulong n','update 0 0'),('capacity','if gt ulong n sub ulong 8388608 total','if false')]
try:
 for name,old,new in mutants:
  text=original.decode();assert text.count(old)==1
  p.write_text(text.replace(old,new))
  r=subprocess.run(['python3',str(w/'tests/seed-migration/sha256.py'),str(w)],capture_output=True,text=True)
  assert r.stdout,r.stderr
  d=json.loads(r.stdout)
  assert r.returncode==1 and d['failures']>0,(name,r.stdout,r.stderr)
  results.append({'mutant':name,'successfully_compiled':True,'failures':d['failures']})
finally:p.write_bytes(original)
print(json.dumps(results,indent=2))
