#!/usr/bin/env python3
"""Rust lane (#626) author self-test: TESTED evidence only (translation-v1 section 10), never QUALIFIED.

For every F2-rust fixture: Rust import, independent decode, #621 `sa` identity/decode/replay, cross-importer
identity against the C import of the F1 source, export (all three S1 members, with and without sidecar),
re-import identity (translation-compose-v1 section 4.1), and rustc DIFFERENTIAL (original and exported
Rust, -O -C overflow-checks=on) against a bigint evaluation of the records. Compositions C->Rust always;
Rust->C only with --c-export (the G1 C exporter accepts C-member units only, so R(U, C) is applied first).
"""
from pathlib import Path
import argparse,hashlib,json,os,random,shutil,struct,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[3]
MASK=(1<<64)-1
SEMDOM=b'omega.source.semantic.v1'
CONTRACT=hashlib.sha256((ROOT/'spec/universal/semantics.md').read_bytes()).digest()
MEMBERS={'C':(1,1,1),'Rust':(2,2,1),'Python':(3,4,5)}  # language, profile, arithmetic obligations
BOUNDARY=[0,1,2,(1<<32)-1,1<<32,(1<<63)-1,1<<63,MASK-1,MASK]
IMPORT=ROOT/'tools/adapters/rust-import';EXPORT=ROOT/'tools/adapters/rust-export'

class Fail(Exception):pass
def check(c,msg):
    if not c:raise Fail(msg)

def run(cmd,data,cwd=None,timeout=60):
    return subprocess.run(cmd,input=data,capture_output=True,timeout=timeout,cwd=cwd)

def py(tool,data):return run([sys.executable,str(tool)],data)

# ---- independent envelope model (not the #621 core) ----
def split(env):
    check(len(env)>=13 and env[:5]==b'OMSB\x01','bad envelope magic')
    ns,np_=struct.unpack_from('<II',env,5);check(len(env)==13+ns+np_,'bad envelope length')
    return env[13:13+ns],env[13+ns:]

def ops_of(sem):
    count=struct.unpack_from('<H',sem,10)[0];check(len(sem)==44+20*count,'bad semantic length')
    return [struct.unpack_from('<8BHHQ',sem,44+20*i) for i in range(count)]

def decode(env,member):
    lang,prof,arob=MEMBERS[member]
    sem,side=split(env)
    check(sem[:5]==b'OMSA\x01' and sem[5]==lang and sem[6]==prof and sem[7:10]==b'\x01\x00\x01','wrong %s header'%member)
    check(sem[12:44]==CONTRACT,'wrong parent semantics hash')
    ops=[];slots=[]
    for i,(d,v,op,t,attr,eff,alias,obl,a,b,imm) in enumerate(ops_of(sem)):
        check(d==lang and v==1 and t==1 and eff==0 and alias==0,'op %d fields'%i)
        if op==9:check(attr==0 and obl==1 and a==b==65535,'input op');slots.append(imm)
        elif op==1:check(attr==1 and obl==0 and a==b==65535,'const op')
        else:check(op in (2,3,4) and attr==1 and obl==arob and a<i and b<i and imm==0,'arith op %d'%i)
        ops.append((op,a,b,imm))
    check(sorted(slots)==list(range(len(slots))),'input slots not a permutation')
    return sem,side,ops

def sidecar(sem,side,src,count):
    check(side[:6]==b'OMSP\x01\x01','sidecar must be v1 encoding 1 (strict UTF-8)')
    check(struct.unpack_from('<I',side,6)[0]==len(src) and side[10:42]==hashlib.sha256(sem).digest() and side[42:74]==hashlib.sha256(src).digest(),'bad sidecar binding')
    check(len(side)==74+8*count+len(src) and side[74+8*count:]==src,'sidecar source mismatch')
    return [struct.unpack_from('<II',side,74+8*i) for i in range(count)]

def evaluate(ops,args):
    vals=[]
    for op,a,b,imm in ops:
        v=args[imm] if op==9 else imm if op==1 else vals[a]+vals[b] if op==2 else vals[a]-vals[b] if op==3 else vals[a]*vals[b]
        vals.append(v&MASK)
    return vals[-1]

def normalized(ops):
    n=len(ops)
    return n>=3 and ops[n-2][0]==1 and ops[n-2][3]==0 and ops[n-1][0]==2 and ops[n-1][2]==n-2 and ops[n-1][1]<n-2

def retarget(sem,member):
    """R(U, M), translation-compose-v1 section 2: header language/profile, op dialect, arithmetic obligations."""
    lang,prof,arob=MEMBERS[member];b=bytearray(sem);b[5]=lang;b[6]=prof
    for i,(_,_,op,*_) in enumerate(ops_of(sem)):
        b[44+20*i]=lang
        if op in (2,3,4):b[44+20*i+7]=arob
    return bytes(b)

def N(sem):return retarget(sem,'C')

def strip(sem):
    ops=ops_of(sem)
    if not normalized([(o[2],o[8],o[9],o[10]) for o in ops]):return sem
    n=len(ops)-2;b=bytearray(sem[:44+20*n]);struct.pack_into('<H',b,10,n);return bytes(b)

def semonly(sem):return b'OMSB\x01'+struct.pack('<II',len(sem),0)+sem
def identity(sem):return hashlib.sha256(SEMDOM+sem).hexdigest()

# ---- oracles ----
class Oracles:
    def __init__(s,out,rustc,cc):s.out=out;s.rustc=rustc;s.cc=cc;s.cache={};s.compiles=0
    def build(s,kind,text,name,nargs):
        key=hashlib.sha256(kind.encode()+b'\0'+name.encode()+b'\0'+text).hexdigest()
        if key in s.cache:return s.cache[key]
        d=s.out/'oracle';d.mkdir(exist_ok=True);exe=d/key[:24]
        if kind=='rust':
            call=name+'('+', '.join('v[%d]'%j for j in range(nargs))+')'
            harness=('\nfn main() {\n    use std::io::BufRead;\n    for line in std::io::stdin().lock().lines() {\n'
                     '        let line = line.unwrap();\n        let v: Vec<u64> = line.split_whitespace().map(|x| x.parse().unwrap()).collect();\n'
                     '        assert_eq!(v.len(), %d);\n        println!("{}", %s);\n    }\n}\n'%(nargs,call))
            f=d/(key[:24]+'.rs');f.write_bytes(text+harness.encode())
            cmd=['nice','-n','19',s.rustc,'--edition','2021','-O','-C','overflow-checks=on','-o',str(exe),str(f)]
        else:
            f=d/(key[:24]+'.c')
            body=('uint64_t x[6]={0};while(scanf("'+' '.join(['%" SCNu64 "']*nargs)+'",'+','.join('&x[%d]'%j for j in range(nargs))+')==%d)'%nargs+
                  'printf("%" PRIu64 "\\n",'+name+'('+','.join('x[%d]'%j for j in range(nargs))+'));') if nargs else 'printf("%" PRIu64 "\\n",'+name+'());'
            f.write_bytes(text+('\n#include <stdio.h>\n#include <inttypes.h>\nint main(void){'+body+'return 0;}\n').encode())
            cmd=['nice','-n','19',s.cc,'-std=c17','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter',str(f),'-o',str(exe)]
        r=run(cmd,None,timeout=180);s.compiles+=1
        check(r.returncode==0,kind+' oracle compile failed: '+r.stderr.decode(errors='replace')[-800:])
        s.cache[key]=exe;return exe
    def values(s,kind,text,name,vectors):
        nargs=len(vectors[0]);exe=s.build(kind,text,name,nargs)
        r=run([str(exe)],''.join(' '.join(map(str,v))+'\n' for v in vectors).encode(),timeout=60)
        check(r.returncode==0,kind+' oracle run failed: '+r.stderr.decode(errors='replace')[-400:])
        return [int(x) for x in r.stdout.split()]

def vectors(nargs,rng):
    if nargs==0:return [()]
    if nargs<=2:base=[(x,) for x in BOUNDARY] if nargs==1 else [(x,y) for x in BOUNDARY for y in BOUNDARY]
    else:base=[tuple(BOUNDARY[(j+k)%len(BOUNDARY)] for k in range(nargs)) for j in range(len(BOUNDARY))]
    return base+[tuple(rng.getrandbits(64) for _ in range(nargs)) for _ in range(32)]

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--out',required=True)
    ap.add_argument('--c-import',default=str(ROOT/'build/L589/c-import'),help='native C importer (default: in-tree build of mk/c-u64-import.mk)')
    ap.add_argument('--sa-root',default=str(ROOT),help='checkout holding tools/source-aware/sa and build/L574/sa (default: this repo)')
    ap.add_argument('--c-export',default=os.environ.get('RUST_SELFTEST_C_EXPORT',''),help='public tools/adapters/c-export of a passing G1 checkout')
    ap.add_argument('--rustc',default=shutil.which('rustc') or 'rustc')
    ap.add_argument('--cc',default=os.environ.get('CC','cc'))
    a=ap.parse_args()
    out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=False)
    cimp=Path(a.c_import);sa_root=Path(a.sa_root)
    O=Oracles(out,a.rustc,a.cc);rng=random.Random(626);failures=[];counts={}
    def bump(k,n=1):counts[k]=counts.get(k,0)+n
    def sa(cmd,env,prefix=b''):
        r=run(['bash','tools/source-aware/sa',cmd],prefix+env,cwd=sa_root);bump('sa_requests');return r
    def rimport(src):
        r=py(IMPORT,src);bump('rust_import_runs');return r
    def rexport(env):
        r=py(EXPORT,env);bump('rust_export_runs');return r

    f1=json.loads((ROOT/'tests/universal/translation/fixtures-f1.json').read_text())
    f2=json.loads((ROOT/'tests/universal/translation/fixtures-f2-rust.json').read_text())
    f1pos={p['id']:p for p in f1['positives']}
    check(len(f2['positives'])+len(f2['not_in_profile'])==len(f1pos),'F2-rust does not cover every F1 positive')
    check({p['f1_id'] for p in f2['positives']}|{p['f1_id'] for p in f2['not_in_profile']}==set(f1pos),'F2-rust F1 id set')

    # Oracle self-check: a plain-operator program must panic under the oracle flags (proves the flag bites).
    plain=O.build('rust',b'pub fn calc(a: u64, b: u64) -> u64 { a + b }\n','calc',2)
    r=run([str(plain)],b'%d 1\n'%MASK);check(r.returncode!=0 and b'overflow' in r.stderr,'overflow-checks oracle control did not panic')
    counts['oracle_overflow_control']='PASS'

    def export_checks(sem,env_with_sidecar,tag):
        """Exporter: identical bytes for every member with or without sidecar; returns src_B."""
        r=rexport(env_with_sidecar);check(r.returncode==0 and r.stderr==b'',tag+' export refused rc=%d %s'%(r.returncode,r.stderr))
        text=r.stdout
        for m in MEMBERS:
            u=semonly(retarget(sem,m))
            check(sa('decode',u).returncode==0,tag+' R(U,%s) not a valid v1 unit'%m)
            q=rexport(u);check(q.returncode==0 and q.stdout==text,tag+' export differs for member '+m+' (semantic-only)')
        bump('member_exports',3)
        check(text.startswith(b'pub fn calc(') and text.endswith(b' }\n') and text.count(b'\n')==1,tag+' export form')
        check(not any(op in text for op in (b' + ',b' - ',b' * ')),tag+' plain operator in export')
        return text

    def reimport_identity(sem_a,src_b,tag):
        r=rimport(src_b);check(r.returncode==0,tag+' re-import refused rc=%d %s'%(r.returncode,r.stderr))
        sem_b,side_b,ops_b=decode(r.stdout,'Rust');sidecar(sem_b,side_b,src_b,len(ops_b))
        check(N(strip(sem_b))==N(strip(sem_a)),tag+' identity N(strip(U_B)) != N(strip(U_A))')
        return sem_b,ops_b

    rows=[]
    for fx in f2['positives']:
        tag=fx['id'];item={'id':tag}
        try:
            src=fx['source'].encode();check(hashlib.sha256(src).hexdigest()==fx['sha256'],'fixture sha')
            p1=f1pos[fx['f1_id']];check(p1['sha256']==fx['f1_sha256'] and p1['inputs']==fx['inputs'] and p1['name']==fx['name'],'F1 link')
            r=rimport(src);check(r.returncode==0 and r.stderr==b'','import refused rc=%d %s'%(r.returncode,r.stderr))
            env=r.stdout;sem,side,ops=decode(env,'Rust');spans=sidecar(sem,side,src,len(ops))
            check(normalized(ops),'importer must append the normalization pair')
            check(spans[-1]==spans[-2]==(0,0),'synthetic pair spans')
            for (op,_,_,imm),(lo,hi) in zip(ops[:-2],spans[:-2]):
                t=src[lo:hi]
                if op==1:check(t==b'%du64'%imm,'literal span')
                elif op==9:check(len(t)>0 and t.isascii(),'input span')
                else:check(t==b'wrapping_'+{2:b'add',3:b'sub',4:b'mul'}[op],'method span')
            ident=sa('identity',env);check(ident.returncode==0 and ident.stdout.hex()==identity(sem),'sa identity refused or differs')
            check(sa('decode',env).stdout==env,'sa decode changed unit');check(sa('replay',env).stdout==src,'sa replay differs from source')
            c=run([str(cimp)],p1['source'].encode());bump('c_import_runs')
            check(c.returncode==0,'C import of F1 refused')
            csem,_,cops=decode(c.stdout,'C')
            check(N(sem)==N(csem),'cross-importer identity: N(import_Rust(F2)) != N(import_C(F1))')
            exported=export_checks(sem,env,tag)
            sem_b,ops_b=reimport_identity(sem,exported,tag)
            check(sem_b==sem,'same-language round trip changed the semantic bytes')
            vec=vectors(fx['inputs'],rng);ref=[evaluate(ops,v) for v in vec]
            check(O.values('rust',src,fx['name'],vec)==ref,'rustc(original Rust) != reference')
            check(O.values('rust',exported,'calc',vec)==ref,'rustc(exported Rust) != reference')
            check(O.values('c',p1['source'].encode(),p1['name'],vec)==ref,'cc(F1 C) != reference')
            # Non-NORMALIZED input: the exporter emits the last result; re-import gains one pair (recorded).
            s2=strip(sem);q=rexport(semonly(s2));check(q.returncode==0,'export of stripped unit refused')
            r2=rimport(q.stdout);check(r2.returncode==0,'re-import of stripped export refused')
            sem2,_,ops2=decode(r2.stdout,'Rust');sops=[(o[2],o[8],o[9],o[10]) for o in ops_of(s2)]
            check([evaluate(ops2,v) for v in vec]==[evaluate(sops,v) for v in vec],'stripped round trip evaluation differs')
            item.update(result='PASS',ops=len(ops),vectors=len(vec),semantic_identity=identity(sem),N_identity=identity(N(sem)),exported=exported.decode(),
                        stripped_reimport_identity_equal=N(strip(sem2))==N(s2))
            bump('rustc_observations',2*len(vec));bump('cc_observations',len(vec))
        except (Fail,ValueError,KeyError,IndexError,struct.error,subprocess.TimeoutExpired) as e:
            item.update(result='FAIL',failure=str(e));failures.append(item)
        rows.append(item)

    # ---- Rust import refusals ----
    refusals=[]
    for fx in f2['refusals']:
        src=bytes.fromhex(fx['source_hex']) if 'source_hex' in fx else fx['source'].encode()
        r=rimport(src);ok=hashlib.sha256(src).hexdigest()==fx['sha256'] and r.returncode==fx['status'] and r.stdout==b'' and r.stderr.startswith(b'RUST_IMPORT_')
        it={'id':fx['id'],'name':fx['name'],'want':fx['status'],'exit':r.returncode,'stderr':r.stderr.decode(errors='replace').strip(),'result':'PASS' if ok else 'FAIL'}
        if not ok:failures.append(it)
        refusals.append(it)

    # ---- import boundary controls (exact and over capacity), each accepted one exported and re-imported ----
    head=b'pub fn calc(a: u64, b: u64) -> u64 { '
    base=head+b'a.wrapping_add(b) }'
    chain=lambda k:head+b'a'+b'.wrapping_add(a)'*k+b' }'
    right=lambda k:head+b'a.wrapping_add('*k+b'a'+b')'*k+b' }'
    parens=lambda k:head+b'('*k+b'a'+b')'*k+b' }'
    controls=[('exact-source-capacity',base+b' '*(4096-len(base)),0,12),('comment-at-capacity',base+b'//'+b'x'*(4096-len(base)-2),0,12),
              ('operation-capacity',chain(60),0,61*5),('nesting-limit',parens(16),0,5),('call-nesting-limit',right(16),0,17*5),
              ('over-source-capacity',base+b' '*(4097-len(base)),67,None),('over-operation-capacity',chain(61),67,None),
              ('over-nesting-limit',parens(17),67,None),('over-call-nesting-limit',right(17),67,None)]
    ctl=[]
    for name,src,want,value in controls:
        it={'name':name,'source_bytes':len(src)}
        try:
            r=rimport(src)
            if want:check(r.returncode==want and not r.stdout and r.stderr==b'RUST_IMPORT_LIMIT\n','%s rc=%d'%(name,r.returncode))
            else:
                check(r.returncode==0,'%s refused rc=%d %s'%(name,r.returncode,r.stderr));sem,side,ops=decode(r.stdout,'Rust');sidecar(sem,side,src,len(ops))
                check(evaluate(ops,[5,7])==value,name+' value');exported=export_checks(sem,r.stdout,name)
                sem_b,_=reimport_identity(sem,exported,name);check(sem_b==sem,name+' round trip')
                vec=vectors(2,rng);ref=[evaluate(ops,v) for v in vec]
                check(O.values('rust',src,'calc',vec)==ref and O.values('rust',exported,'calc',vec)==ref,name+' rustc differential')
                it.update(ops=len(ops),exported_bytes=len(exported))
            it.update(result='PASS',exit=r.returncode)
        except (Fail,ValueError,struct.error) as e:it.update(result='FAIL',failure=str(e));failures.append(it)
        ctl.append(it)
    r=subprocess.run([str(ROOT/'build/L626/rust-import')],stdin=subprocess.DEVNULL,capture_output=True,preexec_fn=lambda:os.close(0),timeout=10)
    ctl.append({'name':'import-closed-stdin','exit':r.returncode,'result':'PASS' if r.returncode==77 and not r.stdout and r.stderr==b'RUST_IMPORT_IO\n' else 'FAIL'})
    r=subprocess.run([str(ROOT/'build/L626/rust-export')],stdin=subprocess.DEVNULL,capture_output=True,preexec_fn=lambda:os.close(0),timeout=10)
    ctl.append({'name':'export-closed-stdin','exit':r.returncode,'result':'PASS' if r.returncode==77 and not r.stdout and r.stderr==b'RUST_EXPORT_IO\n' else 'FAIL'})
    for tool in (IMPORT,EXPORT):
        r=run([sys.executable,str(tool),'unexpected'],base);ctl.append({'name':tool.name+'-arguments-refused','exit':r.returncode,'result':'PASS' if r.returncode==64 and not r.stdout and b'BAD_COMMAND' in r.stderr else 'FAIL'})
    r=run([sys.executable,str(IMPORT)],base);d=run([str(ROOT/'build/L626/rust-import')],base)
    ctl.append({'name':'public-import-identical-to-native','result':'PASS' if r.returncode==0 and r.stdout==d.stdout else 'FAIL'})
    for tool,victims in ((IMPORT,['src/adapters/rust-u64-import.om','build/L626/rust-import']),(EXPORT,['src/adapters/rust-u64-export.om','src/source-aware/core.om','build/L626/rust-export'])):
        for victim in victims:
            with tempfile.TemporaryDirectory(prefix='closed-build-',dir=out) as td:
                t=Path(td)
                for rel in ['tools/adapters/'+tool.name,'src/source-aware/vendor/sha256.om','src/source-aware/core.om','src/adapters/rust-u64-import.om','src/adapters/rust-u64-export.om','build/L626/build.json','build/L626/rust-import','build/L626/rust-export']:
                    p=t/rel;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/rel,p);p.chmod(0o700)
                with (t/victim).open('ab') as f:f.write(b'changed')
                r=run([sys.executable,str(t/'tools/adapters'/tool.name)],base)
                ok=r.returncode==78 and not r.stdout and b'STALE_OR_MISSING_BUILD' in r.stderr
                ctl.append({'name':tool.name+'-stale-'+victim,'exit':r.returncode,'result':'PASS' if ok else 'FAIL'})
    failures+=[c for c in ctl if c['result']=='FAIL' and c not in failures]

    # ---- exporter refusals ----
    good=rimport(base).stdout;gsem,gside=split(good)
    def unit(lang,prof,runtime,abi,ops,typ=1):
        sem=b'OMSA\x01'+bytes([lang,prof,1,runtime,abi])+struct.pack('<H',len(ops))+CONTRACT
        for op,attr,eff,alias,obl,a,b,imm in ops:sem+=struct.pack('<8BHHQ',lang,1,op,typ,attr,eff,alias,obl,a,b,imm)
        return semonly(sem)
    U=65535
    def shared(k):  # t_j = add(t_{j-1}, t_{j-1}): tree size 2^k, depth k
        ops=[(9,0,0,0,1,U,U,0)]+[(2,1,0,0,1,j,j,0) for j in range(k)]
        return unit(2,2,0,1,ops)
    bigint=unit(3,3,1,1,[(9,0,0,0,1,U,U,0),(9,0,0,0,1,U,U,1),(2,2,20,255,8,0,1,0)],typ=2)
    divc=unit(1,1,0,1,[(9,0,0,0,1,U,U,0),(9,0,0,0,1,U,U,1),(5,1,0,0,3,0,1,0)])
    gap=unit(2,2,0,1,[(9,0,0,0,1,U,U,1)])
    flipped=bytearray(good);flipped[-3]^=1
    xr=[('truncated',good[:-1],65),('bad-magic',b'X'+good[1:],65),('sidecar-source-mismatch',bytes(flipped),65),('oversize',good+b'\0'*(8193-len(good)),65),
        ('profile3-bigint',bigint,68),('c-div-op5',divc,68),('slot-gap',gap,68),('language-profile-mismatch',bytes(good[:18])+b'\x01'+good[19:],68),
        ('shared-ops-over-capacity',shared(6),67),('depth-over-limit',None,67),('size-over-limit',shared(9),67)]
    deep=[(9,0,0,0,1,U,U,0)]
    for j in range(17):deep.append((2,1,0,0,1,0,j,0))
    xr[9]=('depth-over-limit',unit(2,2,0,1,deep),67)
    xrows=[]
    for name,env,want in xr:
        if name in ('profile3-bigint','c-div-op5'):check(sa('decode',env).returncode==0,name+' control is not a valid v1 unit')
        r=rexport(env);ok=r.returncode==want and not r.stdout and r.stderr.startswith(b'RUST_EXPORT_')
        it={'name':name,'want':want,'exit':r.returncode,'stderr':r.stderr.decode(errors='replace').strip(),'result':'PASS' if ok else 'FAIL'}
        if not ok:failures.append(it)
        xrows.append(it)
    # Accepted exporter boundaries: shared SSA expanded to exactly 64 re-import operations, call depth exactly 16.
    exact=[(9,0,0,0,1,U,U,0)]+[(2,1,0,0,1,j,j,0) for j in range(5)]+[(2,1,0,0,1,5,4,0),(2,1,0,0,1,6,3,0),(2,1,0,0,1,7,2,0),(2,1,0,0,1,8,1,0)]
    for name,env in (('shared-ops-at-capacity',unit(2,2,0,1,exact)),('depth-at-limit',unit(2,2,0,1,deep[:17]))):
        it={'name':name}
        try:
            sem,_=split(env);ops=[(o[2],o[8],o[9],o[10]) for o in ops_of(sem)];r=rexport(env);check(r.returncode==0,name+' refused rc=%d'%r.returncode)
            q=rimport(r.stdout);check(q.returncode==0,name+' re-import refused');_,_,ops2=decode(q.stdout,'Rust')
            check(len(ops2)==(64 if name=='shared-ops-at-capacity' else 19),name+' re-import operation count %d'%len(ops2))
            vec=vectors(1,rng);ref=[evaluate(ops,v) for v in vec]
            check([evaluate(ops2,v) for v in vec]==ref and O.values('rust',r.stdout,'calc',vec)==ref,name+' evaluation')
            it.update(result='PASS',reimport_ops=len(ops2),identity_equal=N(strip(split(q.stdout)[0]))==N(strip(sem)))
        except (Fail,ValueError,struct.error) as e:it.update(result='FAIL',failure=str(e));failures.append(it)
        xrows.append(it)

    # ---- composition C -> Rust: F1 positives and the four accepted C import boundary controls ----
    ch=b'#include <stdint.h>\nuint64_t calc(uint64_t a,uint64_t b){return a+b;}'
    cctl=[('C-exact-source-capacity',ch+b' '*(4096-len(ch)),'calc',2),('C-comment-at-capacity',ch+b'//'+b'x'*(4096-len(ch)-2),'calc',2),
          ('C-operation-capacity',b'#include <stdint.h>\nuint64_t calc(uint64_t a,uint64_t b){return '+b'+'.join([b'a']*61)+b';}','calc',2),
          ('C-nesting-limit',b'#include <stdint.h>\nuint64_t calc(uint64_t a,uint64_t b){return '+b'('*16+b'a'+b')'*16+b';}','calc',2)]
    comp=[]
    for tag,csrc,name,nargs in [(p['id'],p['source'].encode(),p['name'],p['inputs']) for p in f1['positives']]+cctl:
        it={'id':'C->Rust '+tag}
        try:
            c=run([str(cimp)],csrc);bump('c_import_runs');check(c.returncode==0,'C import refused')
            sem_a,_,ops_a=decode(c.stdout,'C')
            src_b=export_checks(sem_a,c.stdout,tag)
            sem_b,ops_b=reimport_identity(sem_a,src_b,tag)
            vec=vectors(nargs,rng);ref=[evaluate(ops_a,v) for v in vec]
            check(O.values('c',csrc,name,vec)==ref,'cc(original C) != reference(U_A)')
            check(O.values('rust',src_b,'calc',vec)==ref,'rustc(src_B) != reference(U_A)')
            check([evaluate(ops_b,v) for v in vec]==ref,'reference(U_B) != reference(U_A)')
            it.update(result='PASS',identity=identity(N(strip(sem_a))),vectors=len(vec),native_realization='NOT_RUN (lowering not on this branch)')
        except (Fail,ValueError,struct.error) as e:it.update(result='FAIL',failure=str(e));failures.append(it)
        comp.append(it)

    # ---- composition Rust -> C (only with a passing G1 C exporter) ----
    rc_rows=[];rust_to_c='NOT_RUN'
    if a.c_export:
        rust_to_c='RUN'
        for fx in f2['positives']:
            it={'id':'Rust->C '+fx['id']}
            try:
                src=fx['source'].encode();r=rimport(src);sem_a,_,ops_a=decode(r.stdout,'Rust')
                q=py(Path(a.c_export),semonly(retarget(sem_a,'C')));check(q.returncode==0,'c-export refused R(U,C) rc=%d %s'%(q.returncode,q.stderr))
                c=run([str(cimp)],q.stdout);check(c.returncode==0,'C import of exported C refused')
                sem_b,_,ops_b=decode(c.stdout,'C');check(N(strip(sem_b))==N(strip(sem_a)),'identity N(strip(U_B)) != N(strip(U_A))')
                vec=vectors(fx['inputs'],rng);ref=[evaluate(ops_a,v) for v in vec]
                check(O.values('rust',src,fx['name'],vec)==ref,'rustc(original Rust) != reference')
                check(O.values('c',q.stdout,'calc',vec)==ref,'cc(src_B) != reference')
                it.update(result='PASS',vectors=len(vec),exported_c=q.stdout.decode())
            except (Fail,ValueError,struct.error) as e:it.update(result='FAIL',failure=str(e));failures.append(it)
            rc_rows.append(it)

    report={'schema':'omega.rust-u64.selftest.v1','result':'FAIL' if failures else 'PASS','status_flags':'TESTED (author self-test), not QUALIFIED',
            'evidence':'REFERENCE + DIFFERENTIAL (rustc/cc NATIVE AArch64); no PROOF','source_commit':subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip(),
            'build':json.loads((ROOT/'build/L626/build.json').read_text()),'rustc':subprocess.check_output([a.rustc,'--version'],text=True).strip(),
            'c_import_sha256':hashlib.sha256(cimp.read_bytes()).hexdigest(),'sa_binary_sha256':hashlib.sha256((sa_root/'build/L574/sa').read_bytes()).hexdigest(),
            'positives':len(rows),'positives_passed':sum(r['result']=='PASS' for r in rows),'not_in_profile':len(f2['not_in_profile']),
            'refusals':len(refusals),'refusals_passed':sum(r['result']=='PASS' for r in refusals),
            'controls':len(ctl),'controls_passed':sum(r['result']=='PASS' for r in ctl),'export_refusals':len(xrows),'export_refusals_passed':sum(r['result']=='PASS' for r in xrows),
            'c_to_rust':len(comp),'c_to_rust_passed':sum(r['result']=='PASS' for r in comp),'rust_to_c':rust_to_c,'rust_to_c_passed':sum(r['result']=='PASS' for r in rc_rows),'rust_to_c_cases':len(rc_rows),
            'oracle_compiles':O.compiles,'counts':counts,'cases':rows,'refusal_cases':refusals,'control_cases':ctl,'export_cases':xrows,'c_to_rust_cases':comp,'rust_to_c_rows':rc_rows,'failures':failures}
    (out/'result.json').write_text(json.dumps(report,indent=1)+'\n')
    keys=['positives','positives_passed','not_in_profile','refusals','refusals_passed','controls','controls_passed','export_refusals','export_refusals_passed','c_to_rust','c_to_rust_passed','rust_to_c','rust_to_c_cases','rust_to_c_passed','oracle_compiles']
    print(report['result'],' '.join('%s=%s'%(k,report[k]) for k in keys),' '.join('%s=%s'%kv for kv in counts.items()))
    for f in failures[:20]:print('FAIL',json.dumps(f)[:600])
    return 1 if failures else 0

if __name__=='__main__':raise SystemExit(main())
