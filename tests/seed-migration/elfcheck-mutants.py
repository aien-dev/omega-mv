import pathlib,subprocess,json,sys
w=pathlib.Path(sys.argv[1]);p=w/'seed/native/elfcheck.om';original=p.read_bytes();results=[]
mutants=[('dynamic','if dynamic\nret 3','if false\nret 3'),('segment','if not segment off','if false'),('entry','if entryok off','if true')]
try:
 for name,old,new in mutants:
  text=original.decode();assert text.count(old)==1
  p.write_text(text.replace(old,new))
  r=subprocess.run(['python3',str(w/'tests/seed-migration/elfcheck.py'),str(w)],capture_output=True,text=True)
  assert r.stdout,r.stderr
  d=json.loads(r.stdout)
  assert r.returncode==1 and d['failures']>0,(name,r.stdout,r.stderr)
  results.append({'mutant':name,'successfully_compiled':True,'failures':d['failures'],'examples':d['examples']})
finally:p.write_bytes(original)
print(json.dumps(results,indent=2))
