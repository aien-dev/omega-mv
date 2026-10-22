import pathlib,sys,subprocess,json
w=pathlib.Path(sys.argv[1]);p=w/'seed/native/diagprefix.om';source=p.read_bytes();rows=[]
try:
 for name,old,new in [('line-start',b'if and start prefix src n',b'if prefix src n'),('replacement',b'put Buf dst uchar 88',b'put Buf dst uchar 89'),('read-error',b'if lt r 0\nret 2',b'if lt r 0\nret 0')]:
  assert source.count(old)==1;p.write_bytes(source.replace(old,new))
  r=subprocess.run(['python3',str(w/'tests/seed-migration/diagprefix.py'),str(w)],capture_output=True,text=True,timeout=60);assert r.stdout,r.stderr;d=json.loads(r.stdout);assert r.returncode==1 and d['failures']>0
  rows.append({'mutant':name,'compiled':True,'failures':d['failures']})
finally:p.write_bytes(source)
print(json.dumps(rows,indent=2))
