#!/usr/bin/env python3
"""Python lane self-test (#627; spec/universal/python-u64-v1.md, translation-compose-v1.md). Author-run TESTED
evidence, not QUALIFIED.

Same-language (Python -> Python), for every F2-python positive and accepted boundary control: native import;
structural decode of the Python-member unit (header/op bytes, obligations, spans, sidecar); the #621 codec
(`bash tools/source-aware/sa identity` in --sa-root) accepts it and agrees with the SHA256 identity; export;
the semantic-only envelope exports the same bytes; re-import; compose section 4.1 identity
N(strip(U_B)) = N(strip(U_A)); the F2 rewrite has the same N(strip()) as the C import of its F1 source;
python3 runs the original and the exported source on boundary and seeded random vectors and both must equal
the bigint evaluation of the records (REFERENCE + DIFFERENTIAL).

Compositions: C -> Python on every F1 positive and accepted C boundary control (C importer, original C by cc,
generated Python by python3). The native realization leg (translation-v1 section 5 lowering) is NOT_RUN.
Python -> C runs only with --c-export (a translation-v1 section 6 exporter CLI); that exporter accepts the C
member only, so the Python unit is retargeted R(U, C) first. Without it the direction is NOT_RUN.

Also: every refusal fixture (exact status, no stdout), exporter family acceptance (R(U, C/Rust/Python) export
identically), exporter refusals (profiles 3/5/6, division, malformed, limit, bad name), constructed
non-NORMALIZED / permuted-slot / shared-SSA units, and the public-command identity guards."""
from pathlib import Path
import argparse,hashlib,itertools,json,os,random,shutil,struct,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[3];sys.dont_write_bytecode=True
MASK=(1<<64)-1
BOUNDARY=[0,1,2,(1<<32)-1,1<<32,(1<<63)-1,1<<63,MASK-1,MASK]
IMPORTER=ROOT/'build/L627/import/python-import';EXPORTER=ROOT/'build/L627/export/python-export'
ICLI=ROOT/'tools/adapters/python-import';ECLI=ROOT/'tools/adapters/python-export'
SEMHASH=hashlib.sha256((ROOT/'spec/universal/semantics.md').read_bytes()).digest()
FAMILY={'C':(1,1,1,1),'Rust':(2,2,2,1),'Python':(3,4,3,5)}
CFLAGS=['-std=c17','-O2','-Wall','-Werror','-Wno-unused-parameter']
PYORACLE='''import sys
path,name,n=sys.argv[1],sys.argv[2],int(sys.argv[3])
ns={}
exec(compile(open(path,'rb').read().decode('ascii'),path,'exec'),ns)
f=ns[name]
for line in sys.stdin:
    xs=[int(t) for t in line.split()]
    assert len(xs)==n
    v=f(*xs)
    assert type(v) is int and 0<=v<2**64,(xs,v)
    print(v)
'''

class Fail(Exception):pass

def check(cond,msg):
    if not cond:raise Fail(msg)

def run(cmd,data=b'',**kw):
    return subprocess.run(cmd,input=data,capture_output=True,timeout=60,**kw)

def semantic(env):
    check(env[:5]==b'OMSB\x01','bad envelope magic');ns,np=struct.unpack_from('<II',env,5);check(len(env)==13+ns+np,'envelope length')
    return env[13:13+ns]

def semonly(sem):
    return b'OMSB\x01'+struct.pack('<II',len(sem),0)+sem

def records(sem):
    count=struct.unpack_from('<H',sem,10)[0]
    return [struct.unpack_from('<8BHHQ',sem,44+20*i) for i in range(count)]

def ops_of(sem):
    return [(op,a,b,imm) for (d,v,op,t,attr,e,al,ob,a,b,imm) in records(sem)]

def retarget(sem,member):
    """compose section 2: R(U, M) rewrites language/profile bytes, op dialect bytes, arithmetic obligations."""
    lang,prof,dia,obl=FAMILY[member];b=bytearray(sem);b[5]=lang;b[6]=prof
    for i in range(struct.unpack_from('<H',sem,10)[0]):
        o=44+20*i;b[o]=dia
        if b[o+2] in (2,3,4):b[o+7]=obl
    return bytes(b)

def normalized(ops):
    n=len(ops)
    return n>=3 and ops[n-2]==(1,65535,65535,0) and ops[n-1][0]==2 and ops[n-1][2]==n-2 and ops[n-1][1]<n-2 and ops[n-1][3]==0

def strip(sem):
    ops=ops_of(sem)
    if not normalized(ops):return sem
    n=len(ops)-2;return sem[:10]+struct.pack('<H',n)+sem[12:44+20*n]

def N(sem):
    return retarget(sem,'C')

def evaluate(ops,args):
    values=[]
    for op,a,b,imm in ops:
        if op==9:v=args[imm]
        elif op==1:v=imm
        elif op==2:v=values[a]+values[b]
        elif op==3:v=values[a]-values[b]
        elif op==4:v=values[a]*values[b]
        else:raise Fail('non-S1 op %d'%op)
        values.append(v&MASK)
    return values[-1]

def decode_python(env,src,arity):
    """Independent structural decode of one python-import envelope (python-u64-v1.md, source-aware-v1.md)."""
    ns,np=struct.unpack_from('<II',env,5);sem=semantic(env);p=env[13+ns:]
    check(sem[:10]==b'OMSA\x01\x03\x04\x01\x00\x01','wrong Python member header')
    count=struct.unpack_from('<H',sem,10)[0];check(1<=count<=64 and len(sem)==44+20*count,'operation count')
    check(sem[12:44]==SEMHASH,'wrong semantic contract digest')
    check(p[:6]==b'OMSP\x01\x01' and struct.unpack_from('<I',p,6)[0]==len(src),'sidecar header (encoding 1)')
    check(p[10:42]==hashlib.sha256(sem).digest() and p[42:74]==hashlib.sha256(src).digest(),'provenance binding')
    check(len(p)==74+8*count+len(src) and p[74+8*count:]==src,'source replay bytes')
    slots=[];ops=[]
    for i,(d,v,op,t,attr,eff,al,ob,a,b,imm) in enumerate(records(sem)):
        lo,hi=struct.unpack_from('<II',p,74+8*i);check(lo<=hi<=len(src),'span bounds');text=src[lo:hi]
        check(d==3 and v==1 and t==1 and eff==0 and al==0,'op %d fixed fields'%i)
        if op==9:
            check(attr==0 and ob==1 and a==b==65535 and imm<arity,'input record');slots.append(imm)
            check(text[:1].isalpha() and text.replace(b'_',b'').isalnum(),'input span is not the parameter name')
        elif op==1:
            check(attr==1 and ob==0 and a==b==65535,'constant record')
            check(text==str(imm).encode() or (i==count-2 and (lo,hi)==(0,0) and imm==0),'constant span')
        else:
            check(op in (2,3,4) and attr==1 and ob==5 and a<i and b<i and imm==0,'arithmetic record (obligations 5)')
            check(text=={2:b'+',3:b'-',4:b'*'}[op] or (i==count-1 and (lo,hi)==(0,0)),'operator span')
        ops.append((op,a,b,imm))
    check(sorted(slots)==list(range(arity)) and slots==list(range(arity)),'parameter slots')
    check(normalized(ops),'importer declares the +0 normalization pair')
    return ops

def vectors(nargs,rng):
    if nargs==0:return [()]
    if nargs<=2:vs=list(itertools.product(BOUNDARY,repeat=nargs))
    else:vs=[tuple(BOUNDARY[(j+k)%len(BOUNDARY)] for k in range(nargs)) for j in range(len(BOUNDARY))]+[tuple(x if k==s else 1 for k in range(nargs)) for s in range(nargs) for x in BOUNDARY]
    return vs+[tuple(rng.getrandbits(64) for _ in range(nargs)) for _ in range(64)]

def payload(vs):
    return ''.join(' '.join(map(str,v))+'\n' for v in vs).encode()

def python_oracle(out,tag,src,name,nargs,vs):
    path=out/(tag+'.py');path.write_bytes(src)
    r=run([sys.executable,'-I','-B',str(out/'pyoracle.py'),str(path),name,str(nargs)],payload(vs))
    check(r.returncode==0,tag+' python3 oracle failed: '+r.stderr.decode(errors='replace')[-400:])
    return [int(x) for x in r.stdout.split()]

def c_harness(name,nargs):
    if not nargs:return '\n#include <stdio.h>\n#include <inttypes.h>\nint main(void){printf("%" PRIu64 "\\n",'+name+'());return 0;}\n'
    scan=' '.join(['%" SCNu64 "']*nargs);ptrs=','.join('&x['+str(j)+']' for j in range(nargs));args=','.join('x['+str(j)+']' for j in range(nargs))
    return '\n#include <stdio.h>\n#include <inttypes.h>\nint main(void){uint64_t x[6]={0};while(scanf("'+scan+'",'+ptrs+')=='+str(nargs)+')printf("%" PRIu64 "\\n",'+name+'('+args+'));return 0;}\n'

def c_oracle(out,tag,src,name,nargs,vs):
    c=out/(tag+'.c');exe=out/tag;c.write_bytes(src+c_harness(name,nargs).encode())
    r=run([os.environ.get('CC','cc')]+CFLAGS+[str(c),'-o',str(exe)]);check(r.returncode==0,tag+' cc failed: '+r.stderr.decode(errors='replace')[-400:])
    r=run([str(exe)],payload(vs));check(r.returncode==0,tag+' C run failed')
    return [int(x) for x in r.stdout.split()]

class Ctx:
    def __init__(s,a,out):
        s.out=out;s.sa_root=Path(a.sa_root).resolve();s.cimport=Path(a.c_import).resolve();s.c_export=a.c_export
        s.rng=random.Random(627624);s.observations=0;s.sa_requests=0
    def sa(s,cmd,data):
        s.sa_requests+=1;return run(['bash','tools/source-aware/sa',cmd],data,cwd=str(s.sa_root))
    def identity(s,env):
        r=s.sa('identity',env);check(r.returncode==0 and len(r.stdout)==32,'sa identity refused: rc=%d %s'%(r.returncode,r.stderr.decode(errors='replace')))
        check(r.stdout==hashlib.sha256(b'omega.source.semantic.v1'+semantic(env)).digest(),'sa identity differs from SHA256 semantic identity')
        return r.stdout.hex()
    def pyimport(s,src):
        r=run([str(IMPORTER)],src);check(r.returncode==0 and r.stdout,'python import refused rc=%d %s'%(r.returncode,r.stderr.decode(errors='replace')))
        return r.stdout
    def pyexport(s,env,name=None):
        r=run([sys.executable,str(ECLI)]+([name] if name else []),env)
        check(r.returncode==0 and r.stdout,'python export refused rc=%d %s'%(r.returncode,r.stderr.decode(errors='replace')))
        return r.stdout
    def cimp(s,src):
        r=run([str(s.cimport)],src);check(r.returncode==0,'C import refused rc=%d'%r.returncode);return r.stdout
    def agree(s,tag,ops,nargs,legs):
        """legs: name -> list of outputs; all must equal the REFERENCE evaluation of ops."""
        vs=legs.pop('_vectors');ref=[evaluate(ops,v) for v in vs]
        for leg,vals in legs.items():
            if vals!=ref:
                bad=[(list(v),x,y) for v,x,y in zip(vs,vals,ref) if x!=y][:5]
                raise Fail('%s: %s disagrees with REFERENCE on %d vectors, e.g. %s'%(tag,leg,sum(x!=y for x,y in zip(vals,ref))+abs(len(vals)-len(ref)),bad))
        s.observations+=len(vs)*len(legs);return len(vs)

def same_language(ctx,case,cases_c):
    tag=case['id'];src=case['source'].encode();nargs=case['inputs'];name=case['name'];out=ctx.out
    check(hashlib.sha256(src).hexdigest()==case['sha256'],tag+' fixture sha256')
    ua=ctx.pyimport(src)
    cli=run([sys.executable,str(ICLI)],src);check(cli.returncode==0 and cli.stdout==ua,'public python-import differs from native')
    ops=decode_python(ua,src,nargs);ident=ctx.identity(ua)
    srcb=ctx.pyexport(ua)
    check(ctx.pyexport(semonly(semantic(ua)))==srcb,'semantic-only envelope exports differently (replayed, not generated)')
    native=run([str(EXPORTER)],b'\x00'+ua);check(native.returncode==0 and native.stdout==srcb,'native exporter differs from public command')
    ub=ctx.pyimport(srcb);decode_python(ub,srcb,nargs);ctx.identity(ub)
    check(N(strip(semantic(ub)))==N(strip(semantic(ua))),'compose 4.1 identity N(strip(U_B)) != N(strip(U_A))')
    exact=semantic(ub)==semantic(ua)
    row={'id':tag,'inputs':nargs,'ops':len(ops),'source_bytes':len(src),'identity':ident,'reimport_semantic_bytes_equal':exact,
         'exported_sha256':hashlib.sha256(srcb).hexdigest()}
    csrc=cases_c.get(case.get('f1') or case.get('control'))
    if csrc is not None:
        uc=ctx.cimp(csrc);check(N(strip(semantic(uc)))==N(strip(semantic(ua))),'F2 rewrite is not the F1 expression: N(strip()) differs from the C import')
        check(N(semantic(uc))==N(semantic(ua)),'F2 rewrite N() differs from the C import');row['same_N_as_F1_C_import']=True
    vs=vectors(nargs,ctx.rng)
    legs={'_vectors':vs,'python3(original)':python_oracle(out,tag+'-orig',src,name,nargs,vs),'python3(exported)':python_oracle(out,tag+'-exp',srcb,'calc',nargs,vs)}
    row['vectors']=ctx.agree(tag,ops,nargs,legs);row['result']='PASS';(out/(tag+'.exported.py')).write_bytes(srcb)
    return row,srcb

def c_to_python(ctx,tag,csrc,name,nargs):
    out=ctx.out;ua=ctx.cimp(csrc);ctx.identity(ua);ops=ops_of(semantic(ua))
    srcb=ctx.pyexport(ua);check(ctx.pyexport(semonly(semantic(ua)))==srcb,'C->Python: semantic-only export differs')
    for m in ['Rust','Python']:
        env=semonly(retarget(semantic(ua),m));ctx.identity(env)
        check(ctx.pyexport(env)==srcb,'family: R(U,%s) exports differently'%m)
    ub=ctx.pyimport(srcb);decode_python(ub,srcb,nargs)
    check(N(strip(semantic(ub)))==N(strip(semantic(ua))),'C->Python identity N(strip(U_B)) != N(strip(U_A))')
    vs=vectors(nargs,ctx.rng)
    legs={'_vectors':vs,'cc(original C)':c_oracle(out,tag+'-c',csrc,name,nargs,vs),'python3(generated)':python_oracle(out,tag+'-py',srcb,'calc',nargs,vs)}
    n=ctx.agree(tag,ops,nargs,legs);(out/(tag+'.generated.py')).write_bytes(srcb)
    return {'id':tag,'identity':'PASS','differential':'PASS','legs':['REFERENCE','cc(original C)','python3(generated Python)'],
            'native_realization':'NOT_RUN','vectors':n,'generated_sha256':hashlib.sha256(srcb).hexdigest(),'result':'PASS'},srcb

def python_to_c(ctx,case):
    out=ctx.out;tag='P2C-'+case['id'];src=case['source'].encode();nargs=case['inputs']
    ua=ctx.pyimport(src);ops=ops_of(semantic(ua))
    # The section 6 C exporter (G1) accepts the C member only: retarget R(U, C) first.
    rc=semonly(retarget(semantic(ua),'C'));ctx.identity(rc)
    r=run([sys.executable,ctx.c_export],rc);check(r.returncode==0,'C export refused R(U,C) rc=%d %s'%(r.returncode,r.stderr.decode(errors='replace')))
    srcb=r.stdout;ub=ctx.cimp(srcb)
    check(N(strip(semantic(ub)))==N(strip(semantic(ua))),'Python->C identity N(strip(U_B)) != N(strip(U_A))')
    vs=vectors(nargs,ctx.rng)
    legs={'_vectors':vs,'python3(original Python)':python_oracle(out,tag+'-py',src,case['name'],nargs,vs),'cc(generated C)':c_oracle(out,tag+'-c',srcb,'calc',nargs,vs)}
    return {'id':tag,'identity':'PASS','differential':'PASS','retargeted_to_C_first':True,'native_realization':'NOT_RUN','vectors':ctx.agree(tag,ops,nargs,legs),'result':'PASS'}

def unit(recs,lang=1,profile=1,runtime=0,abi=1,dialect=None,types=None,obl_arith=None,contract=SEMHASH):
    """Semantic-only envelope from (op,a,b,imm) records with registry attributes for u64 members."""
    dialect=lang if dialect is None else dialect;body=b''
    if obl_arith is None:obl_arith=5 if profile in (4,6) else 1
    for op,a,b,imm in recs:
        attr=0 if op==9 else 1;obl=1 if op==9 else 0 if op==1 else (obl_arith|2 if op==5 else obl_arith)
        body+=struct.pack('<8BHHQ',dialect,1,op,1,attr,0,0,obl,a,b,imm)
    return semonly(b'OMSA\x01'+bytes([lang,profile,1,runtime,abi])+struct.pack('<H',len(recs))+contract+body)

def bigint_unit():
    """Profile 3 (cpython-int-pure-v1): type 2 inputs/constants, bigint add (effects 20, alias 255, obligations 8)."""
    body=struct.pack('<8BHHQ',3,1,9,2,0,0,0,1,65535,65535,0)+struct.pack('<8BHHQ',3,1,9,2,0,0,0,1,65535,65535,1)+struct.pack('<8BHHQ',3,1,2,2,2,20,255,8,0,1,0)
    return semonly(b'OMSA\x01'+bytes([3,3,1,1,1])+struct.pack('<H',3)+SEMHASH+body)

def exporter_checks(ctx):
    rows=[];out=ctx.out
    def refuse(name,env,want,args=()):
        r=run([sys.executable,str(ECLI)]+list(args),env)
        check(r.returncode==want and r.stdout==b'' and b'PY_EXPORT_' in r.stderr,'%s: want %d got %d %r'%(name,want,r.returncode,r.stderr))
        rows.append({'name':name,'exit':r.returncode,'result':'PASS'})
    add=[(9,65535,65535,0),(9,65535,65535,1),(2,0,1,0)]
    for name,env in [('profile6-same-records',unit(add,lang=3,profile=6,runtime=1,abi=2)),('profile3-bigint',bigint_unit()),('osc-profile5',unit(add,lang=4,profile=5))]:
        d=ctx.sa('decode',env);check(d.returncode==0 and d.stdout==env,name+': constructed unit is not codec-valid')
        refuse(name,env,68)
    div=unit([(9,65535,65535,0),(9,65535,65535,1),(5,0,1,0)])
    d=ctx.sa('decode',div);check(d.returncode==0,'division unit not codec-valid');refuse('u64-division',div,68)
    good=unit(add)
    refuse('truncated',good[:-1],65);refuse('trailing-byte',good+b'\x00',65);refuse('bad-magic',b'X'+good[1:],65)
    refuse('wrong-contract',unit(add,contract=b'\x00'*32),65);refuse('empty',b'',65)
    blow=[(9,65535,65535,0)]+[(4,k,k,0) for k in range(6)]
    refuse('tree-expansion-over-64-operations',unit(blow),67)
    for bad in ['Calc','def','x'*33,'1abc','a-b']:refuse('bad-name-'+bad[:8],good,64,[bad])
    refuse('two-arguments',good,64,['a','b'])
    # Constructed units: not NORMALIZED, permuted input slots, shared SSA results; evaluation equality.
    for name,recs,n in [('not-normalized',add,2),('permuted-slots',[(9,65535,65535,1),(9,65535,65535,0),(3,0,1,0)],2),
                        ('shared-ssa',[(9,65535,65535,0),(9,65535,65535,1),(4,0,1,0),(2,2,2,0)],2),
                        ('const-root-not-normalized',[(1,65535,65535,MASK)],0),('input-root-last-slot',[(9,65535,65535,0),(9,65535,65535,1)],2),
                        ('capacity-61-chain-not-normalized',[(9,65535,65535,0),(9,65535,65535,1)]+[(2,0 if k==0 else k+1,0,0) for k in range(60)],2)]:
        env=unit(recs);ctx.identity(env);src=ctx.pyexport(env);ub=ctx.pyimport(src);decode_python(ub,src,n)
        ops=ops_of(semantic(env));vs=vectors(n,ctx.rng)
        nv=ctx.agree(name,ops,n,{'_vectors':vs,'reimport':[evaluate(ops_of(semantic(ub)),v) for v in vs],'python3(exported)':python_oracle(out,'ctor-'+name,src,'calc',n,vs)})
        rows.append({'name':name,'result':'PASS','vectors':nv,'identity_equal_after_reimport':N(strip(semantic(ub)))==N(strip(semantic(env))),
                     'note':'not NORMALIZED or not import-shaped: re-import adds the pair / reorders inputs; checked by equal evaluation (translation-v1 section 4)'})
    env=unit(add);src=ctx.pyexport(env,'omega_fold');check(src.startswith(b'def omega_fold(i0, i1):\n'),'named export');ctx.pyimport(src)
    rows.append({'name':'named-export','result':'PASS'})
    return rows

def refusals(ctx,fx):
    rows=[]
    for r in fx['refusals']:
        src=bytes.fromhex(r['source_hex']) if 'source_hex' in r else r['source'].encode()
        check(hashlib.sha256(src).hexdigest()==r['sha256'],r['id']+' fixture sha256')
        p=run([str(IMPORTER)],src);ok=p.returncode==r['status'] and p.stdout==b'' and p.stderr.startswith(b'PY_IMPORT_')
        rows.append({'id':r['id'],'want':r['status'],'exit':p.returncode,'stderr':p.stderr.decode(errors='replace').strip(),'result':'PASS' if ok else 'FAIL'})
    return rows

def public_checks(ctx):
    rows=[];src=b'def calc(a, b):\n    return (a + b) & 0xFFFFFFFFFFFFFFFF\n'
    r=run([sys.executable,str(ICLI),'x'],src);check(r.returncode==64 and not r.stdout and b'PY_IMPORT_BAD_COMMAND' in r.stderr,'import args');rows.append('import-arguments-refused')
    r=subprocess.run([str(IMPORTER)],stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE,preexec_fn=lambda:os.close(0),timeout=10)
    check(r.returncode==77 and not r.stdout and r.stderr==b'PY_IMPORT_IO\n','closed stdin');rows.append('import-closed-stdin')
    env=ctx.pyimport(src)
    for tool,rels in [('python-import',['src/source-aware/vendor/sha256.om','src/adapters/python-u64-import.om','build/L627/import/build.json','build/L627/import/python-import']),
                      ('python-export',['src/source-aware/vendor/sha256.om','src/source-aware/core.om','src/adapters/python-u64-export.om','build/L627/export/build.json','build/L627/export/python-export'])]:
        for fault in ['source','binary']:
            with tempfile.TemporaryDirectory(prefix='closed-build-',dir=ctx.out) as td:
                t=Path(td)
                for rel in ['tools/adapters/'+tool]+rels:
                    p=t/rel;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/rel,p);p.chmod(0o700 if rel.endswith(tool) else 0o600)
                victim=t/(rels[-3] if fault=='source' else rels[-1]);victim.write_bytes(victim.read_bytes()+b'changed')
                r=run([sys.executable,str(t/'tools/adapters'/tool)],src if tool=='python-import' else env)
                check(r.returncode==78 and not r.stdout and b'STALE_OR_MISSING_BUILD' in r.stderr,tool+' '+fault+' identity change not refused');rows.append(tool+'-'+fault+'-identity-change-refused')
    return rows

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--out',required=True)
    ap.add_argument('--sa-root',default=str(Path.home()/'workspace/universal-read'),help='checkout whose tools/source-aware/sa (build/L574/sa) is the reference codec')
    ap.add_argument('--c-import',default=str(Path.home()/'workspace/universal-read/build/L589/c-import'))
    ap.add_argument('--c-export',default='',help='translation-v1 section 6 C exporter CLI (tools/adapters/c-export); empty = Python->C NOT_RUN')
    a=ap.parse_args();out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=False);(out/'pyoracle.py').write_text(PYORACLE)
    ctx=Ctx(a,out)
    for p in [IMPORTER,EXPORTER,ctx.cimport]:check(p.read_bytes()[:4]==b'\x7fELF','missing native artifact '+str(p))
    fx=json.loads((ROOT/'tests/universal/translation/fixtures-f2-python.json').read_text())
    f1=json.loads((ROOT/'tests/universal/translation/fixtures-f1.json').read_text())
    cases_c={f['id']:f['source'].encode() for f in f1['positives']}
    H='#include <stdint.h>\n';base=(H+'uint64_t calc(uint64_t a,uint64_t b){return a+b;}').encode()
    controls_c={'exact-source-capacity':base+b' '*(4096-len(base)),'comment-at-capacity':base+b'//'+b'x'*(4096-len(base)-2),
                'operation-capacity':(H+'uint64_t calc(uint64_t a,uint64_t b){return '+'+'.join(['a']*61)+';}').encode(),
                'nesting-limit':(H+'uint64_t calc(uint64_t a,uint64_t b){return '+'('*16+'a'+')'*16+';}').encode()}
    cases_c.update(controls_c)
    report={'schema':'omega.python-u64.selftest.v1','lane':'#627','classification':'TESTED (author self-test), not QUALIFIED','proof':False,
            'source_commit':subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip(),
            'importer_sha256':hashlib.sha256(IMPORTER.read_bytes()).hexdigest(),'exporter_sha256':hashlib.sha256(EXPORTER.read_bytes()).hexdigest(),
            'c_importer':str(ctx.cimport),'c_importer_sha256':hashlib.sha256(ctx.cimport.read_bytes()).hexdigest(),
            'sa_binary_sha256':hashlib.sha256((ctx.sa_root/'build/L574/sa').read_bytes()).hexdigest(),'python3':sys.version.split()[0]}
    failures=[];same=[];c2p=[];p2c=[];shown={}
    for case in fx['positives']+fx['controls']:
        try:
            row,srcb=same_language(ctx,case,cases_c);same.append(row)
            if case.get('f1') in ('F1-P05','F1-P13'):shown['python->python '+case['id']]=srcb.decode()
        except Fail as e:failures.append({'case':case['id'],'failure':str(e)});same.append({'id':case['id'],'result':'FAIL','failure':str(e)})
    for f in f1['positives']:
        try:
            row,srcb=c_to_python(ctx,'C2P-'+f['id'],f['source'].encode(),f['name'],f['inputs']);c2p.append(row)
            if f['id'] in ('F1-P05','F1-P13'):shown['C->Python '+f['id']]=srcb.decode()
        except Fail as e:failures.append({'case':'C2P-'+f['id'],'failure':str(e)})
    for cid,csrc in controls_c.items():
        try:c2p.append(c_to_python(ctx,'C2P-ctl-'+cid,csrc,'calc',2)[0])
        except Fail as e:failures.append({'case':'C2P-ctl-'+cid,'failure':str(e)})
    if a.c_export:
        for case in fx['positives']+fx['controls']:
            try:p2c.append(python_to_c(ctx,case))
            except Fail as e:failures.append({'case':'P2C-'+case['id'],'failure':str(e)})
        p2c_status={'status':'RUN','c_export':a.c_export,'note':'R(U, C) applied before the C exporter (it accepts the C member only)'}
    else:
        p2c_status={'status':'NOT_RUN','reason':'no --c-export given (translation-v1 section 6 exporter is not on this base)'}
    try:exp=exporter_checks(ctx)
    except Fail as e:failures.append({'case':'exporter-checks','failure':str(e)});exp=[]
    neg=refusals(ctx,fx);failures+=[{'case':r['id'],'failure':'refusal status %d != %d'%(r['exit'],r['want'])} for r in neg if r['result']!='PASS']
    try:pub=public_checks(ctx)
    except Fail as e:failures.append({'case':'public-commands','failure':str(e)});pub=[]
    report.update({'result':'FAIL' if failures else 'PASS','same_language':{'executed':len(same),'passed':sum(r.get('result')=='PASS' for r in same),'cases':same},
        'not_in_profile':fx['not_in_profile'],
        'compositions':{'C->Python':{'executed':len(c2p),'passed':sum(r['result']=='PASS' for r in c2p),'native_realization':'NOT_RUN (translation-v1 section 5 lowering is not on this base)','cases':c2p},
                        'Python->C':dict(p2c_status,executed=len(p2c),passed=sum(r['result']=='PASS' for r in p2c),cases=p2c)},
        'refusals':{'executed':len(neg),'passed':sum(r['result']=='PASS' for r in neg),'cases':neg},
        'exporter_checks':{'executed':len(exp),'passed':len(exp),'cases':exp},'public_command_checks':pub,
        'observations':ctx.observations,'source_aware_requests':ctx.sa_requests,'shown_exports':shown,'failures':failures})
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    for k,v in shown.items():print('--- '+k+'\n'+v,end='')
    summary={'result':report['result'],'same_language':'%d/%d'%(report['same_language']['passed'],len(same)),
             'C->Python':'%d/%d (native realization NOT_RUN)'%(report['compositions']['C->Python']['passed'],len(c2p)),
             'Python->C':p2c_status['status']+(' %d/%d'%(len(p2c),len(p2c)) if a.c_export and not any(f['case'].startswith('P2C') for f in failures) else ''),
             'refusals':'%d/%d'%(report['refusals']['passed'],len(neg)),'exporter_checks':len(exp),'public_command_checks':len(pub),
             'not_in_profile':len(fx['not_in_profile']),'observations':ctx.observations,'sa_requests':ctx.sa_requests}
    print(json.dumps(summary,indent=1))
    for f in failures[:20]:print('FAIL',f['case'],f['failure'][:300])
    print(('PASS' if not failures else 'FAIL')+' python-u64 selftest; report '+str(out/'result.json'))
    return 1 if failures else 0

if __name__=='__main__':raise SystemExit(main())
