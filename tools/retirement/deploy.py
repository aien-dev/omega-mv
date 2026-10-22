#!/usr/bin/env python3
"""Isolated deployment TEST infrastructure. The active command is a native ELF.
The destination is deliberately fixed inside this task's ignored build directory.
This is not a live-service installer or an authorization for production cutover.
"""
import fcntl
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
B=ROOT/'build/retirement'
E=ROOT/'evidence/seed-retirement'
D=B/'isolated'
SEED_SHA='191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9'


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def syncdir(path):
    fd=os.open(path,os.O_RDONLY|os.O_DIRECTORY)
    try:os.fsync(fd)
    finally:os.close(fd)


def jsonwrite(path,value):
    with tempfile.NamedTemporaryFile(dir=path.parent,delete=False) as f:
        tmp=Path(f.name);f.write((json.dumps(value,indent=2)+'\n').encode());f.flush();os.fsync(f.fileno())
    os.replace(tmp,path);syncdir(path.parent)


def checked(target,expected):
    path=D/target
    if path.resolve().parent.parent != (D/'releases').resolve():raise ValueError('release outside isolated directory')
    if path.name!='trn1' or digest(path)!=expected:raise ValueError('release digest mismatch')
    return path


def stage(source,label,expected):
    if digest(source)!=expected:raise ValueError('candidate digest mismatch')
    target=f'releases/{label}-{expected}/trn1'
    dest=D/target;dest.parent.mkdir(parents=True,exist_ok=True)
    if not dest.exists():
        with tempfile.NamedTemporaryFile(dir=dest.parent,delete=False) as f:
            tmp=Path(f.name);f.write(source.read_bytes());f.flush();os.fchmod(f.fileno(),0o555);os.fsync(f.fileno())
        if digest(tmp)!=expected:raise ValueError('staged bytes changed')
        os.replace(tmp,dest);syncdir(dest.parent);syncdir(dest.parent.parent)
    checked(target,expected)
    return {'target':target,'sha256':expected}


def active():
    p=D/'active'
    if not p.is_symlink():return None
    target=os.readlink(p)
    return {'target':target,'sha256':digest(D/target)}


def switch(release):
    checked(release['target'],release['sha256'])
    tmp=D/'next'
    if tmp.is_symlink():tmp.unlink()
    os.symlink(release['target'],tmp)
    os.replace(tmp,D/'active');syncdir(D)


def smoke(exe):
    p=subprocess.run([str(exe)],input=b'S\0\0S\xff\xffV'+bytes(8),capture_output=True,timeout=10)
    if (p.returncode,p.stdout,p.stderr)!=(0,b'transcript\nunknown\nrefuse -3 event=0\n',b''):
        raise ValueError('post-swap smoke failure')


def recover():
    p=D/'journal.json'
    if p.exists():
        j=json.loads(p.read_text())
        if j['state']=='pending':
            switch(j['previous']);j['state']='recovered';jsonwrite(p,j)
            return True
    return False


def deploy(release,probe=smoke):
    recover()
    checked(release['target'],release['sha256'])
    previous=active()
    if previous is None:raise ValueError('initial baseline must be staged explicitly')
    checked(previous['target'],previous['sha256'])
    j={'state':'pending','previous':previous,'candidate':release}
    jsonwrite(D/'journal.json',j)
    try:
        switch(release);probe(D/'active');checked(release['target'],release['sha256'])
        j['state']='committed';jsonwrite(D/'journal.json',j)
    except BaseException:
        switch(previous);j['state']='rolled_back';jsonwrite(D/'journal.json',j)
        raise


def qualify():
    summary=json.loads((E/'trn1-summary.json').read_text())
    if summary['status']!='PASS' or summary['cases_written']!=summary['cases_run']:raise ValueError('equivalence gate incomplete')
    if summary['source_sha256']!=digest(ROOT/'seed/retirement/trn1/trn1.om'):raise ValueError('source changed since equivalence')
    if summary['native_sha256']!=digest(B/'trn1'):raise ValueError('executable changed since equivalence')
    if digest(B/'seed-src/seed/out/omc')!=SEED_SHA:raise ValueError('unqualified birth compiler')
    transport=json.loads((E/'transport.json').read_text())
    if transport['status']!='PASS' or transport['native_sha256']!=summary['native_sha256']:raise ValueError('transport gate stale')
    mutants=json.loads((E/'mutants.json').read_text())
    if len(mutants)<4 or any(m['result']!='KILLED' or m['compile_rc']!=0 for m in mutants):raise ValueError('mutant gate incomplete')
    if any(m['source_sha256']!=summary['source_sha256'] or m['seed_sha256']!=SEED_SHA for m in mutants):raise ValueError('mutant gate stale')
    for line in (E/'oracles.sha256').read_text().splitlines():
        expected,path=line.split(None,1)
        if digest(Path(path))!=expected:raise ValueError('pinned oracle changed since equivalence')
    raw=subprocess.check_output(['readelf','-h','-l','-d',str(B/'trn1')]).decode()
    if 'AArch64' not in raw or 'INTERP' in raw or 'NEEDED' in raw:raise ValueError('native dependency closure failed')
    return stage(B/'trn1','native',summary['native_sha256'])


def main():
    D.mkdir(parents=True,exist_ok=True)
    tests=[]
    with (D/'lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        recover()
        baseline=stage(B/'rust-target/release/oracle','rust-oracle',digest(B/'rust-target/release/oracle'))
        candidate=qualify()
        switch(baseline);smoke(D/'active')
        tests.append('actual pinned Rust baseline smoke')
        before=active()
        try:stage(B/'trn1','bad','0'*64)
        except ValueError:pass
        else:raise AssertionError('bad hash accepted')
        assert active()==before;tests.append('wrong candidate hash refused without swap')
        deploy(candidate);assert active()==candidate
        smoke(D/'active');tests.append('atomic Rust-oracle to native switch and smoke')
        # Roll back to the actual old executable and prove its behavior, then retry.
        deploy(baseline);assert active()==baseline;smoke(D/'active')
        tests.append('explicit rollback to original Rust oracle')
        def broken(_):raise ValueError('injected post-swap health failure')
        try:deploy(candidate,broken)
        except ValueError:pass
        else:raise AssertionError('failed health probe accepted')
        assert active()==baseline;smoke(D/'active');tests.append('post-swap failure restores old executable')
        # A pending journal survives a process interruption; next invocation recovers.
        jsonwrite(D/'journal.json',{'state':'pending','previous':baseline,'candidate':candidate})
        switch(candidate)
        assert recover() and active()==baseline
        smoke(D/'active');tests.append('interrupted transaction recovered from durable journal')
        deploy(candidate)
        p=subprocess.run(['python3',str(ROOT/'tools/retirement/trn1/check.py'),'--native',str(D/'active'),'--quick'],capture_output=True,timeout=120)
        (E/'deployed-corpus.log').write_bytes(p.stdout+p.stderr)
        assert p.returncode==0,'deployed original corpus'
        tests.append('all 77 original cases through installed native symlink')
        p=subprocess.run(['strace','-qq','-o',str(E/'native-syscalls.log'),str(D/'active')],input=b'S\0\0',capture_output=True,timeout=10)
        assert (p.returncode,p.stdout,p.stderr)==(0,b'transcript\n',b'')
        trace=(E/'native-syscalls.log').read_text()
        calls=[line.split('(',1)[0] for line in trace.splitlines() if '(' in line]
        assert calls and calls[0]=='execve' and set(calls)<= {'execve','read','write','exit','exit_group'}
        assert calls.count('execve')==1
        tests.append('sample path invokes only read/write/exit after initial exec')
        assert active()==candidate
        result={'status':'PASS','scope':'isolated only; production untouched','production_cutover':False,'native_sha256':candidate['sha256'],'source_sha256':digest(ROOT/'seed/retirement/trn1/trn1.om'),'seed_sha256':SEED_SHA,'active_path':str(D/'active'),'baseline':baseline,'candidate':candidate,'cases':tests,'closure':{'status':'PASS','interpreter':None,'needed':[],'external_floor':['Linux AArch64 read/write/exit syscall ABI'],'trace_scope':'one smoke request; ELF/source lineage separately checked'}}
        jsonwrite(E/'deployment.json',result)
    print(f'DEPLOYMENT PASS cases={len(tests)} production_cutover=false')

if __name__=='__main__':main()
