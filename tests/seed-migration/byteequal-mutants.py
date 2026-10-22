import pathlib,subprocess,json,sys
w=pathlib.Path(sys.argv[1]);p=w/'seed/native/byteequal.om';original=p.read_bytes();results=[]
mutants=[('bytes','if ne get a i get b i','if false'),('length','if ne na nb','if false'),('capacity','if gt ulong na sub ulong 67108864 total','if false')]
try:
 for name,old,new in mutants:
  text=original.decode();assert text.count(old)==1
  p.write_text(text.replace(old,new))
  r=subprocess.run(['python3',str(w/'tests/seed-migration/byteequal.py'),str(w)],capture_output=True,text=True)
  assert r.stdout,r.stderr
  d=json.loads(r.stdout)
  assert r.returncode==1 and d['failures']>0,(name,r.stdout,r.stderr)
  results.append({'mutant':name,'successfully_compiled':True,'failures':d['failures'],'examples':d['examples']})
finally:p.write_bytes(original)
print(json.dumps(results,indent=2))
