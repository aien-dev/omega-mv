from pathlib import Path
import json,subprocess,hashlib,time,shutil,os
home=Path.home();author=home/'workspace/hermes-desk/universal-c-u64-import';sha='50f75da28845f21b118bcf8e3499b96c35ddb587';replay=author/'build/L589/replay-50f75da2';e=home/'handoffs/universal-L569/L589-cold-replay';e.mkdir(parents=True,exist_ok=False)
subprocess.run(['git','clone','--shared','--no-checkout',str(author),str(replay)],check=True);subprocess.run(['git','-C',str(replay),'checkout','--detach',sha],check=True)
assert not (replay/'build').exists()
commands=[['make','-f','mk/c-u64-import.mk','test-c-u64-import'],['make','PHYSICS_DIR='+str(home/'workspace/hermes-desk/universal-L569-physics'),'test-source-aware'],['make','-j2','OUT_DIR=build/L589/foundation','PHYSICS_DIR='+str(home/'workspace/hermes-desk/universal-L569-physics'),'test']]
rows=[]
for i,cmd in enumerate(commands):
 with (e/f'gate-{i}.log').open('wb') as log:
  try:r=subprocess.run(cmd,cwd=replay,stdout=log,stderr=subprocess.STDOUT,timeout=360);status=r.returncode
  except subprocess.TimeoutExpired:status='TIMEOUT'
 rows.append({'command':cmd,'status':status,'log_sha256':hashlib.sha256((e/f'gate-{i}.log').read_bytes()).hexdigest()});print(json.dumps(rows[-1]),flush=True)
 if status!=0:break
records={}
for kind,pattern in [('C','gate-*/result.json'),('boundaries','controls-*/boundaries.json'),('mutants','controls-*/mutants.json')]:
 files=list((replay/'build/L589').glob(pattern));assert len(files)==1,(kind,files);p=files[0];shutil.copyfile(p,e/(kind+'.json'));records[kind]=str(p.relative_to(replay))
p=replay/'build/L574/PASS.json'
if p.exists():shutil.copyfile(p,e/'source-aware-cold.json')
shutil.copyfile(replay/'build/L589/build.json',e/'build.json')
current=subprocess.check_output(['git','-C',str(replay),'rev-parse','HEAD'],text=True).strip();assert current==sha
closure={str(p.relative_to(replay)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [replay/x for x in ['src/adapters/c-u64-import.om','src/source-aware/core.om','src/source-aware/main.om','src/source-aware/vendor/sha256.om','tools/adapters/build-c-import.py','tools/adapters/c-import','tools/source-aware/build','tools/source-aware/sa','tests/universal/c_import_gate.py','tests/universal/c_import_controls.py']]}
result={'schema':'omega.universal.cold-replay.v1','source_commit':sha,'upstream_621_source':'cd2c41f3c697524629585b2a228e2f0ab5b99c45','result':'PASS' if len(rows)==len(commands) and all(x['status']==0 for x in rows) else 'FAIL','commands':rows,'input_hashes':closure,'records':records,'physical_host':subprocess.check_output(['uname','-a'],text=True).strip(),'cpu':json.loads(subprocess.check_output(['lscpu','--json'],text=True)),'operating_system':Path('/etc/os-release').read_text(),'compiler':subprocess.check_output(['cc','--version'],text=True),'node':subprocess.check_output(['node','--version'],text=True).strip(),'scope':'HOST_NATIVE correctness only; no timing, GPU, x86 or RV64 qualification; independent review pending','signed':False}
(e/'replay.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:result[k] for k in ['source_commit','result','scope']},indent=2),flush=True)
if result['result']!='PASS':raise SystemExit(1)
c=json.loads((e/'C.json').read_text());s=json.loads((e/'source-aware-cold.json').read_text());assert c['source_commit']==sha and s['sourceHEAD']==sha;assert c['source_aware_binary_sha256']==s['toolchain']['nativeBinarySHA256'];assert c['source_aware_requests']==3*c['positive_executed'];print('CROSS_COMPONENT_HASH_AND_EXECUTED_COUNT_MATCH',c['source_aware_requests'],flush=True)
