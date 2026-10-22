import hashlib,json,pathlib,subprocess
repo=pathlib.Path(__file__).resolve().parents[2];s=pathlib.Path('/home/drakestapleton/handoffs/omega-issue-sweep-20261022');pin='1e4cdbe91b444efe51af4e745f366e07d54de030';root=s/'L541-decoder-full'
paths=['Makefile','mk','src/compiler','src/sha256.c','src/sha256.h','tests/compiler','tools/quietlock','src/osh','tests/physics0/osc']
raw=subprocess.check_output(['git','-C',str(repo),'ls-tree','-rz',pin,'--',*paths]);files=[]
for r in raw.split(b'\0'):
 if not r:continue
 meta,name=r.split(b'\t',1);mode,kind,oid=meta.decode().split();name=name.decode();p=root/name;b=p.read_bytes();actual=hashlib.sha1(b'blob '+str(len(b)).encode()+b'\0'+b).hexdigest();files.append({'path':name,'bytes':len(b),'git_blob':oid,'sha256':hashlib.sha256(b).hexdigest(),'unchanged':oid==actual,'symlink':p.is_symlink()})
binaries=[]
for p in sorted((root/'build/closure/compiler').glob('test_osc_ir_decode*')):
 if p.is_file():binaries.append({'path':str(p.relative_to(root)),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
packet={'pin':pin,'export_paths':paths,'source_files':len(files),'all_sources_match_pin_after_run':all(r['unchanged'] and not r['symlink'] for r in files),'files':files,'binaries':binaries}
(repo/'evidence/L541/decoder-export-pins.json').write_text(json.dumps(packet,indent=2)+'\n');assert packet['all_sources_match_pin_after_run'];print(json.dumps({k:packet[k] for k in ['source_files','all_sources_match_pin_after_run']}))
