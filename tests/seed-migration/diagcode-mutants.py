import pathlib,sys,subprocess,json
w=pathlib.Path(sys.argv[1]);p=w/'seed/native/diagcode.om';source=p.read_bytes();rows=[]
try:
 for name,old,new in [('anchor',b'ret eq get Buf p uchar 10',b'ret true'),('code-bytes',b'if ne get Buf add p j get Code j',b'if false'),('code-range',b'if gt v 127',b'if false')]:
  assert source.count(old)==1;p.write_bytes(source.replace(old,new));r=subprocess.run(['python3',str(w/'tests/seed-migration/diagcode.py'),str(w)],capture_output=True,text=True,timeout=60);assert r.stdout,r.stderr;d=json.loads(r.stdout);assert r.returncode==1 and d['failures']>0
  rows.append({'mutant':name,'compiled':True,'failures':d['failures']})
finally:p.write_bytes(source)
print(json.dumps(rows,indent=2))
