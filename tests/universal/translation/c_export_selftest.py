#!/usr/bin/env python3
"""C export lane self-test (translation-v1 section 6, #622). Author-run TESTED evidence, not QUALIFIED.

For every F1 positive and the four accepted boundary controls of c_import_controls.py:
import (native importer) -> export (native exporter) -> re-import; the re-imported OMSA semantic bytes
must equal the first import's. The semantic-only envelope must export byte-identically. Original and
exported C are compiled with cc -std=c17 -O2 -Wall -Werror and compared on boundary and seeded random
vectors (DIFFERENTIAL), and against the Python integer model of the imported graph (REFERENCE).
Constructed units cover the non-NORMALIZED, shared-SSA and permuted-slot paths and every refusal."""
from pathlib import Path
import argparse,hashlib,importlib.util,itertools,json,os,random,shutil,struct,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[3];sys.dont_write_bytecode=True
spec=importlib.util.spec_from_file_location('cgate',ROOT/'tests/universal/c_import_gate.py');g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
MASK=g.MASK
IMPORTER=ROOT/'build/L589/c-import';EXPORTER=ROOT/'build/L622/c-export';CLI=ROOT/'tools/adapters/c-export';SA=ROOT/'tools/source-aware/sa'
BOUNDARY=[0,1,2,(1<<32)-1,1<<32,(1<<63)-1,1<<63,MASK-1,MASK]
CFLAGS=['-std=c17','-O2','-Wall','-Werror']
SEMHASH=hashlib.sha256((ROOT/'spec/universal/semantics.md').read_bytes()).digest()

def controls():
    # Exactly the four accepted boundary controls of tests/universal/c_import_controls.py.
    header=g.HEADER;base=(header+'uint64_t calc(uint64_t a,uint64_t b){return a+b;}').encode()
    pad=base+b' '*(4096-len(base));comment=base+b'//'+b'x'*(4096-len(base)-2)
    maximum=(header+'uint64_t calc(uint64_t a,uint64_t b){return '+'+'.join(['a']*61)+';}').encode()
    deep=(header+'uint64_t calc(uint64_t a,uint64_t b){return '+'('*16+'a'+')'*16+';}').encode()
    return [('exact-source-capacity',pad,'calc',2),('comment-at-capacity',comment,'calc',2),('operation-capacity',maximum,'calc',2),('nesting-limit',deep,'calc',2)]

def run(cmd,data):
    return subprocess.run(cmd,input=data,capture_output=True,timeout=30)

def export(unit,name=None):
    return run([sys.executable,str(CLI)]+([name] if name else []),unit)

def semantic(env):
    assert env[:5]==b'OMSB\x01';ns,np=struct.unpack_from('<II',env,5);assert len(env)==13+ns+np
    return env[13:13+ns]

def semonly(sem):
    return b'OMSB\x01'+struct.pack('<II',len(sem),0)+sem

def unit(records,ninputs=None,lang=1,profile=1):
    """Semantic-only S1-shaped envelope from (op,a,b,imm) records; obligations/attributes per registry."""
    body=b''
    for op,a,b,imm in records:
        attr=0 if op==9 else 1;obl=0 if op==1 else (3 if op==5 else 1)
        body+=struct.pack('<8BHHQ',lang,1,op,1,attr,0,0,obl,a,b,imm)
    sem=b'OMSA\x01'+bytes([lang,profile,1,0,1])+struct.pack('<H',len(records))+SEMHASH+body
    return semonly(sem)

def ops_of(sem):
    count=struct.unpack_from('<H',sem,10)[0]
    return [(op,a,b,imm) for (d,v,op,t,attr,e,al,ob,a,b,imm) in (struct.unpack_from('<8BHHQ',sem,44+20*i) for i in range(count))]

def vectors(nargs,rng):
    if nargs==0:return [()]
    if nargs<=2:vs=list(itertools.product(BOUNDARY,repeat=nargs))
    else:vs=[tuple(BOUNDARY[(j+k)%len(BOUNDARY)] for k in range(nargs)) for j in range(len(BOUNDARY))]+[tuple(x if k==s else 1 for k in range(nargs)) for s in range(nargs) for x in BOUNDARY]
    return vs+[tuple(rng.getrandbits(64) for _ in range(nargs)) for _ in range(64)]

def harness(name,nargs):
    if not nargs:return '\n#include <stdio.h>\n#include <inttypes.h>\nint main(void){printf("%" PRIu64 "\\n",'+name+'());return 0;}\n'
    scan=' '.join(['%" SCNu64 "']*nargs);ptrs=','.join('&x['+str(j)+']' for j in range(nargs));args=','.join('x['+str(j)+']' for j in range(nargs))
    return '\n#include <stdio.h>\n#include <inttypes.h>\nint main(void){uint64_t x[6]={0};while(scanf("'+scan+'",'+ptrs+')=='+str(nargs)+')printf("%" PRIu64 "\\n",'+name+'('+args+'));return 0;}\n'

def native(out,tag,src,name,nargs,payload):
    c=out/(tag+'.c');exe=out/tag;c.write_bytes(src+harness(name,nargs).encode())
    r=subprocess.run([os.environ.get('CC','cc')]+CFLAGS+[str(c),'-o',str(exe)],capture_output=True,timeout=60)
    assert r.returncode==0,tag+' compile failed: '+r.stderr.decode(errors='replace')
    r=run([str(exe)],payload);assert r.returncode==0,tag+' run failed'
    return [int(x) for x in r.stdout.split()]

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--out',required=True);a=ap.parse_args()
    out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=False)
    for p in [IMPORTER,EXPORTER]:assert p.read_bytes()[:4]==b'\x7fELF',p
    rng=random.Random(622624);rows=[];failures=[];observations=0
    fixtures=json.loads((ROOT/'tests/universal/translation/fixtures-f1.json').read_text())
    cases=[]
    for f in fixtures['positives']:
        src=f['source'].encode();assert hashlib.sha256(src).hexdigest()==f['sha256'],f['id']
        cases.append((f['id'],src,f['name'],f['inputs']))
    for name,src,fn,nargs in controls():cases.append(('control-'+name,src,fn,nargs))
    # 1. Required boundaries: import -> export -> re-import identity; semantic-only; native differential.
    for cid,src,name,nargs in cases:
        item={'id':cid,'source_sha256':hashlib.sha256(src).hexdigest()}
        try:
            r=run([str(IMPORTER)],src);assert r.returncode==0,'import refused rc=%d'%r.returncode
            u1=r.stdout;s1=semantic(u1);ops=g.decode(u1,src,nargs)
            assert len(ops)>=3 and ops[-2]==(1,65535,65535,0) and ops[-1][0]==2 and ops[-1][2]==len(ops)-2 and ops[-1][1]<len(ops)-2,'importer unit not NORMALIZED'
            fname=None if name=='calc' else name
            e=export(u1,fname);assert e.returncode==0 and e.stderr==b'','export refused rc=%d %s'%(e.returncode,e.stderr)
            c=e.stdout
            assert c.startswith(b'#include <stdint.h>\nuint64_t '+name.encode()+b'(') and c.endswith(b'; }\n') and c.count(b'\n')==2,'output form'
            assert len(c)<=4096,'over 4096 bytes'
            d=run([str(EXPORTER)],bytes([len(fname or '')])+(fname or '').encode()+u1);assert d.returncode==0 and d.stdout==c,'native direct differs from public command'
            so=semonly(s1);v=run([str(SA),'decode'],so);assert v.returncode==0 and v.stdout==so,'semantic-only envelope not accepted by sa decode'
            e2=export(so,fname);assert e2.returncode==0 and e2.stdout==c,'semantic-only envelope exported differently'
            r2=run([str(IMPORTER)],c);assert r2.returncode==0,'re-import refused rc=%d %s'%(r2.returncode,r2.stderr)
            s2=semantic(r2.stdout);assert s2==s1,'re-imported semantic bytes differ'
            i1=run([str(SA),'identity'],u1).stdout;i2=run([str(SA),'identity'],r2.stdout).stdout;i3=run([str(SA),'identity'],so).stdout
            assert len(i1)==32 and i1==i2==i3,'semantic identity differs'
            vs=vectors(nargs,rng);payload=''.join(' '.join(map(str,x))+'\n' for x in vs).encode()
            orig=native(out,cid+'-orig',src,name,nargs,payload);expo=native(out,cid+'-export',c,name,nargs,payload)
            ref=[g.evaluate(ops,list(x)) for x in vs]
            assert len(orig)==len(vs) and orig==expo,'native original C vs exported C mismatch'
            assert expo==ref,'exported C vs REFERENCE model mismatch'
            observations+=len(vs)
            (out/(cid+'.export.c')).write_bytes(c)
            item.update({'result':'PASS','ops':len(ops),'export_bytes':len(c),'export_sha256':hashlib.sha256(c).hexdigest(),'semantic_identity':i1.hex(),'vectors':len(vs),'export_head':c.split(b'\n')[1][:80].decode()})
        except (AssertionError,ValueError,struct.error,IndexError) as exc:
            item.update({'result':'FAIL','failure':str(exc)});failures.append(item)
        rows.append(item)
    # 2. Constructed accepted units: non-NORMALIZED (+2 on re-import), shared SSA, permuted slots, zero inputs.
    A,B=(9,65535,65535,0),(9,65535,65535,1)
    built=[('non-normalized',[A,B,(2,0,1,0)],2,'i0 + i1'),
           ('shared-ssa',[A,B,(2,0,1,0),(4,2,2,0)],2,'(i0 + i1) * (i0 + i1)'),
           ('permuted-slots',[(9,65535,65535,1),(9,65535,65535,0),(3,0,1,0)],2,'i1 - i0'),
           ('right-assoc-depth-16',[A]+[(2,0,0,0)]+[(2,0,k,0) for k in range(1,17)],1,None),
           ('max-constant',[(1,65535,65535,MASK),(1,65535,65535,0),(3,1,0,0)],0,'0ULL - 18446744073709551615ULL')]
    constructed=[]
    for cid,recs,nargs,expr in built:
        item={'id':'built-'+cid}
        try:
            env=unit(recs);v=run([str(SA),'decode'],env);assert v.returncode==0,'constructed unit not valid v1'
            e=export(env);assert e.returncode==0,'export refused rc=%d %s'%(e.returncode,e.stderr)
            c=e.stdout;body=c.split(b'{ return ')[1][:-4].decode()
            if expr:assert body==expr,'expression '+body
            else:assert body.count('(')==16 and body.endswith(')'*16),'depth 16 chain parentheses'
            r2=run([str(IMPORTER)],c);assert r2.returncode==0,'re-import refused'
            s2=semantic(r2.stdout);ops2=ops_of(s2);ops=[tuple(x) for x in recs]
            vs=vectors(nargs,rng);payload=''.join(' '.join(map(str,x))+'\n' for x in vs).encode()
            expo=native(out,'built-'+cid,c,'calc',nargs,payload);ref=[g.evaluate(ops,list(x)) for x in vs]
            assert expo==ref,'exported C vs REFERENCE model mismatch'
            assert [g.evaluate(ops2,list(x)) for x in vs]==ref,'re-import evaluation differs'
            same=s2==semantic(env)
            if cid=='non-normalized':
                assert ops2==ops+[(1,65535,65535,0),(2,2,3,0)],'non-NORMALIZED unit must gain exactly one +0 pair'
            item.update({'result':'PASS','export':body[:80],'reimport_identity_equal':same,'reimport_ops':len(ops2),'vectors':len(vs)});observations+=len(vs)
        except (AssertionError,ValueError,struct.error,IndexError) as exc:
            item.update({'result':'FAIL','failure':str(exc)});failures.append(item)
        constructed.append(item)
    # 3. Refusals: status, empty stdout, named stderr.
    good=run([str(IMPORTER)],cases[2][1]).stdout
    sem=semantic(good)
    def patch(buf,off,val):b=bytearray(buf);b[off]=val;return bytes(b)
    doubling=[A]+[(2,0,0,0)]+[(2,k,k,0) for k in range(1,62)]
    rust=b'OMSA\x01'+bytes([2,2,1,0,1])+sem[10:44]+b''.join(bytes([2])+sem[44+20*i+1:64+20*i] for i in range(struct.unpack_from('<H',sem,10)[0]))
    div=bytearray(sem);n=struct.unpack_from('<H',sem,10)[0];off=44+20*(n-3);assert div[off+2]==2;div[off+2]=5;div[off+7]=3
    # Last field: the envelope must (True) / must not (False) decode under source-aware v1 (sa decode), None unchecked.
    refusals=[('two-arguments',['calc','x'],good,64,None),('name-main',['main'],good,64,None),('name-not-profile',['foo'],good,64,None),
              ('name-bare-omega_',['omega_'],good,64,None),('name-bad-char',['omega_a-b'],good,64,None),('name-33-bytes',['omega_'+'a'*27],good,64,None),
              ('empty-name',[''],good,64,None),
              ('empty-input',[],b'',65,False),('truncated',[],good[:-1],65,False),('trailing-byte',[],good+b'\x00',65,False),
              ('bad-magic',[],patch(good,0,0x58),65,False),('over-8192',[],good+b'\x00'*(8193-len(good)),65,False),
              ('sidecar-source-tamper',[],patch(good,len(good)-2,0x5a),65,False),('wrong-contract-hash',[],semonly(patch(sem,12,sem[12]^1)),65,False),
              ('profile-language-mismatch',[],semonly(patch(sem,6,2)),68,False),('rust-profile-2',[],semonly(rust),68,True),
              ('division-op-5',[],semonly(bytes(div)),68,True),('slot-gap',[],unit([A,(9,65535,65535,2),(2,0,1,0)]),68,True),
              ('op-capacity-65',[],unit([A,B]+[(2,0,0,0)]+[(2,k,0,0) for k in range(2,62)]),67,True),
              ('nesting-17',[],unit([A]+[(2,0,0,0)]+[(2,0,k,0) for k in range(1,18)]),67,True),
              ('shared-blowup-2^61',[],unit(doubling),67,True)]
    negatives=[]
    for cid,args,data,want,v1 in refusals:
        r=run([sys.executable,str(CLI)]+args,data)
        valid=None if v1 is None else run([str(SA),'decode'],data).returncode==0
        ok=r.returncode==want and r.stdout==b'' and b'C_EXPORT_' in r.stderr and valid==v1
        item={'id':cid,'want':want,'exit':r.returncode,'stdout_bytes':len(r.stdout),'stderr':r.stderr.decode(errors='replace').strip(),'valid_v1':valid,'result':'PASS' if ok else 'FAIL'}
        if not ok:failures.append(item)
        negatives.append(item)
    r=run([str(EXPORTER)],b'');ok=r.returncode==64 and not r.stdout
    negatives.append({'id':'native-no-frame','want':64,'exit':r.returncode,'result':'PASS' if ok else 'FAIL'})
    if not ok:failures.append(negatives[-1])
    # 4. Public command refuses changed source or artifact identities (78).
    for fault in ['source','binary']:
        with tempfile.TemporaryDirectory(prefix='closed-build-',dir=out) as td:
            t=Path(td)
            for rel in ['tools/adapters/c-export','src/source-aware/vendor/sha256.om','src/source-aware/core.om','src/adapters/c-u64-export.om','build/L622/build.json','build/L622/c-export']:
                p=t/rel;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/rel,p);p.chmod(0o700 if rel.endswith('c-export') else 0o600)
            with (t/('src/adapters/c-u64-export.om' if fault=='source' else 'build/L622/c-export')).open('ab') as f:f.write(b'changed')
            r=run([sys.executable,str(t/'tools/adapters/c-export')],good);ok=r.returncode==78 and not r.stdout and b'C_EXPORT_STALE_OR_MISSING_BUILD' in r.stderr
            negatives.append({'id':'public-'+fault+'-identity-change','want':78,'exit':r.returncode,'result':'PASS' if ok else 'FAIL'})
            if not ok:failures.append(negatives[-1])
    report={'schema':'omega.c-u64-export.selftest.v1','contract_sha256':hashlib.sha256((ROOT/'spec/universal/translation-v1.md').read_bytes()).hexdigest(),
            'result':'FAIL' if failures else 'PASS','status':'TESTED (author self-test; not QUALIFIED)','evidence':['DIFFERENTIAL','REFERENCE'],'proof':False,
            'source_commit':subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip(),
            'exporter_sha256':hashlib.sha256(EXPORTER.read_bytes()).hexdigest(),'importer_sha256':hashlib.sha256(IMPORTER.read_bytes()).hexdigest(),
            'cc':subprocess.check_output([os.environ.get('CC','cc'),'--version'],text=True).splitlines()[0],'cflags':CFLAGS,
            'boundaries':len(rows),'boundaries_passed':sum(x['result']=='PASS' for x in rows),
            'constructed':len(constructed),'constructed_passed':sum(x['result']=='PASS' for x in constructed),
            'refusals':len(negatives),'refusals_passed':sum(x['result']=='PASS' for x in negatives),'native_C_observations':observations,
            'cases':rows,'constructed_cases':constructed,'refusal_cases':negatives,'failures':failures}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    for x in rows:
        if x['id'] in ('F1-P05','F1-P13','control-operation-capacity'):print(x['id'],x.get('export_head',x.get('failure')))
    for x in failures:print('FAIL',json.dumps(x)[:400])
    print('%s boundaries=%d/%d identical_reimport=%d constructed=%d/%d refusals=%d/%d native_C_observations=%d'%(report['result'],report['boundaries_passed'],len(rows),report['boundaries_passed'],report['constructed_passed'],len(constructed),report['refusals_passed'],len(negatives),observations))
    return 1 if failures else 0
if __name__=='__main__':raise SystemExit(main())
