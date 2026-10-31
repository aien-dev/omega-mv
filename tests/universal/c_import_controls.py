#!/usr/bin/env python3
"""Boundary/public-entry and successfully compiled behavioral mutation controls."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,os,shutil,struct,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('cgate',ROOT/'tests/universal/c_import_gate.py');g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
ap=argparse.ArgumentParser();ap.add_argument('--out',required=True);a=ap.parse_args();out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=False)
exe=ROOT/'build/L589/c-import';compiler=ROOT/'build/L574/toolchain/build/bootstrap/omc';assert hashlib.sha256(compiler.read_bytes()).hexdigest()=='191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9'
header=g.HEADER
base=(header+'uint64_t calc(uint64_t a,uint64_t b){return a+b;}').encode()
pad=base+b' '*(4096-len(base));comment=base+b'//'+b'x'*(4096-len(base)-2)
maximum=(header+'uint64_t calc(uint64_t a,uint64_t b){return '+'+'.join(['a']*61)+';}').encode()
deep=(header+'uint64_t calc(uint64_t a,uint64_t b){return '+'('*16+'a'+')'*16+';}').encode()
positives=[('exact-source-capacity',pad,2,12),('comment-at-capacity',comment,2,12),('operation-capacity',maximum,2,(61*5)&g.MASK),('nesting-limit',deep,2,5)]
checks=[]
for name,src,arity,want in positives:
 r=subprocess.run([str(exe)],input=src,capture_output=True,timeout=10);assert r.returncode==0,(name,r.stderr)
 ops=g.decode(r.stdout,src,arity);assert g.evaluate(ops,[5,7])==want
 checks.append({'name':name,'result':'PASS','exit':r.returncode,'source_bytes':len(src),'operation_count':len(ops),'upper_sha256':hashlib.sha256(r.stdout).hexdigest()})
negatives=[('over-source-capacity',pad+b' ',67),('over-operation-capacity',(header+'uint64_t calc(uint64_t a,uint64_t b){return '+'+'.join(['a']*62)+';}').encode(),67),('over-nesting-limit',(header+'uint64_t calc(uint64_t a,uint64_t b){return '+'('*17+'a'+')'*17+';}').encode(),67),('literal-overflow',(header+'uint64_t calc(void){return 18446744073709551616ULL;}').encode(),67),('unknown-input',(header+'uint64_t calc(void){return unbound;}').encode(),70),('long-identifier',(header+'uint64_t calc(uint64_t '+('a'*33)+'){return 1ULL;}').encode(),71)]
for name,src,want in negatives:
 r=subprocess.run([str(exe)],input=src,capture_output=True,timeout=10);assert r.returncode==want and not r.stdout and b'C_IMPORT_' in r.stderr,(name,r.returncode,r.stdout,r.stderr)
 checks.append({'name':name,'result':'PASS','exit':r.returncode,'stderr':r.stderr.decode(),'stdout_bytes':len(r.stdout)})
r=subprocess.run([str(exe)],stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE,preexec_fn=lambda:os.close(0),timeout=10)
assert r.returncode==77 and not r.stdout and r.stderr==b'C_IMPORT_IO\n',(r.returncode,r.stderr)
checks.append({'name':'closed-stdin','result':'PASS','exit':r.returncode})
cli=ROOT/'tools/adapters/c-import'
r=subprocess.run([sys.executable,str(cli)],input=base,capture_output=True,timeout=10);direct=subprocess.run([str(exe)],input=base,capture_output=True,timeout=10);assert r.returncode==0 and r.stdout==direct.stdout
checks.append({'name':'public-command-identical-to-native','result':'PASS','exit':0})
r=subprocess.run([sys.executable,str(cli),'unexpected'],input=base,capture_output=True,timeout=10);assert r.returncode==64 and not r.stdout and b'C_IMPORT_BAD_COMMAND' in r.stderr
checks.append({'name':'public-command-arguments-refused','result':'PASS','exit':64})
for fault in ['source','binary']:
 with tempfile.TemporaryDirectory(prefix='closed-build-',dir=out) as td:
  t=Path(td)
  for rel in ['tools/adapters/c-import','src/source-aware/vendor/sha256.om','src/adapters/c-u64-import.om','build/L589/build.json','build/L589/c-import']:
   p=t/rel;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/rel,p);p.chmod(0o700 if rel.endswith('c-import') else 0o600)
  victim=t/('src/adapters/c-u64-import.om' if fault=='source' else 'build/L589/c-import')
  with victim.open('ab') as f:f.write(b'changed')
  r=subprocess.run([sys.executable,str(t/'tools/adapters/c-import')],input=base,capture_output=True,timeout=10)
  assert r.returncode==78 and not r.stdout and b'C_IMPORT_STALE_OR_MISSING_BUILD' in r.stderr
  checks.append({'name':'public-'+fault+'-identity-change-refused','result':'PASS','exit':78})
(out/'boundaries.json').write_text(json.dumps(checks,indent=2)+'\n')
print('BOUNDARIES_PASS',len(checks),flush=True)
source=(ROOT/'src/adapters/c-u64-import.om').read_text();vendor=(ROOT/'src/source-aware/vendor/sha256.om').read_text();mutants=[]
changes=[('add-as-sub','code = ulong 2','code = ulong 3'),('sub-as-add','code = 3\n','code = 2\n'),('mul-as-add','left = emitop 4 left right 0 lo hi','left = emitop 2 left right 0 lo hi'),('literal-digit-offset','v = add mul v 10 d','v = add add mul v 10 d 1'),('parameter-slot-swap','ret j\n','ret rem add j 1 get Npar 0\n')]
for name,old,new in changes:
 assert source.count(old)==1,(name,source.count(old));program=vendor+'\n'+source.replace(old,new)+'\n';r=subprocess.run([str(compiler)],input=program.encode(),capture_output=True,timeout=60)
 assert r.returncode==0 and len(r.stdout)>64 and r.stdout[:4]==b'\x7fELF' and struct.unpack_from('<H',r.stdout,18)[0]==183,(name,'mutant did not compile',r.stderr)
 binary=out/(name+'.elf');binary.write_bytes(r.stdout);binary.chmod(0o700)
 attempt=out/(name+'-observations');r=subprocess.run([sys.executable,str(ROOT/'tests/universal/c_import_gate.py'),'--importer',str(binary),'--out',str(attempt)],capture_output=True,text=True,timeout=120)
 assert r.returncode==1,(name,'mutant survived',r.stdout,r.stderr)
 d=json.loads((attempt/'result.json').read_text());semantic=[c for c in d['cases'] if c.get('failure')=='native C versus imported semantic arithmetic mismatch' and c.get('mismatched_rows')]
 assert semantic,(name,'only setup/refusal failure, not a semantic mutation kill')
 mutants.append({'name':name,'compiled':True,'native_semantic_mismatch_programs':len(semantic),'mismatched_rows':sum(len(c['mismatched_rows']) for c in semantic),'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'source_sha256':hashlib.sha256(program.encode()).hexdigest()});print('MUTANT_CAUGHT',json.dumps(mutants[-1]),flush=True)
(out/'mutants.json').write_text(json.dumps(mutants,indent=2)+'\n');print('PASS boundaries='+str(len(checks))+' compiled_semantic_mutants='+str(len(mutants)))
