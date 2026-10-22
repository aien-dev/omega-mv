#!/usr/bin/env python3
"""Test infrastructure only. No Python, Rust, or C runs inside the native utility.
The pinned Rust source and independent C verifier supply refusal-order oracles;
Python hashlib/struct builds additional transcripts with independent digests.
"""
import argparse
import hashlib
import json
import pathlib
import random
import struct
import subprocess
import sys
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[3]
B = ROOT / 'build/retirement'
E = ROOT / 'evidence/seed-retirement'
sha = lambda b: hashlib.sha256(b).digest()
u32 = lambda n: struct.pack('<I', n)
u64 = lambda n: struct.pack('<Q', n)


def frame(op, *payloads):
    return op + b''.join(u64(len(p)) + p for p in payloads)


def run(exe, data, *args):
    p = subprocess.run([str(exe), *map(str, args)], input=data, capture_output=True, timeout=90)
    return p.returncode, p.stdout, p.stderr


def lines(path):
    return [s for s in path.read_text().splitlines() if s and not s.startswith('#')]


def transcript(records, run_id=bytes(32), producer=7, end=True):
    """Independent encoder; records are (type, subsystem, identity, annotation)."""
    head = b'TRN1' + struct.pack('<HH', 1, 0) + run_id + struct.pack('<H', producer) + bytes(6)
    out = head
    prev = sha(head)
    compared = []
    records = list(records)
    if end:
        records.append((65535, 0, u64(len(records)+1)+bytes(8), b''))
    for k, (t, sub, ident, annot) in enumerate(records, 1):
        r = struct.pack('<HHIIIQ', t, sub, len(ident), len(annot), 0, k) + prev + ident + annot
        out += r
        prev = sha(r)
        compared.append((sha(b'AIEN_TRN1_CMP'+struct.pack('<HHQI', t, sub, k, len(ident))+ident), sub))
    verdict = f'ok records={len(records)} run={run_id.hex()} final={prev.hex()}\n'.encode()
    detail = verdict + b''.join(f'digest {h.hex()} subsystem={s}\n'.encode() for h, s in compared)
    return out, verdict, detail


def generated():
    rows = []
    valid = []
    def add(name, recs, **kw):
        b, v, d = transcript(recs, **kw)
        valid.append((name, b))
        rows.append((name, frame(b'D', b), (0, d, b''), [b]))
        return b
    empty = add('only-end', [])
    sizes = {1:32, 2:24, 3:24, 4:40, 5:24, 6:48, 7:32, 8:56, 9:56, 10:48, 11:48, 12:16, 13:48, 14:40}
    for t, size in sizes.items():
        ident = bytearray(size)
        if t == 7: ident[4] = 1
        add(f'type-{t}', [(t, 10, ident, bytes(range(256)))])
    for n in [0, 1, 7, 55, 56, 63, 64, 65, 119, 120, 127, 128, 4095, 4096]:
        content = bytes(i % 256 for i in range(n))
        add(f'input-sha-{n}', [(11, 10, bytes(16)+sha(content)+content, b'')])
    add('annotation-limit', [(1, 1, bytes(32), b'Z'*65536)])
    # All parent categories, at the actual parent capacity; recursive causal state is data.
    for ident_id in [0, 1, 2, (1<<64)-1]:
        parents = [0, 1, ident_id, (1<<64)-1] * 16 + [1]
        c = u64(ident_id) + bytes(28)
        for count, each in [(8,24), (8,16), (8,24)]: c += u32(count)+bytes(count*each)
        c += bytes(4)+u32(len(parents))
        for p in parents:
            c += u64(p)
            if 1 <= p < ident_id: c += sha(u64(p))
        add(f'crumb-{ident_id}', [(15, 1, sha(b'AIEN_RX_CAUSAL_V1'+c)+c, b'')])
    prev = sha(b'arbitrary initial argus state')
    recs = []
    for flag in [0, 1, 1, 0, 1]:
        event = bytes(range(128))
        after = sha(prev+event) if flag else prev
        recs.append((16, 5, event+bytes([flag])+bytes(7)+prev+after, b''))
        prev = after
    add('argus-state', recs)
    add('record-limit', [(1, 1, bytes(32), b'')]*4095)
    too_many = transcript([(1, 1, bytes(32), b'')]*4096)[0]
    rows.append(('record-over-limit', frame(b'V', too_many), (0,b'refuse -3 event=4097\n',b''),[too_many]))
    # EOF after exactly 4096 non-END records is TRUNCATED, before the capacity check.
    truncated = transcript([(1,1,bytes(32),b'')]*4096, end=False)[0]
    rows.append(('cap-truncated-precedence', frame(b'V', truncated), (0,b'refuse -10 event=4097\n',b''),[truncated]))
    base = valid[1][1]
    for n in range(len(base)+1):
        rows.append((f'truncate-{n}', frame(b'V', base[:n]), None, [base[:n]]))
    for offset in range(len(base)):
        b = bytearray(base); b[offset] ^= 255
        rows.append((f'flip-{offset}', frame(b'V', b), None, [bytes(b)]))
    for off, width in [(52,4),(56,4),(64,8)]:
        for value in [65535,65536,65537,(1<<(width*8))-1]:
            b = bytearray(base); b[off:off+width] = value.to_bytes(width,'little')
            rows.append((f'integer-{off}-{value}',frame(b'V',b),None,[bytes(b)]))
    # Extra data is consumed even after refusal; the next command must start clean.
    for extra in [b'x',bytes(8193),bytes(131129),bytes(1048577)]:
        b = empty+extra
        rows.append((f'trailing-{len(extra)}',frame(b'V',b),(0,b'refuse -3 event=2\n',b''),[b]))
    rng = random.Random(496)
    for i in range(64):
        recs = []
        for _ in range(rng.randrange(1,12)):
            t = rng.choice([1,2,4,5,8,9])
            ident = rng.randbytes(sizes[t]); annot = rng.randbytes(rng.randrange(150))
            recs.append((t,rng.randrange(1,11),ident,annot))
        add(f'mixed-{i}',recs,run_id=rng.randbytes(32))
    for i in range(0,len(valid)-1,3):
        name,a=valid[i]; _,b=valid[i+1]
        rows.append((f'compare-{name}',frame(b'C',a,b),None,[a,b]))
    names=['transcript','omega-world','omega-cortex','omega-jspace','aienos-kernel','aienos-argus','aienos-store','sovcore-runtime','sovcore-scheduler','sovcore-kv','external']
    for i in [*range(13),255,256,65535]:
        want=(names[i] if i<len(names) else 'unknown')+'\n'
        rows.append((f'subsystem-{i}',b'S'+struct.pack('<H',i),(0,want.encode(),b''),[]))
    for i,data in enumerate([b'?',b'V',b'V'+bytes(7),b'V'+u64(10)+b'abc',b'C'+u64(0),b'S',b'S\x01',b'V'+u64((1<<64)-1)]):
        # Avoid feeding an enormous allocation request to the Rust adapter. Native must reject EOF safely.
        rows.append((f'bad-frame-{i}',data,(2,b'',b'TRN1 transport error\n'),None if i==7 else []))
    return rows


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--native',default=str(B/'trn1'))
    ap.add_argument('--quick',action='store_true',help='original corpus only, for compiled mutant checks')
    args=ap.parse_args()
    native=pathlib.Path(args.native)
    if not native.is_file() or native.stat().st_size==0:
        print('RED: native TRN1 executable absent or empty',file=sys.stderr); return 1
    oracle=B/'rust-target/release/oracle'
    vectors=B/'protocol/specs/execution-transcript/vectors'
    rows=[]
    for l in lines(vectors/'expected.txt'):
        name,want=l.split(' ',1); b=(vectors/name).read_bytes()
        rows.append((name,frame(b'V',b),(0,(want+'\n').encode(),b''),[b]))
    for l in lines(vectors/'compare.txt'):
        x,y,want=l.split(' ',2); a=(vectors/x).read_bytes();b=(vectors/y).read_bytes()
        rows.append((f'{x}/{y}',frame(b'C',a,b),(0,(want+'\n').encode(),b''),[a,b]))
    original=len(rows)
    if not args.quick:
        cur=B/'protocol-current/specs/execution-transcript/vectors'
        for f in sorted(cur.glob('*.trn')):
            b=f.read_bytes();rows.append(('current/'+f.name,frame(b'D',b),None,[b]))
        rows+=generated()
    log=[]; batch=[]; batch_want=[]; independent=0
    B.joinpath('casework').mkdir(exist_ok=True)
    for name,data,want,payloads in rows:
        if payloads is not None:
            rust=run(oracle,data)
            if want is not None and rust!=want: raise AssertionError(f'{name}: Rust differs from independently expected result: {rust!r} != {want!r}')
            want=rust
        got=run(native,data)
        if got!=want: raise AssertionError(f'{name}: native {got!r} != expected {want!r}')
        # C's file CLI has different process exit rules; compare semantic output, preserving those raw codes in evidence.
        c_rc=None
        if payloads:
            paths=[]
            for i,b in enumerate(payloads):
                path=B/f'casework/{i}.trn';path.write_bytes(b);paths.append(path)
            argv=paths if len(paths)==1 else ['--compare',*paths]
            c=run(B/'trn1-c-oracle',b'',*argv)
            c_want=got[1].split(b'\n',1)[0]+b'\n'
            assert c[1]==c_want and c[2]==b'',f'{name}: C {c!r} != {c_want!r}'
            c_rc=c[0];independent+=1
        log.append({'case':name,'status':'PASS','stdin_sha256':sha(data).hex(),'stdout_sha256':sha(got[1]).hex(),'stdout_bytes':len(got[1]),'stderr_sha256':sha(got[2]).hex(),'exit':got[0],'c_exit':c_rc})
        if got[0]==0:
            batch.append(data);batch_want.append(got[1])
    if not args.quick:
        data=b''.join(batch);want=b''.join(batch_want)
        for label,exe in [('rust',oracle),('native',native)]:
            for repeat in range(2):
                assert run(exe,data)==(0,want,b''),f'{label} repeated use {repeat}'
        # Pipe fragments, including frame/header/record boundaries, and live response before EOF.
        small=b''.join(batch[:12]); expected=b''.join(batch_want[:12])
        p=subprocess.Popen([str(native)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        def feed():
            for i in range(0,len(small),7):
                p.stdin.write(small[i:i+7]);p.stdin.flush()
            p.stdin.close()
        t=threading.Thread(target=feed);t.start()
        out=p.stdout.read();err=p.stderr.read();rc=p.wait(timeout=60);t.join()
        assert (rc,out,err)==(0,expected,b''),'fragmented pipe'
        # Independent source interpreter, not another compiler using this code generator.
        for name,data,_,_ in rows[:3]:
            p=subprocess.run([str(B/'omega_ref'),'run',str(ROOT/'seed/retirement/trn1/trn1.om')],input=data,capture_output=True,timeout=90)
            assert (p.returncode,p.stdout,p.stderr)==run(native,data),f'interpreter {name}'
        (E/'trn1-cases.json').write_text(json.dumps(log,indent=2)+'\n')
        summary={'status':'PASS','original_corpus':original,'cases_written':len(rows),'cases_run':len(log),'independent_c_cases':independent,'batch_requests':len(batch),'batch_repetitions':2,'fragmented_pipe':'PASS','independent_interpreter_cases':3,'native_sha256':sha(native.read_bytes()).hex(),'source_sha256':sha((ROOT/'seed/retirement/trn1/trn1.om').read_bytes()).hex()}
        (E/'trn1-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(f'TRN1 PASS original={original} written={len(rows)} run={len(log)} independent_c={independent}')
    return 0
if __name__=='__main__':
    try: sys.exit(main())
    except (AssertionError,subprocess.TimeoutExpired,OSError) as e:
        print(f'FAIL {e}',file=sys.stderr);sys.exit(1)
