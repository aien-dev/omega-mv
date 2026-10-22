#!/usr/bin/env python3
"""Compiled wrong-result controls; compilation failure is not a killed mutant."""
import hashlib,json,pathlib,subprocess,sys
ROOT=pathlib.Path(__file__).resolve().parents[3]
B=ROOT/'build/retirement';E=ROOT/'evidence/seed-retirement'
src=(ROOT/'seed/retirement/trn1/trn1.om').read_text()
changes=[('sha-word','put K 0 1116352408','put K 0 1116352409'),('accept-count','number n\nsayRun','number add n 1\nsayRun'),('version','if ne loadle 4 2 1','if ne loadle 4 2 2'),('argus-continuity','put Rv 7 1','put Rv 7 0')]
log=[]
for name,old,new in changes:
    assert src.count(old)==1,(name,src.count(old))
    p=B/(name+'.om');p.write_text(src.replace(old,new));exe=B/(name+'.mutant')
    with p.open('rb') as i,exe.open('wb') as o:
        c=subprocess.run([str(B/'seed-src/seed/out/omc')],stdin=i,stdout=o,stderr=subprocess.PIPE)
    assert c.returncode==0 and exe.stat().st_size>0,(name,'BROKEN',c.stderr)
    header=subprocess.run(['readelf','-h',str(exe)],capture_output=True)
    assert header.returncode==0 and b'AArch64' in header.stdout,(name,'INVALID ELF',header.stderr)
    exe.chmod(0o755)
    r=subprocess.run([sys.executable,str(ROOT/'tools/retirement/trn1/check.py'),'--native',str(exe),'--quick'],capture_output=True)
    (E/(name+'-mutant.log')).write_bytes(r.stdout+r.stderr)
    assert r.returncode==1 and b'FAIL' in r.stderr,(name,'SURVIVED',r.stdout,r.stderr)
    log.append({'mutant':name,'compile_rc':c.returncode,'bytes':exe.stat().st_size,'valid_aarch64_elf':True,'comparison_rc':r.returncode,'executable_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'source_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'original_source_sha256':hashlib.sha256(src.encode()).hexdigest(),'seed_sha256':hashlib.sha256((B/'seed-src/seed/out/omc').read_bytes()).hexdigest(),'result':'KILLED'})
(E/'mutants.json').write_text(json.dumps(log,indent=2)+'\n')
print(f'MUTANTS PASS compiled={len(log)} killed={len(log)}')
