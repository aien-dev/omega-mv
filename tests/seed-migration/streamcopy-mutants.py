import pathlib,subprocess,sys,json
w=pathlib.Path(sys.argv[1]);p=w/'seed/native/streamcopy.om';original=p.read_bytes();rows=[]
try:
 for name,old,new in [('capacity','if gt ulong n sub ulong 8388608 total','if false'),('read-error','if lt n 0','if false'),('write-error','if le w 0\nret 2','if le w 0\nret 0')]:
  s=original.decode();assert s.count(old)==1;p.write_text(s.replace(old,new))
  r=subprocess.run(['python3',str(w/'tests/seed-migration/streamcopy.py'),str(w)],capture_output=True,text=True,timeout=60)
  assert r.stdout,r.stderr;d=json.loads(r.stdout);assert r.returncode==1 and d['failures']>0
  rows.append({'mutant':name,'compiled':True,'failures':d['failures']})
finally:p.write_bytes(original)
print(json.dumps(rows,indent=2))
