import pathlib,sys,json,subprocess,hashlib
w=pathlib.Path(sys.argv[1]);r=json.loads((w/'evidence/OSC-MANUAL-MIGRATION/diagprefix-wolfram.json').read_text())['result']
assert r['input'].encode()==bytes(r['inputBytes']);assert r['expected'].encode()==bytes(r['expectedBytes'])
p=subprocess.run([str(w/'build/manual-migration/diagprefix')],input=bytes(r['inputBytes']),capture_output=True,check=True)
assert p.stdout==bytes(r['expectedBytes']);assert hashlib.sha256(p.stdout).hexdigest()==r['sha256']
print(json.dumps({'matches_wolfram':True,'output_sha256':hashlib.sha256(p.stdout).hexdigest(),'status':'PASS'}))
