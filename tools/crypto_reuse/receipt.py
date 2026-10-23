#!/usr/bin/env python3
"""External experiment orchestrator; no cipher, signing or production dependency."""
import argparse,hashlib,json,os,pathlib,re,signal,subprocess,sys
SCHEMA='AIEN_CRYPTO_REUSE_RECEIPT_V1'
def digest(data):return hashlib.sha256(data).hexdigest()
def load(path):
 def unique(pairs):
  d={}
  for k,v in pairs:
   if k in d:raise ValueError('duplicate key '+k)
   d[k]=v
  return d
 return json.loads(pathlib.Path(path).read_text(),object_pairs_hook=unique)
def checked_contract(path,expected):
 if not re.fullmatch('[0-9a-f]{64}',expected):raise ValueError('expected contract SHA256 required')
 p=pathlib.Path(path).resolve()
 if digest(p.read_bytes())!=expected:raise ValueError('contract digest mismatch')
 d=load(p)
 if d['schema']!='AIEN_CRYPTO_REUSE_FIXTURE_V1':raise ValueError('fixture version')
 return d

def check_pins(contract):
 for a in contract['adapters']:
  root=pathlib.Path(a['root']).resolve()
  head=subprocess.run(['git','-C',str(root),'rev-parse','HEAD'],capture_output=True,text=True,check=True).stdout.strip()
  if not re.fullmatch('[0-9a-f]{40}',a['source_commit']) or head!=a['source_commit']:raise ValueError('source pin mismatch '+a['id'])
  for item in a['inputs']:
   p=root/item['path']
   if p.is_symlink() or not p.is_file() or digest(p.read_bytes())!=item['sha256']:raise ValueError('input mismatch '+str(p))
  for b in a['binaries']:
   p=pathlib.Path(b['path'])
   if p.is_symlink() or not p.is_file() or digest(p.read_bytes())!=b['sha256']:raise ValueError('binary pin mismatch '+str(p))
  if not a['binaries'] or not a['toolchain'] or not a['dependency_pins']:raise ValueError('missing provenance')
  if a['argv'][0]!=a['binaries'][0]['path']:raise ValueError('entry executable must be pinned')

def control(fixture):
 p,q,k=[bytes.fromhex(fixture[x]) for x in ['p1_hex','p2_hex','keystream_hex']]
 if not len(p)==len(q)==len(k) or not p:raise ValueError('equal nonempty synthetic inputs required')
 px=bytes(a^b for a,b in zip(p,q)); c1=bytes(a^b for a,b in zip(p,k));c2=bytes(a^b for a,b in zip(q,k))
 observed=bytes(a^b for a,b in zip(c1,c2))
 if px.hex()!=fixture['expected_payload_xor_hex'] or observed!=px:raise ValueError('independent expected XOR mismatch')
 broken=bytes(a^b for a,b in zip(c1,q))
 if broken==px:raise ValueError('broken detector not discriminating')
 return {'status':'PASS','expected':px.hex(),'observed':observed.hex(),'broken_detector_observed':broken.hex(),'mutant':'omit-second-keystream','mutant_assertion':'ciphertext payload XOR must equal independent plaintext XOR'}

def validate(contract,receipt,base):
 if receipt['schema']!=SCHEMA:raise ValueError('receipt version')
 if receipt['contract_sha256']!=digest(json.dumps(contract,sort_keys=True,separators=(',',':')).encode()):raise ValueError('canonical fixture mismatch')
 if receipt['historical_control']!=control(contract['historical_control']):raise ValueError('oracle/control tampering')
 if len(receipt['results'])!=len(contract['adapters']):raise ValueError('missing result')
 for a,r in zip(contract['adapters'],receipt['results']):
  if r['id']!=a['id'] or r['argv']!=a['argv'] or r['source_commit']!=a['source_commit'] or r['pins']!=a:raise ValueError('result pin/command mismatch')
  if r['status'] not in ['PASS','FAIL','INVALID','NOT_RUN']:raise ValueError('status')
  if r['expected']!=a['required_output'] or r['observed']!=r['status']:raise ValueError('expected/observed mismatch')
  if pathlib.PurePath(r['log']).name!=r['log']:raise ValueError('unsafe log path')
  p=base/r['log']
  if p.is_symlink() or not p.is_file() or digest(p.read_bytes())!=r['log_sha256']:raise ValueError('missing/tampered log')
  text=p.read_text(errors='replace')
  if r['status']=='PASS':
   if r['exit']!=0 or not all(s in text for s in a['required_output']):raise ValueError('unsupported PASS')
   if any(s in text for s in ['running 0 tests','0 passed; 0 failed','FAILED','ignored;']):
    # Direct selected test executables report "0 ignored;", which is permitted.
    if 'FAILED' in text or 'running 0 tests' in text or '0 passed; 0 failed' in text or re.search(r'[1-9][0-9]* ignored;',text):raise ValueError('empty/failed/skipped run cannot PASS')
 if receipt['overall']!=('PASS' if all(r['status']=='PASS' for r in receipt['results']) else 'FAIL'):raise ValueError('aggregate mismatch')

def run(contract,out):
 out.mkdir(parents=True,exist_ok=False);check_pins(contract);rows=[]
 for index,a in enumerate(contract['adapters']):
  name=f'{index:02d}-{a["id"]}.log';log=out/name;rc=None;status='INVALID'
  with log.open('wb') as f:
   try:
    p=subprocess.Popen(a['argv'],cwd=a['root'],stdout=f,stderr=subprocess.STDOUT,env={**os.environ,**a.get('environment',{})},start_new_session=True)
    try:rc=p.wait(timeout=a['timeout_seconds'])
    except subprocess.TimeoutExpired:
     os.killpg(p.pid,signal.SIGKILL);p.wait();f.write(b'\nINVALID: qualification timeout\n')
   except OSError as e:f.write(('INVALID: '+str(e)+'\n').encode())
  text=log.read_text(errors='replace')
  if rc==0 and all(s in text for s in a['required_output']) and not re.search(r'[1-9][0-9]* ignored;',text) and 'running 0 tests' not in text:status='PASS'
  elif rc in [1,101] and ('FAILED' in text or 'STORE_SEALED: FAIL' in text):status='FAIL'
  rows.append({'id':a['id'],'status':status,'source_commit':a['source_commit'],'pins':a,'argv':a['argv'],'exit':rc,'expected':a['required_output'],'observed':status,'mutant':a.get('mutant','control'),'log':name,'log_sha256':digest(log.read_bytes()),'tracked_dirty':subprocess.run(['git','-C',a['root'],'diff','--quiet','HEAD'],check=False).returncode!=0})
 check_pins(contract)
 r={'schema':SCHEMA,'contract_sha256':digest(json.dumps(contract,sort_keys=True,separators=(',',':')).encode()),'historical_control':control(contract['historical_control']),'results':rows,'overall':'PASS' if all(x['status']=='PASS' for x in rows) else 'FAIL','checker':'tools/crypto_reuse/receipt.py-v1','hardware':'NOT_RUN','security_proof_scope':'synthetic/local tests only; no cipher security or whole-image freshness proof'}
 validate(contract,r,out);(out/'receipt.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r));return r['overall']=='PASS'
def main():
 p=argparse.ArgumentParser();p.add_argument('action',choices=['run','verify']);p.add_argument('contract');p.add_argument('--sha256',required=True);p.add_argument('--out',required=True);a=p.parse_args();c=checked_contract(a.contract,a.sha256);out=pathlib.Path(a.out).resolve()
 if a.action=='run':return 0 if run(c,out) else 1
 check_pins(c);validate(c,load(out/'receipt.json'),out);print('receipt=VALID_SYNTHETIC_HOST_ONLY');return 0
if __name__=='__main__':
 try:sys.exit(main())
 except (ValueError,KeyError,TypeError,OSError,subprocess.SubprocessError) as e:print('receipt=REFUSED '+str(e),file=sys.stderr);sys.exit(2)
