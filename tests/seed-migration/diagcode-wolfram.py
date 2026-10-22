import pathlib,sys,tempfile,json,subprocess
w=pathlib.Path(sys.argv[1]).resolve();rows=json.loads((w/'evidence/OSC-MANUAL-MIGRATION/diagcode-wolfram.json').read_text())['results']
with tempfile.TemporaryDirectory(dir=w/'build/manual-migration') as td:
 code=pathlib.Path(td)/'code'
 for r in rows:
  code.write_text(str(r['code']));p=subprocess.run(['sh','-c','exec "$1" 3<"$2"','sh',str(w/'build/manual-migration/diagcode'),str(code)],input=r['input'].encode(),capture_output=True)
  assert p.returncode==(0 if r['accept'] else 1) and not p.stdout,(r,p.returncode)
print(json.dumps({'wolfram_cases':len(rows),'status':'PASS'}))
