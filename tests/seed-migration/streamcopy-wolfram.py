import pathlib,sys,json,subprocess,hashlib
w=pathlib.Path(sys.argv[1]);r=json.loads((w/'evidence/OSC-MANUAL-MIGRATION/streamcopy-wolfram.json').read_text())['result']
assert hashlib.sha256(b'abc').hexdigest()==r['byteArrayABC']
data=bytes(range(256));p=subprocess.run([str(w/'build/manual-migration/streamcopy')],input=data,capture_output=True,check=True)
assert p.stdout==data and hashlib.sha256(p.stdout).hexdigest()==r['byteRangeSHA256']
assert r['capacityBytes']==r['bufferBytes']*r['fullBuffersAtCapacity'] and r['overCapacityInputBytes']==r['capacityBytes']+1
print(json.dumps({'all_byte_values_copied':True,'output_sha256':hashlib.sha256(p.stdout).hexdigest(),'matches_wolfram':True,'capacity_bytes':r['capacityBytes'],'status':'PASS'},indent=2))
