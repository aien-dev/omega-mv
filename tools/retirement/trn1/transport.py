#!/usr/bin/env python3
"""Native transport/fault tests. strace controls only these child processes."""
import hashlib
import json
import os
from pathlib import Path
import select
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[3]
B=ROOT/'build/retirement'
E=ROOT/'evidence/seed-retirement'
EXE=B/'trn1'
ERR=b'TRN1 transport error\n'

def main():
    results=[]
    def check(name,data,want,extra=()):
        argv=[str(EXE)]
        if extra: argv=['strace','-qq','-o',str(B/'transport.trace'),*extra,*argv]
        p=subprocess.run(argv,input=data,capture_output=True,timeout=60)
        assert (p.returncode,p.stdout,p.stderr)==want,(name,p.returncode,p.stdout[:120],p.stderr[:120])
        results.append({'case':name,'exit':p.returncode,'stdout_sha256':hashlib.sha256(p.stdout).hexdigest(),'stderr_sha256':hashlib.sha256(p.stderr).hexdigest()})
    check('empty-eof',b'',(0,b'',b''))
    check('read-eintr',b'S\0\0',(0,b'transcript\n',b''),('-e','inject=read:error=EINTR:when=1'))
    check('read-eintr-512',b'S\0\0',(0,b'transcript\n',b''),('-e','inject=read:error=EINTR:when=1..512'))
    check('write-eintr',b'S\0\0',(0,b'transcript\n',b''),('-e','inject=write:error=EINTR:when=1'))
    check('read-ebadf',b'',(2,b'',ERR),('-e','inject=read:error=EBADF:when=1'))
    check('write-ebadf',b'S\0\0',(2,b'',ERR),('-e','inject=write:error=EBADF:when=1'))
    check('request-limit',b'S\xff\xff'*1000000,(0,b'unknown\n'*1000000,b''))
    check('request-over-limit',b'S\xff\xff'*1000001,(2,b'unknown\n'*1000000,ERR))
    # Require a reply while stdin is still open; do not mistake EOF flushing for interactivity.
    p=subprocess.Popen([str(EXE)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    try:
        p.stdin.write(b'S\0\0');p.stdin.flush()
        assert select.select([p.stdout],[],[],10)[0],'response requires EOF'
        assert os.read(p.stdout.fileno(),11)==b'transcript\n'
        p.stdin.close()
        assert p.wait(timeout=10)==0 and p.stdout.read()==b'' and p.stderr.read()==b''
    finally:
        if not p.stdin.closed:p.stdin.close()
        p.wait(timeout=10)
    results.append({'case':'reply-before-eof','exit':0})
    # Signal dispositions are inherited: a closed pipe under the Linux default
    # SIGPIPE disposition terminates the process before write can return EPIPE.
    r,w=os.pipe();os.close(r)
    try:
        p=subprocess.run([str(EXE)],input=b'S\0\0',stdout=w,stderr=subprocess.PIPE,timeout=10)
    finally:os.close(w)
    assert (p.returncode,p.stderr)==(-13,b''),'default SIGPIPE behavior'
    results.append({'case':'closed-pipe-default-sigpipe','signal':13,'exit':p.returncode})
    (E/'transport.json').write_text(json.dumps({'status':'PASS','native_sha256':hashlib.sha256(EXE.read_bytes()).hexdigest(),'cases':results},indent=2)+'\n')
    print(f'TRANSPORT PASS cases={len(results)}')

if __name__=='__main__':main()
