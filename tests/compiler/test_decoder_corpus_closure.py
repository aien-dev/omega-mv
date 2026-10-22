#!/usr/bin/env python3
"""Test PR499 export completeness; never executes or changes decoder input."""
import io,json,pathlib,subprocess,sys,tarfile
repo=pathlib.Path(sys.argv[1]).resolve(); verifier=pathlib.Path(sys.argv[2]).resolve(); out=pathlib.Path(sys.argv[3]).resolve(); out.mkdir(parents=True,exist_ok=False)
ref='1e4cdbe91b444efe51af4e745f366e07d54de030'; dirs=['tests/compiler/progs','tests/compiler/slices','src/osh','tests/physics0/osc']; loader='tests/compiler/test_osc_ir_decode.c'
raw=subprocess.check_output(['git','-C',str(repo),'archive',ref,*dirs,loader])
fixture=out/'export'; fixture.mkdir()
with tarfile.open(fileobj=io.BytesIO(raw)) as a: a.extractall(fixture,filter='data')
cmd=['sh' if verifier.suffix=='.sh' else sys.executable,str(verifier),str(fixture),str(repo)]
checks=[]
def check(name,want):
    p=subprocess.run(cmd,capture_output=True,text=True,timeout=5)
    checks.append({'case':name,'expected':want,'actual':p.returncode,'pass':p.returncode==want,'stdout':p.stdout.strip(),'stderr':p.stderr.strip()})
check('complete',0)
for d in dirs:
    f=next((fixture/d).glob('*.osc')); saved=f.read_bytes();f.unlink();check('missing:'+str(f.relative_to(fixture)),1);f.write_bytes(saved)
for f in sorted(x for d in dirs for x in (fixture/d).glob('*.osc')):
    saved=f.read_bytes();f.write_bytes(saved+b'\n// changed input\n');check('changed:'+str(f.relative_to(fixture)),1);f.write_bytes(saved)
f=fixture/dirs[0]/'unmanifested.osc'; f.write_text('unmanifested');check('extra_program',1);f.unlink()
f=next((fixture/dirs[0]).glob('*.osc'));saved=f.read_bytes();target=out/'outside.osc';target.write_bytes(saved);f.unlink();f.symlink_to(target);check('symlink_program',1);f.unlink();f.write_bytes(saved)
f=fixture/loader;saved=f.read_bytes();f.write_bytes(saved+b'\n/* changed loader */\n');check('changed_loader',1);f.write_bytes(saved)
fail=[r for r in checks if not r['pass']]
(out/'checks.json').write_text(json.dumps(checks,indent=2)+'\n')
print(json.dumps({'checks':len(checks),'failed':len(fail),'passed':len(checks)-len(fail),'failures':fail[:4]}));sys.exit(bool(fail))
