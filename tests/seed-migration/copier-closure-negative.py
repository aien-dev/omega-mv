import pathlib,sys,subprocess,tempfile,json,shlex
w=pathlib.Path(sys.argv[1]).resolve();script=(w/'tests/seed-migration/native-closure.sh').read_text();rows=[]
anchor='"$CC" < seed/native/streamcopy.om > "$D/copy"';assert script.count(anchor)==1
for name,source,expected in [('empty-copy','fn main to int\nret 0\nend\n',2),('copy-error','fn main to int\nret 2\nend\n',2),('appended-byte',None,2)]:
 with tempfile.TemporaryDirectory(dir=w/'build/manual-migration') as td:
  d=pathlib.Path(td);s=script
  if source is not None:
   stub=d/'stub.om';stub.write_text(source);r=subprocess.run([str(w/'seed/out/omc')],input=source.encode(),capture_output=True);assert r.returncode==0 and r.stdout[:4]==b'\x7fELF' and len(r.stdout)>120
   s=s.replace(anchor,'"$CC" < '+shlex.quote(str(stub))+' > "$D/copy"')
  else:
   line='"$D/copy" < "$D/stage3" > "$D/copied"';assert s.count(line)==1;s=s.replace(line,line+'\nprintf x >> "$D/copied"')
  with tempfile.NamedTemporaryFile(mode='w',suffix='.sh',prefix='copier-negative-',dir=w/'tests/seed-migration',delete=False) as f:f.write(s);namepath=pathlib.Path(f.name)
  try:r=subprocess.run(['sh',str(namepath)],capture_output=True,text=True,timeout=60)
  finally:namepath.unlink()
  assert r.returncode==expected and 'PASS ' not in r.stdout,(name,r.returncode,r.stdout,r.stderr)
  if name!='copy-error':assert 'REFUSED: copied compiler differs' in r.stderr
  rows.append({'case':name,'exit':r.returncode,'refused':True,'compiled_stub':source is not None,'stderr':r.stderr})
print(json.dumps({'cases':len(rows),'failures':0,'results':rows},indent=2))
