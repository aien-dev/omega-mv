#!/usr/bin/env python3
"""C++ lane (#739) author self-test: TESTED evidence only (translation-v1 section 10), never QUALIFIED.

For every F2-cpp fixture: C++ import, independent decode (v2 header, fields, sidecar binding, spans), #621 v2
codec `sa identity|decode|replay` (build/L832, NOT_RUN when absent), cross-importer identity against the C
import of the F1 source, export of the unit and of R(U, M) semantic-only for all six family members (identical
bytes), same-language re-import with identical semantic bytes, and g++ DIFFERENTIAL (original and exported
C++) plus cc on the F1 C against a bigint evaluation of the records. Compositions C->C++ and C++->C
(translation-compose-v2 section 3: the v1-core G1 C exporter gets R(U, C); with --retarget the #624 retarget tool
produces every R(U, M) and must equal the test-code R byte for byte, otherwise the test-code R is used) with identity under N and the DIFFERENTIAL check including the native realization
lower(N(U_A)) -> omc 734f0a39 -> bridge binding.
"""
from pathlib import Path
import argparse,hashlib,json,os,random,shutil,struct,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[3]
HOME=Path.home()
MASK=(1<<64)-1
U=65535
SEMDOM=b'omega.source.semantic.v1'
CONTRACT=hashlib.sha256((ROOT/'spec/universal/semantics.md').read_bytes()).digest()
# translation-compose-v2 section 1: language, profile, arithmetic obligations, OMSA version
MEMBERS={'C':(1,1,1,1),'Rust':(2,2,1,1),'Python':(3,4,5,1),'Cpp':(5,7,1,2),'Zig':(6,8,1,2),'Go':(7,9,1,2)}
BOUNDARY=[0,1,2,(1<<32)-1,1<<32,(1<<63)-1,1<<63,MASK-1,MASK]
IMPORT=ROOT/'tools/adapters/cpp-import';EXPORT=ROOT/'tools/adapters/cpp-export'
CEXPORT=ROOT/'tools/adapters/c-export';LOWER=ROOT/'tools/adapters/lower'
CXXFLAGS=['-std=c++17','-O2','-Wall','-Wextra','-Werror','-Wconversion','-Wsign-conversion','-pedantic-errors','-Wno-unused-parameter']
CFLAGS=['-std=c17','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter']
OMC_PIN='734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61'
BRIDGE_COMMIT='012860dbf26ee62a9838594b9dfd1e3e94a540a9'
NATIVE_OM_PIN='2c2c53f61d4ab302f1a2092c3c32189335dd53f6cf2b388f47a475f99f2c3940'
T='std::uint64_t'

class Fail(Exception):pass
def check(c,msg):
    if not c:raise Fail(msg)

def run(cmd,data,cwd=None,timeout=60,env=None):
    return subprocess.run(cmd,input=data,capture_output=True,timeout=timeout,cwd=cwd,env=env)

def py(tool,data,*args):return run([sys.executable,str(tool),*args],data)

# ---- independent envelope model (not the #621 codec) ----
def split(env):
    check(len(env)>=13 and env[:5]==b'OMSB\x01','bad envelope magic')
    ns,np_=struct.unpack_from('<II',env,5);check(len(env)==13+ns+np_,'bad envelope length')
    return env[13:13+ns],env[13+ns:]

def ops_of(sem):
    count=struct.unpack_from('<H',sem,10)[0];check(len(sem)==44+20*count,'bad semantic length')
    return [struct.unpack_from('<8BHHQ',sem,44+20*i) for i in range(count)]

def decode(env,member):
    lang,prof,arob,ver=MEMBERS[member]
    sem,side=split(env)
    check(sem[:4]==b'OMSA' and sem[4]==ver and sem[5]==lang and sem[6]==prof and sem[7:10]==b'\x01\x00\x01','wrong %s header'%member)
    check(sem[12:44]==CONTRACT,'wrong parent semantics hash')
    ops=[];slots=[]
    for i,(d,v,op,t,attr,eff,alias,obl,a,b,imm) in enumerate(ops_of(sem)):
        check(d==lang and v==1 and t==1 and eff==0 and alias==0,'op %d fields'%i)
        if op==9:check(attr==0 and obl==1 and a==b==U,'input op');slots.append(imm)
        elif op==1:check(attr==1 and obl==0 and a==b==U,'const op')
        else:check(op in (2,3,4) and attr==1 and obl==arob and a<i and b<i and imm==0,'arith op %d'%i)
        ops.append((op,a,b,imm))
    check(sorted(slots)==list(range(len(slots))),'input slots not a permutation')
    return sem,side,ops

def sidecar(sem,side,src,count):
    """C and C++ importers store raw bytes (OMSP encoding 0)."""
    check(side[:6]==b'OMSP\x01\x00','sidecar must be v1 encoding 0 (raw bytes)')
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
    """R(U, M) on the semantic bytes (translation-compose-v2 section 2): version, language, profile, dialects, obligations."""
    lang,prof,arob,ver=MEMBERS[member];b=bytearray(sem);b[4]=ver;b[5]=lang;b[6]=prof
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
def recs(sem):return [(o[2],o[8],o[9],o[10]) for o in ops_of(sem)]

# ---- oracles ----
class Oracles:
    def __init__(s,out,cxx,cc):s.out=out;s.cxx=cxx;s.cc=cc;s.cache={};s.compiles=0
    def build(s,kind,text,name,nargs):
        key=hashlib.sha256(kind.encode()+b'\0'+name.encode()+b'\0'+text).hexdigest()
        if key in s.cache:return s.cache[key]
        d=s.out/'oracle';d.mkdir(exist_ok=True);exe=d/key[:24]
        call=name+'('+', '.join('x[%d]'%j for j in range(nargs))+')'
        if kind=='cpp':
            f=d/(key[:24]+'.cpp')
            loop=('std::uint64_t x[6] = {};\n    while (std::scanf("'+' '.join(['%" SCNu64 "']*nargs)+'", '+', '.join('&x[%d]'%j for j in range(nargs))+') == %d)\n        '%nargs
                  if nargs else '')
            f.write_bytes(text+('\n#include <cstdio>\n#include <cinttypes>\nint main() {\n    '+loop+'std::printf("%" PRIu64 "\\n", '+call+');\n    return 0;\n}\n').encode())
            cmd=['nice','-n','19',s.cxx,*CXXFLAGS,str(f),'-o',str(exe)]
        else:
            f=d/(key[:24]+'.c')
            body=('uint64_t x[6]={0};while(scanf("'+' '.join(['%" SCNu64 "']*nargs)+'",'+','.join('&x[%d]'%j for j in range(nargs))+')==%d)'%nargs+
                  'printf("%" PRIu64 "\\n",'+call+');') if nargs else 'printf("%" PRIu64 "\\n",'+name+'());'
            f.write_bytes(text+('\n#include <stdio.h>\n#include <inttypes.h>\nint main(void){'+body+'return 0;}\n').encode())
            cmd=['nice','-n','19',s.cc,*CFLAGS,str(f),'-o',str(exe)]
        r=run(cmd,None,timeout=180);s.compiles+=1
        check(r.returncode==0,kind+' oracle compile failed: '+r.stderr.decode(errors='replace')[-800:])
        s.cache[key]=exe;return exe
    def values(s,kind,text,name,vectors):
        nargs=len(vectors[0]);exe=s.build(kind,text,name,nargs)
        r=run([str(exe)],''.join(' '.join(map(str,v))+'\n' for v in vectors).encode(),timeout=60)
        check(r.returncode==0,kind+' oracle run failed: '+r.stderr.decode(errors='replace')[-400:])
        return [int(x) for x in r.stdout.split()]

def caller_c(n):
    params=''.join(', uint64_t i%d'%i for i in range(n));args=''.join(', a[%d]'%i for i in range(n))
    return ('#include <inttypes.h>\n#include <stdio.h>\n#include <stdlib.h>\nuint32_t omega_calc(uint64_t *result%s);\n'
            'int main(void)\n{\n    char line[512];\n    while (fgets(line, sizeof line, stdin)) {\n        char *p = line;\n'
            '        uint64_t a[6] = {0};\n        for (int i = 0; i < %d; i++)\n            a[i] = strtoull(p, &p, 10);\n'
            '        (void)a;\n        (void)p;\n        uint64_t r = UINT64_C(6148914691236517205);\n        uint32_t s = omega_calc(&r%s);\n'
            '        printf("%%" PRIu32 " %%" PRIu64 "\\n", s, r);\n    }\n    return 0;\n}\n')%(params,n,args)

class Native:
    """Native realization (translation-v1 section 5): lower(N(U)) -> pinned omc function mode -> pinned bridge binding -> cc caller."""
    def __init__(s,out,omc,bridge,cc):
        s.out=out/'native';s.out.mkdir();s.omc=omc;s.cc=cc;s.cache={};s.vectors=0;s.status='NOT_RUN';s.reason=''
        try:
            check(Path(omc).is_file() and hashlib.sha256(Path(omc).read_bytes()).hexdigest()==OMC_PIN,'omc 734f0a39 pin')
            src=subprocess.run(['git','-C',bridge,'show',BRIDGE_COMMIT+':src/native.om'],capture_output=True,check=True).stdout
            check(hashlib.sha256(src).hexdigest()==NATIVE_OM_PIN,'bridge native.om pin')
            r=run([omc],src);check(r.returncode==0,'bridge native.om compile')
            s.gen=s.out/'native-gen';s.gen.write_bytes(r.stdout);s.gen.chmod(0o700);s.status='RUN'
        except (Fail,OSError,subprocess.CalledProcessError) as e:s.reason=str(e)
    def values(s,sem,vectors):
        key=identity(N(sem))
        if key not in s.cache:
            d=s.out/key[:24];d.mkdir()
            r=py(LOWER,semonly(N(sem)));check(r.returncode==0,'lower refused N(U) rc=%d %s'%(r.returncode,r.stderr))
            unit=r.stdout;(d/'unit.om').write_bytes(unit)
            r=run([s.omc],unit);check(r.returncode==0 and r.stdout,'omc function-mode compile rc=%d'%r.returncode)
            code=r.stdout
            r=run([str(s.gen)],unit+b'\0'+b'%08d'%len(code)+code);check(r.returncode==0 and r.stdout,'bridge binding rc=%d'%r.returncode)
            n=sum(1 for o in recs(sem) if o[0]==9)
            (d/'omega_native.c').write_bytes(r.stdout);(d/'caller.c').write_text(caller_c(n))
            r=run(['nice','-n','19',s.cc,'-std=gnu11','-O2','-Wall','-Wextra','-Werror','-pthread',str(d/'caller.c'),str(d/'omega_native.c'),'-o',str(d/'caller')],None,timeout=180)
            check(r.returncode==0,'cc caller: '+r.stderr.decode(errors='replace')[-400:])
            s.cache[key]=d/'caller'
        r=run([str(s.cache[key])],''.join(' '.join(map(str,v))+'\n' for v in vectors).encode())
        lines=r.stdout.decode().split('\n')[:-1];check(r.returncode==0 and len(lines)==len(vectors),'native caller run')
        out=[]
        for line in lines:
            st,val=line.split();check(st=='0','native status '+st);out.append(int(val))
        s.vectors+=len(vectors);return out

def vectors(nargs,rng):
    if nargs==0:return [()]
    if nargs<=2:base=[(x,) for x in BOUNDARY] if nargs==1 else [(x,y) for x in BOUNDARY for y in BOUNDARY]
    else:base=[tuple(BOUNDARY[(j+k)%len(BOUNDARY)] for k in range(nargs)) for j in range(len(BOUNDARY))]
    return base+[tuple(rng.getrandbits(64) for _ in range(nargs)) for _ in range(32)]

def unit(lang,prof,runtime,abi,ops,typ=1,ver=1):
    sem=b'OMSA'+bytes([ver,lang,prof,1,runtime,abi])+struct.pack('<H',len(ops))+CONTRACT
    for op,attr,eff,alias,obl,a,b,imm in ops:sem+=struct.pack('<8BHHQ',lang,1,op,typ,attr,eff,alias,obl,a,b,imm)
    return semonly(sem)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--out',required=True)
    ap.add_argument('--c-import',default=str(ROOT/'build/L589/c-import'))
    ap.add_argument('--sa-build',default='build/L832',help='v2 codec build directory under this checkout (SA_BUILD)')
    ap.add_argument('--cxx',default=os.environ.get('CXX','/usr/bin/g++'))
    ap.add_argument('--cc',default=os.environ.get('CC','cc'))
    ap.add_argument('--omc',default=os.environ.get('OMEGA_NATIVE_OMC',str(HOME/'workspace/omega-seedqual-20261025/seed/out/omc')))
    ap.add_argument('--bridge',default=os.environ.get('OMEGA_BRIDGE_REPO',str(HOME/'workspace/omega-bridge')))
    ap.add_argument('--retarget',default=os.environ.get('CPP_SELFTEST_RETARGET',''),help='public tools/adapters/retarget (#624); cross-checked against the test-code R')
    a=ap.parse_args()
    out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=False)
    cimp=Path(a.c_import)
    O=Oracles(out,a.cxx,a.cc);NAT=Native(out,a.omc,a.bridge,a.cc);rng=random.Random(739);failures=[];counts={}
    def bump(k,n=1):counts[k]=counts.get(k,0)+n
    sa_exe=ROOT/a.sa_build/'sa';codec='RUN' if sa_exe.is_file() and (ROOT/'src/source-aware/core-v2.om').is_file() else 'NOT_RUN'
    sa_env=dict(os.environ,SA_BUILD=a.sa_build)
    def sa(cmd,env):
        r=run(['sh','tools/source-aware/sa',cmd],env,cwd=ROOT,env=sa_env);bump('sa_v2_requests');return r
    def cimport(src):
        r=run([str(cimp)],src);bump('c_import_runs');return r
    def ximport(src):
        r=py(IMPORT,src);bump('cpp_import_runs');return r
    def xexport(env):
        r=py(EXPORT,env);bump('cpp_export_runs');return r
    RMEMBER={'C':'c','Rust':'rust','Python':'python','Cpp':'cpp','Zig':'zig','Go':'go'}
    def R(env,sem,m):
        """R(U, M) semantic-only: the retarget tool when given (must equal the test-code R byte for byte), else the test-code R."""
        want=semonly(retarget(sem,m))
        if not a.retarget:return want
        r=py(Path(a.retarget),env,RMEMBER[m]);bump('retarget_runs')
        check(r.returncode==0 and r.stdout==want,'retarget %s differs from the test-code R (rc=%d %s)'%(m,r.returncode,r.stderr));return r.stdout
    def native_check(sem,vec,ref):
        if NAT.status!='RUN':return 'NOT_RUN ('+NAT.reason+')'
        check(NAT.values(sem,vec)==ref,'native realization lower(N(U_A)) != reference');return 'PASS'

    f1=json.loads((ROOT/'tests/universal/translation/fixtures-f1.json').read_text())
    f2=json.loads((ROOT/'tests/universal/translation/fixtures-f2-cpp.json').read_text())
    f1pos={p['id']:p for p in f1['positives']}
    check(len(f2['positives'])+len(f2['not_in_profile'])==len(f1pos),'F2-cpp does not cover every F1 positive')
    check({p['f1_id'] for p in f2['positives']}|{p['f1_id'] for p in f2['not_in_profile']}==set(f1pos),'F2-cpp F1 id set')

    # Oracle type control: a ULL literal is not std::uint64_t on this target, the braced form and the operators are.
    ctl_src=('#include <cstdint>\n#include <type_traits>\nstatic_assert(!std::is_same_v<decltype(1ULL), std::uint64_t>, "ULL");\n'
             'static_assert(std::is_same_v<decltype(std::uint64_t{1ULL}), std::uint64_t>, "braced");\n'
             'static_assert(std::is_same_v<decltype(std::uint64_t{1ULL} * std::uint64_t{2ULL} - std::uint64_t{3ULL}), std::uint64_t>, "ops");\n'
             'static_assert(sizeof(std::uint64_t) == 8 && sizeof(unsigned long long) == 8 && sizeof(int) <= 8, "widths");\n'
             'std::uint64_t calc() { return std::uint64_t{18446744073709551615ULL} * std::uint64_t{18446744073709551615ULL}; }\n').encode()
    check(O.values('cpp',ctl_src,'calc',[()])==[1],'oracle type control');counts['oracle_type_control']='PASS'
    over=b'#include <cstdint>\nstd::uint64_t calc() { return std::uint64_t{18446744073709551616ULL}; }\n'
    r=run([a.cxx,*CXXFLAGS,'-fsyntax-only','-x','c++','-'],over);check(r.returncode!=0,'oracle accepted an out-of-range braced literal')
    counts['oracle_overflow_control']='PASS'

    def export_checks(sem,env_with_sidecar,tag):
        """Exporter: identical bytes for every family member with or without sidecar; returns src_B."""
        r=xexport(env_with_sidecar);check(r.returncode==0 and r.stderr==b'',tag+' export refused rc=%d %s'%(r.returncode,r.stderr))
        text=r.stdout
        for m in MEMBERS:
            u=R(env_with_sidecar,sem,m)
            if codec=='RUN':check(sa('decode',u).returncode==0,tag+' R(U,%s) not a valid v2-codec unit'%m)
            q=xexport(u);check(q.returncode==0 and q.stdout==text,tag+' export differs for member '+m+' (semantic-only)')
        bump('member_exports',len(MEMBERS))
        check(text.startswith(b'#include <cstdint>\nstd::uint64_t calc(') and text.endswith(b'; }\n') and text.count(b'\n')==2,tag+' export form')
        check(b'ULL' not in text.replace(b'ULL}',b''),tag+' unbraced literal in export')
        return text

    def reimport_identity(sem_a,src_b,tag):
        r=ximport(src_b);check(r.returncode==0,tag+' re-import refused rc=%d %s'%(r.returncode,r.stderr))
        sem_b,side_b,ops_b=decode(r.stdout,'Cpp');sidecar(sem_b,side_b,src_b,len(ops_b))
        check(N(strip(sem_b))==N(strip(sem_a)),tag+' identity N(strip(U_B)) != N(strip(U_A))')
        return sem_b,ops_b

    def codec_checks(env,sem,src):
        if codec!='RUN':bump('codec_not_run');return 'NOT_RUN'
        ident=sa('identity',env);check(ident.returncode==0 and ident.stdout.hex()==identity(sem),'sa v2 identity refused or differs')
        check(sa('decode',env).stdout==env,'sa v2 decode changed unit');check(sa('replay',env).stdout==src,'sa v2 replay differs from source')
        lg=sa('legality',b'\x02\x00\x01\x00'+env);obl=[o[7] for o in ops_of(sem)]
        check(lg.returncode==0 and lg.stdout==bytes(x for ob in obl for x in (3,ob)),'sa v2 legality is not LOWERABLE_WITH_OBLIGATIONS per op');bump('codec_accepted');return 'PASS'

    rows=[]
    for fx in f2['positives']:
        tag=fx['id'];item={'id':tag}
        try:
            src=fx['source'].encode();check(hashlib.sha256(src).hexdigest()==fx['sha256'],'fixture sha')
            p1=f1pos[fx['f1_id']];check(p1['sha256']==fx['f1_sha256'] and p1['inputs']==fx['inputs'] and p1['name']==fx['name'],'F1 link')
            r=ximport(src);check(r.returncode==0 and r.stderr==b'','import refused rc=%d %s'%(r.returncode,r.stderr))
            env=r.stdout;sem,side,ops=decode(env,'Cpp');spans=sidecar(sem,side,src,len(ops))
            check(normalized(ops),'importer must append the normalization pair')
            check(spans[-1]==spans[-2]==(0,0),'synthetic pair spans')
            for (op,_,_,imm),(lo,hi) in zip(ops[:-2],spans[:-2]):
                t=src[lo:hi]
                if op==1:check(t.startswith(b'std::uint64_t{') and t.endswith(b'}') and b'%dULL'%imm in t,'literal span')
                elif op==9:check(len(t)>0 and t.isascii() and t.decode().isidentifier(),'input span')
                else:check(t==b'+-*'[op-2:op-1],'operator span')
            item['codec']=codec_checks(env,sem,src)
            c=cimport(p1['source'].encode());check(c.returncode==0,'C import of F1 refused')
            csem,_,_=decode(c.stdout,'C')
            check(N(sem)==N(csem),'cross-importer identity: N(import_Cpp(F2)) != N(import_C(F1))')
            exported=export_checks(sem,env,tag)
            sem_b,ops_b=reimport_identity(sem,exported,tag)
            check(sem_b==sem,'same-language round trip changed the semantic bytes')
            vec=vectors(fx['inputs'],rng);ref=[evaluate(ops,v) for v in vec]
            check(O.values('cpp',src,fx['name'],vec)==ref,'g++(original C++) != reference')
            check(O.values('cpp',exported,'calc',vec)==ref,'g++(exported C++) != reference')
            check(O.values('c',p1['source'].encode(),p1['name'],vec)==ref,'cc(F1 C) != reference')
            # Non-NORMALIZED input: the exporter emits the last result; re-import gains one pair (recorded).
            s2=strip(sem);q=xexport(semonly(s2));check(q.returncode==0,'export of stripped unit refused')
            r2=ximport(q.stdout);check(r2.returncode==0,'re-import of stripped export refused')
            sem2,_,ops2=decode(r2.stdout,'Cpp')
            check([evaluate(ops2,v) for v in vec]==[evaluate(recs(s2),v) for v in vec],'stripped round trip evaluation differs')
            item.update(result='PASS',ops=len(ops),vectors=len(vec),semantic_identity=identity(sem),N_identity=identity(N(sem)),exported=exported.decode(),
                        stripped_reimport_identity_equal=N(strip(sem2))==N(s2))
            bump('gxx_observations',2*len(vec));bump('cc_observations',len(vec))
        except (Fail,ValueError,KeyError,IndexError,struct.error,subprocess.TimeoutExpired) as e:
            item.update(result='FAIL',failure=str(e));failures.append(item)
        rows.append(item)

    # ---- C++ import refusals ----
    refusals=[]
    for fx in f2['refusals']:
        src=bytes.fromhex(fx['source_hex']) if 'source_hex' in fx else fx['source'].encode()
        r=ximport(src);ok=hashlib.sha256(src).hexdigest()==fx['sha256'] and r.returncode==fx['status'] and r.stdout==b'' and r.stderr.startswith(b'CPP_IMPORT_')
        it={'id':fx['id'],'name':fx['name'],'want':fx['status'],'exit':r.returncode,'stderr':r.stderr.decode(errors='replace').strip(),'result':'PASS' if ok else 'FAIL'}
        if not ok:failures.append(it)
        refusals.append(it)

    # ---- import boundary controls (exact and over capacity); each accepted one exported, re-imported and compiled ----
    head=('#include <cstdint>\n'+T+' calc('+T+' a, '+T+' b) { return ').encode()
    base=head+b'a + b; }'
    chain=lambda k:head+b'a'+b' + a'*k+b'; }'
    parens=lambda k:head+b'('*k+b'a'+b')'*k+b'; }'
    lits=lambda k:head+b' + '.join([b'std::uint64_t{18446744073709551615ULL}']*k)+b'; }'
    controls=[('exact-source-capacity',base+b' '*(4096-len(base)),0,12),('comment-at-capacity',base+b'//'+b'x'*(4096-len(base)-2),0,12),
              ('operation-capacity',chain(60),0,61*5),('nesting-limit',parens(16),0,5),('literal-capacity',lits(30),0,(30*MASK)&MASK),
              ('over-source-capacity',base+b' '*(4097-len(base)),67,None),('over-operation-capacity',chain(61),67,None),
              ('over-nesting-limit',parens(17),67,None),('over-literal-capacity',lits(31),67,None)]
    ctl=[]
    for name,src,want,value in controls:
        it={'name':name,'source_bytes':len(src)}
        try:
            r=ximport(src)
            if want:check(r.returncode==want and not r.stdout and r.stderr==b'CPP_IMPORT_LIMIT\n','%s rc=%d'%(name,r.returncode))
            else:
                check(r.returncode==0,'%s refused rc=%d %s'%(name,r.returncode,r.stderr));sem,side,ops=decode(r.stdout,'Cpp');sidecar(sem,side,src,len(ops))
                check(evaluate(ops,[5,7])==value,name+' value');exported=export_checks(sem,r.stdout,name)
                sem_b,_=reimport_identity(sem,exported,name);check(sem_b==sem,name+' round trip')
                it['codec']=codec_checks(r.stdout,sem,src)
                vec=vectors(2,rng);ref=[evaluate(ops,v) for v in vec]
                check(O.values('cpp',src,'calc',vec)==ref and O.values('cpp',exported,'calc',vec)==ref,name+' g++ differential')
                it.update(ops=len(ops),exported_bytes=len(exported))
            it.update(result='PASS',exit=r.returncode)
        except (Fail,ValueError,struct.error) as e:it.update(result='FAIL',failure=str(e));failures.append(it)
        ctl.append(it)
    for nm in ('cpp-import','cpp-export'):
        r=subprocess.run([str(ROOT/'build/L739'/nm)],stdin=subprocess.DEVNULL,capture_output=True,preexec_fn=lambda:os.close(0),timeout=10)
        want=b'CPP_IMPORT_IO\n' if nm=='cpp-import' else b'CPP_EXPORT_IO\n'
        ctl.append({'name':nm+'-closed-stdin','exit':r.returncode,'result':'PASS' if r.returncode==77 and not r.stdout and r.stderr==want else 'FAIL'})
    for tool in (IMPORT,EXPORT):
        r=py(tool,base,'unexpected');ctl.append({'name':tool.name+'-arguments-refused','exit':r.returncode,'result':'PASS' if r.returncode==64 and not r.stdout and b'BAD_COMMAND' in r.stderr else 'FAIL'})
    r=py(IMPORT,base);d=run([str(ROOT/'build/L739/cpp-import')],base)
    ctl.append({'name':'public-import-identical-to-native','result':'PASS' if r.returncode==0 and r.stdout==d.stdout else 'FAIL'})
    good=r.stdout
    e1=py(EXPORT,good);e2=run([str(ROOT/'build/L739/cpp-export')],good)
    ctl.append({'name':'public-export-identical-to-native','result':'PASS' if e1.returncode==0 and e1.stdout==e2.stdout else 'FAIL'})
    files=['src/source-aware/vendor/sha256.om','src/source-aware/core-v2.om','src/adapters/cpp-u64-import.om','src/adapters/cpp-u64-export.om','build/L739/build.json','build/L739/cpp-import','build/L739/cpp-export']
    for tool,victims,data in ((IMPORT,['src/source-aware/vendor/sha256.om','src/adapters/cpp-u64-import.om','build/L739/cpp-import','build/L739/build.json'],base),
                              (EXPORT,['src/source-aware/core-v2.om','src/adapters/cpp-u64-export.om','build/L739/cpp-export'],good)):
        for victim in victims:
            with tempfile.TemporaryDirectory(prefix='closed-build-',dir=out) as td:
                t=Path(td)
                for rel in ['tools/adapters/'+tool.name,*files]:
                    p=t/rel;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/rel,p);p.chmod(0o700)
                with (t/victim).open('ab') as f:f.write(b'changed')
                r=py(t/'tools/adapters'/tool.name,data)
                ok=r.returncode==78 and not r.stdout and b'STALE_OR_MISSING_BUILD' in r.stderr
                ctl.append({'name':tool.name+'-stale-'+victim,'exit':r.returncode,'result':'PASS' if ok else 'FAIL'})
    failures+=[c for c in ctl if c['result']=='FAIL' and c not in failures]

    # ---- exporter refusals ----
    gsem,gside=split(good)
    def shared(k):  # t_j = add(t_{j-1}, t_{j-1}): tree size 2^k, depth k
        return unit(5,7,0,1,[(9,0,0,0,1,U,U,0)]+[(2,1,0,0,1,j,j,0) for j in range(k)],ver=2)
    deep=[(9,0,0,0,1,U,U,0)]+[(2,1,0,0,1,0,j,0) for j in range(18)]
    flipped=bytearray(good);flipped[-3]^=1
    v3=bytearray(good);v3[17]=3
    xr=[('truncated',good[:-1],65),('bad-magic',b'X'+good[1:],65),('sidecar-source-mismatch',bytes(flipped),65),('oversize',good+b'\0'*(8193-len(good)),65),
        ('unknown-semantic-version-3',bytes(v3),65),
        ('profile3-bigint',unit(3,3,1,1,[(9,0,0,0,1,U,U,0),(9,0,0,0,1,U,U,1),(2,2,20,255,8,0,1,0)],typ=2),68),
        ('cpp-div-op5',unit(5,7,0,1,[(9,0,0,0,1,U,U,0),(9,0,0,0,1,U,U,1),(5,1,0,0,3,0,1,0)],ver=2),68),
        ('go-div-op5',unit(7,9,0,1,[(9,0,0,0,1,U,U,0),(9,0,0,0,1,U,U,1),(5,1,0,0,3,0,1,0)],ver=2),68),
        ('c-div-op5',unit(1,1,0,1,[(9,0,0,0,1,U,U,0),(9,0,0,0,1,U,U,1),(5,1,0,0,3,0,1,0)]),68),
        ('slot-gap',unit(5,7,0,1,[(9,0,0,0,1,U,U,1)],ver=2),68),
        ('v1-header-profile7',unit(5,7,0,1,[(9,0,0,0,1,U,U,0)],ver=1),68),
        ('v2-header-profile1',unit(1,1,0,1,[(9,0,0,0,1,U,U,0)],ver=2),68),
        ('language-profile-mismatch',semonly(bytes(gsem[:6])+b'\x08'+gsem[7:]),68),
        ('cpp-obligation-5-decoder-72',unit(5,7,0,1,[(9,0,0,0,1,U,U,0),(2,1,0,0,5,0,0,0)],ver=2),65),
        ('shared-ops-over-capacity',shared(6),67),('depth-over-limit',unit(5,7,0,1,deep,ver=2),67),('size-over-limit',shared(9),67)]
    xrows=[]
    for name,env,want in xr:
        it={'name':name,'want':want}
        if name in ('profile3-bigint','cpp-div-op5','go-div-op5','c-div-op5','slot-gap','shared-ops-over-capacity','depth-over-limit','size-over-limit'):
            it['codec_decode']=('PASS' if sa('decode',env).returncode==0 else 'FAIL') if codec=='RUN' else 'NOT_RUN'
        r=xexport(env);ok=r.returncode==want and not r.stdout and r.stderr.startswith(b'CPP_EXPORT_') and it.get('codec_decode','PASS')!='FAIL'
        it.update(exit=r.returncode,stderr=r.stderr.decode(errors='replace').strip(),result='PASS' if ok else 'FAIL')
        if not ok:failures.append(it)
        xrows.append(it)
    # Accepted exporter boundaries: shared SSA expanded to exactly 64 re-import operations; a right-nested chain of 17 adds
    # exports with parenthesis depth exactly 16 (18 adds: depth 17, refused above).
    exact=[(9,0,0,0,1,U,U,0)]+[(2,1,0,0,1,j,j,0) for j in range(5)]+[(2,1,0,0,1,5,4,0),(2,1,0,0,1,6,3,0),(2,1,0,0,1,7,2,0),(2,1,0,0,1,8,1,0)]
    for name,env,want_ops in (('shared-ops-at-capacity',unit(5,7,0,1,exact,ver=2),64),('depth-at-limit',unit(5,7,0,1,deep[:18],ver=2),20),
                              ('zig-member-depth-at-limit',unit(6,8,0,1,deep[:18],ver=2),20),('python-member-shared',unit(3,4,0,1,[(9,0,0,0,1,U,U,0)]+[(2,1,0,0,5,j,j,0) for j in range(4)]),18)):
        it={'name':name}
        try:
            sem,_=split(env);ops=recs(sem);r=xexport(env);check(r.returncode==0,name+' refused rc=%d %s'%(r.returncode,r.stderr))
            q=ximport(r.stdout);check(q.returncode==0,name+' re-import refused');_,_,ops2=decode(q.stdout,'Cpp')
            check(len(ops2)==want_ops,name+' re-import operation count %d'%len(ops2))
            vec=vectors(1,rng);ref=[evaluate(ops,v) for v in vec]
            check([evaluate(ops2,v) for v in vec]==ref and O.values('cpp',r.stdout,'calc',vec)==ref,name+' evaluation')
            it.update(result='PASS',reimport_ops=len(ops2),identity_equal=N(strip(split(q.stdout)[0]))==N(strip(sem)))
        except (Fail,ValueError,struct.error) as e:it.update(result='FAIL',failure=str(e));failures.append(it)
        xrows.append(it)

    # ---- composition C -> C++: F1 positives and the four accepted C import boundary controls ----
    ch=b'#include <stdint.h>\nuint64_t calc(uint64_t a,uint64_t b){return a+b;}'
    cctl=[('C-exact-source-capacity',ch+b' '*(4096-len(ch)),'calc',2),('C-comment-at-capacity',ch+b'//'+b'x'*(4096-len(ch)-2),'calc',2),
          ('C-operation-capacity',b'#include <stdint.h>\nuint64_t calc(uint64_t a,uint64_t b){return '+b'+'.join([b'a']*61)+b';}','calc',2),
          ('C-nesting-limit',b'#include <stdint.h>\nuint64_t calc(uint64_t a,uint64_t b){return '+b'('*16+b'a'+b')'*16+b';}','calc',2)]
    comp=[]
    for tag,csrc,name,nargs in [(p['id'],p['source'].encode(),p['name'],p['inputs']) for p in f1['positives']]+cctl:
        it={'id':'C->C++ '+tag}
        try:
            c=cimport(csrc);check(c.returncode==0,'C import refused')
            sem_a,_,ops_a=decode(c.stdout,'C')
            src_b=export_checks(sem_a,c.stdout,tag)
            sem_b,ops_b=reimport_identity(sem_a,src_b,tag)
            vec=vectors(nargs,rng);ref=[evaluate(ops_a,v) for v in vec]
            check(O.values('c',csrc,name,vec)==ref,'cc(original C) != reference(U_A)')
            check(O.values('cpp',src_b,'calc',vec)==ref,'g++(src_B) != reference(U_A)')
            check([evaluate(ops_b,v) for v in vec]==ref,'reference(U_B) != reference(U_A)')
            it.update(native=native_check(sem_a,vec,ref))
            it.update(result='PASS',identity=identity(N(strip(sem_a))),vectors=len(vec))
        except (Fail,ValueError,struct.error) as e:it.update(result='FAIL',failure=str(e));failures.append(it)
        comp.append(it)

    # ---- composition C++ -> C: export_C(R(U_A, C)) with the v1-core G1 C exporter (translation-compose-v2 section 3) ----
    rc_rows=[]
    ctl_cpp=[(c['name'],src) for (c,(name,src,want,_)) in zip(ctl,controls) if want==0]
    for tag,src,nargs,orig_name in [(fx['id'],fx['source'].encode(),fx['inputs'],fx['name']) for fx in f2['positives']]+[('cpp-'+n,s,2,'calc') for n,s in ctl_cpp]:
        it={'id':'C++->C '+tag}
        try:
            r=ximport(src);check(r.returncode==0,'C++ import refused');sem_a,_,ops_a=decode(r.stdout,'Cpp')
            direct=py(CEXPORT,r.stdout);check(direct.returncode!=0 and not direct.stdout,'v1-core c-export accepted a v2 unit');bump('c_export_v2_refusal_status_%d'%direct.returncode)
            q=py(CEXPORT,R(r.stdout,sem_a,'C'));check(q.returncode==0,'c-export refused R(U,C) rc=%d %s'%(q.returncode,q.stderr))
            c=cimport(q.stdout);check(c.returncode==0,'C import of exported C refused')
            sem_b,_,ops_b=decode(c.stdout,'C');check(N(strip(sem_b))==N(strip(sem_a)),'identity N(strip(U_B)) != N(strip(U_A))')
            vec=vectors(nargs,rng);ref=[evaluate(ops_a,v) for v in vec]
            check(O.values('cpp',src,orig_name,vec)==ref,'g++(original C++) != reference(U_A)')
            check(O.values('c',q.stdout,'calc',vec)==ref,'cc(src_B) != reference(U_A)')
            check([evaluate(ops_b,v) for v in vec]==ref,'reference(U_B) != reference(U_A)')
            it.update(native=native_check(sem_a,vec,ref))
            it.update(result='PASS',vectors=len(vec),exported_c=q.stdout.decode()[:400])
        except (Fail,ValueError,struct.error) as e:it.update(result='FAIL',failure=str(e));failures.append(it)
        rc_rows.append(it)

    def sha(p):
        p=Path(p);return hashlib.sha256(p.read_bytes()).hexdigest() if p.is_file() else None
    nat_pass=sum(1 for r in comp+rc_rows if r.get('native')=='PASS')
    report={'schema':'omega.cpp-u64.selftest.v1','result':'FAIL' if failures else 'PASS','status_flags':'TESTED (author self-test), not QUALIFIED',
            'evidence':'REFERENCE + DIFFERENTIAL (g++/cc and the native realization, NATIVE AArch64); no PROOF','source_commit':subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip(),
            'build':json.loads((ROOT/'build/L739/build.json').read_text()),'gxx':subprocess.check_output([a.cxx,'--version'],text=True).splitlines()[0],'gxx_flags':CXXFLAGS,
            'cc':subprocess.check_output([a.cc,'--version'],text=True).splitlines()[0],
            'c_import_sha256':sha(cimp),'c_export_sha256':sha(ROOT/'build/L622/c-export'),'lower_sha256':sha(ROOT/'build/L831/lower'),
            'retarget':('TOOL %s (cross-checked against test-code R)'%a.retarget) if a.retarget else 'test-code R only (retarget tool not given)',
            'retarget_build':json.loads((Path(a.retarget).resolve().parents[2]/'build/L833/build.json').read_text()) if a.retarget else None,
            'codec_v2':codec,'codec_v2_sa_sha256':sha(sa_exe),'codec_v2_source_sha256':sha(ROOT/'src/source-aware/core-v2.om'),
            'native_realization':NAT.status,'native_realization_reason':NAT.reason,'native_realized_units':len(NAT.cache),'native_vectors':NAT.vectors,
            'positives':len(rows),'positives_passed':sum(r['result']=='PASS' for r in rows),'not_in_profile':len(f2['not_in_profile']),
            'refusals':len(refusals),'refusals_passed':sum(r['result']=='PASS' for r in refusals),
            'controls':len(ctl),'controls_passed':sum(r['result']=='PASS' for r in ctl),'export_cases':len(xrows),'export_cases_passed':sum(r['result']=='PASS' for r in xrows),
            'c_to_cpp':len(comp),'c_to_cpp_passed':sum(r['result']=='PASS' for r in comp),'cpp_to_c':len(rc_rows),'cpp_to_c_passed':sum(r['result']=='PASS' for r in rc_rows),
            'native_checks_passed':nat_pass,'oracle_compiles':O.compiles,'counts':counts,
            'cases':rows,'refusal_cases':refusals,'control_cases':ctl,'export_rows':xrows,'c_to_cpp_cases':comp,'cpp_to_c_cases':rc_rows,'failures':failures}
    (out/'result.json').write_text(json.dumps(report,indent=1)+'\n')
    keys=['positives','positives_passed','not_in_profile','refusals','refusals_passed','controls','controls_passed','export_cases','export_cases_passed',
          'c_to_cpp','c_to_cpp_passed','cpp_to_c','cpp_to_c_passed','native_realization','native_checks_passed','native_vectors','codec_v2','retarget','oracle_compiles']
    print(report['result'],' '.join('%s=%s'%(k,report[k]) for k in keys),' '.join('%s=%s'%kv for kv in counts.items()))
    for f in failures[:20]:print('FAIL',json.dumps(f)[:600])
    return 1 if failures else 0

if __name__=='__main__':raise SystemExit(main())
