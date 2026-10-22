from pathlib import Path
import subprocess,json,re,hashlib
w=Path.home()/'workspace/omega-osc-manual-migration';p=w/'evidence/OSC-MANUAL-MIGRATION';physics=w/'build/manual-migration/physics-6d7cf0d'
def git(path,*args):return subprocess.check_output(['git','-C',str(path),*args],text=True).strip()
pin=(w/'physics.lock').read_text().strip();actual=git(physics,'rev-parse','HEAD');assert actual==pin
assert not git(physics,'status','--porcelain')
shared=git(w.parent/'physics','rev-parse','HEAD');assert shared=='e95e3ed2a86fe4bffe4d954fa94c27dfb5284280'
s=(p/'pinned-repository-test.txt').read_text();assert s.endswith('RC=0\n')
src=(w/'tools/omegatool.c').read_text();part=src[src.index('int omega_run_m4_gates(void)'):src.index('int omega_run_m5_gates(void)')];expected=part.count('report_gate(')
m=re.search(r'TOTAL GATES: (\d+) \| PASSED: (\d+) \| FAILED: (\d+)',s);assert m
counts=list(map(int,m.groups()));assert counts==[expected,expected,0]
assert len(re.findall(r'^  \[PASS\] OMEGA_',s,re.M))==expected
for fragment in ['Convergence Result: IDENTICAL CANONICAL SEMANTIC ID (PASS)','Divergence Result: DIFFERENT SEMANTIC ID CONFIRMED (PASS)','Result: IDENTICAL CANONICAL SEMANTIC ID (PASS)']:assert fragment in s
binary=w/'build/manual-migration/repository-pinned/omegatool'
r={'source_commit':git(w,'rev-parse','HEAD'),'physics_expected':pin,'physics_actual':actual,'physics_worktree_clean':True,'shared_physics_head_unchanged':shared,'lock_check_disabled':False,'expected_m4_gates_from_source':expected,'actual_gates':counts[0],'passed':counts[1],'failed':counts[2],'reference_demonstrations':['arithmetic','physics'],'exit':0,'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'log_sha256':hashlib.sha256((p/'pinned-repository-test.txt').read_bytes()).hexdigest(),'scope':'default make test target; legacy reference build, not all repository targets or native retirement'}
(p/'pinned-repository-result.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
