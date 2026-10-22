#!/usr/bin/env python3
import hashlib,json,os,pathlib,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[3]; folder=root/'research/exploration-600/continuation'
seed=pathlib.Path(os.environ['SEED_OMC']); out=pathlib.Path(sys.argv[1]); out.mkdir(parents=True,exist_ok=False)
expected='01b1545b23e8ff40cb5691e20b5df910ea4acaefde3495f0d62d270465b98cd6'
assert hashlib.sha256(seed.read_bytes()).hexdigest()==expected
source=(folder/'guard.om').read_text()
variants={
 'risk_cap':('if gt get q 5 sub get q 7 get ans 1','if false'),
 'horizon':('if ge div get q 5 get q 3 get q 2','if false'),
 'free_decision':('put ans 1 add get q 4 get q 9','put ans 1 get q 4'),
 'empty_frontier':('if eq get q 8 0','if false'),
 'break_even_overflow':('put ans 3 ulong 1','put ans 3 ulong 0'),
}
results=[]
for name,(before,after) in variants.items():
    assert source.count(before)==1
    src=out/(name+'.om'); binary=out/name; src.write_text(source.replace(before,after))
    p=subprocess.run([str(seed)],input=src.read_bytes(),capture_output=True,timeout=10)
    (out/(name+'-compile.log')).write_bytes(p.stderr); (out/(name+'-compile.exit')).write_text(str(p.returncode)+'\n')
    # A compiler failure cannot count as detection.
    assert p.returncode==0 and len(p.stdout)>64 and p.stdout[:5]==b'\x7fELF\x02'
    binary.write_bytes(p.stdout);binary.chmod(0o755)
    t=subprocess.run([sys.executable,str(folder/'test_guard.py'),str(binary)],capture_output=True,timeout=40)
    (out/(name+'-test.log')).write_bytes(t.stdout+t.stderr);(out/(name+'-test.exit')).write_text(str(t.returncode)+'\n')
    report=json.loads(t.stdout);assert t.returncode==1 and report['failed']>0
    results.append({'mutant':name,'compile_exit':p.returncode,'test_exit':t.returncode,'behavioral_failures':report['failed'],'binary_sha256':hashlib.sha256(p.stdout).hexdigest()})
(out/'results.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps({'compiled_and_caught':len(results),'results':results}))
