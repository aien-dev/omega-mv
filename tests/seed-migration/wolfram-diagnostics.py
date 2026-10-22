import pathlib,sys,json,hashlib,subprocess,tempfile
w=pathlib.Path(sys.argv[1]).resolve();oracle=json.loads((w/'evidence/OSC-MANUAL-MIGRATION/diagnostics-wolfram.json').read_text());b=w/'build/seed/birth'
assert oracle['abcSHA256']==hashlib.sha256(b'abc').hexdigest()
rows=[]
for name,row in zip(['cycle','assign'],oracle['fixtures'],strict=True):
 data=(w/('seed/native/diagnostics/'+name+'.txt')).read_bytes();assert data==bytes(row['bytes'])==row['text'].encode()
 r=subprocess.run([str(b/'sha256')],input=data,capture_output=True,check=True)
 assert r.stdout.decode()==row['sha256']+'\n';assert hashlib.sha256(data).hexdigest()==row['sha256']
 rows.append({'fixture':name,'native_sha256':r.stdout.decode().strip(),'matches_wolfram':True})
with tempfile.TemporaryDirectory(dir=w/'build/manual-migration') as td:
 p=pathlib.Path(td)
 for row in oracle['negativeControls']:
  (p/'actual').write_text(row['actual']);(p/'expected').write_text(row['expected'])
  r=subprocess.run(['sh','-c','exec "$1" 3<"$2" 4<"$3"','sh',str(b/'byteequal'),str(p/'actual'),str(p/'expected')],capture_output=True)
  assert row['legacySubstringAccepts'] and not row['exactBytesAccept'];assert r.returncode==1 and not r.stdout
print(json.dumps({'fixtures':rows,'negative_controls':len(oracle['negativeControls']),'status':'PASS'},indent=2))
