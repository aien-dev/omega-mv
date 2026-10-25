#!/usr/bin/env python3
"""Zig lane (#740) author self-test: TESTED evidence only (translation-v1 section 10), never QUALIFIED.

For every F2-zig fixture: Zig import, independent decode of the v2 unit, v2 codec acceptance (`sa` from a
build/L832 tree, else NOT_RUN), cross-importer identity against the C import of the F1 source, export of
the unit and of R(U, M) for all six family members (semantic-only, identical bytes), re-import identity
(translation-compose-v1 section 4.1), and Zig 0.17 DIFFERENTIAL in Debug and ReleaseSafe (original and
exported Zig) against a bigint evaluation of the records. Compositions C->Zig and Zig->C (R(U, C) by
tools/adapters/retarget when present, cross-checked against the test-code R; translation-compose-v2 section 2), each with the native
realization lower(N(U_A)) when the G1 lowering tools are present.
"""
from pathlib import Path
import argparse,hashlib,json,os,random,re,shutil,struct,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[3]
HOME=Path.home()
MASK=(1<<64)-1
SEMDOM=b'omega.source.semantic.v1'
CONTRACT=hashlib.sha256((ROOT/'spec/universal/semantics.md').read_bytes()).digest()
# translation-compose-v2 section 1: OMSA version, language (= op dialect), profile, arithmetic obligations.
MEMBERS={'C':(1,1,1,1),'Rust':(1,2,2,1),'Python':(1,3,4,5),'Cpp':(2,5,7,1),'Zig':(2,6,8,1),'Go':(2,7,9,1)}
BOUNDARY=[0,1,2,(1<<32)-1,1<<32,(1<<63)-1,1<<63,MASK-1,MASK]
IMPORT=ROOT/'tools/adapters/zig-import';EXPORT=ROOT/'tools/adapters/zig-export'
OMC_PIN='734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61'
BRIDGE_COMMIT='012860dbf26ee62a9838594b9dfd1e3e94a540a9'
NATIVE_OM_PIN='2c2c53f61d4ab302f1a2092c3c32189335dd53f6cf2b388f47a475f99f2c3940'
ZIG_MODES=('Debug','ReleaseSafe')
PLAIN=re.compile(rb'[+\-*](?!%)')

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

def records(sem):return [(o[2],o[8],o[9],o[10]) for o in ops_of(sem)]

def decode(env,member):
    ver,lang,prof,arob=MEMBERS[member]
    sem,side=split(env)
    check(sem[:4]==b'OMSA' and sem[4]==ver and sem[5]==lang and sem[6]==prof and sem[7:10]==b'\x01\x00\x01','wrong %s header'%member)
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
    """R(U, M), translation-compose-v2 section 2 (test-code stand-in for tools/adapters/retarget):
    OMSA version, header language/profile, op dialect and arithmetic obligations; sidecar dropped by the caller."""
    ver,lang,prof,arob=MEMBERS[member];b=bytearray(sem);b[4]=ver;b[5]=lang;b[6]=prof
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

def vectors(nargs,rng):
    if nargs==0:return [()]
    if nargs<=2:base=[(x,) for x in BOUNDARY] if nargs==1 else [(x,y) for x in BOUNDARY for y in BOUNDARY]
    else:base=[tuple(BOUNDARY[(j+k)%len(BOUNDARY)] for k in range(nargs)) for j in range(len(BOUNDARY))]
    return base+[tuple(rng.getrandbits(64) for _ in range(nargs)) for _ in range(32)]

# ---- Zig oracle: every requested program is one module of a single harness per build mode ----
ZHEAD=('extern "c" fn scanf(fmt: [*:0]const u8, ...) c_int;\nextern "c" fn printf(fmt: [*:0]const u8, ...) c_int;\n'
       'pub fn main() void {\n    var id: u64 = 0;\n    var x: [6]u64 = .{ 0, 0, 0, 0, 0, 0 };\n'
       '    while (scanf("%llu %llu %llu %llu %llu %llu %llu", &id, &x[0], &x[1], &x[2], &x[3], &x[4], &x[5]) == 7) {\n'
       '        const r: u64 = switch (id) {\n')
ZTAIL='            else => unreachable,\n        };\n        _ = printf("%llu\\n", r);\n    }\n}\n'
class Zig:
    def __init__(s,out,zig):
        s.out=out/'zig';s.out.mkdir();s.zig=zig;s.items=[];s.compiles=0;s.observations=0
        s.env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(s.out/'cache'),ZIG_LOCAL_CACHE_DIR=str(s.out/'cache'))
    def add(s,tag,text,name,vecs,ref):s.items.append((tag,text,name,vecs,ref))
    def build(s,d,mods,mode):
        """mods: list of (text,name,nargs); returns (ok, stderr, exe)."""
        d.mkdir(parents=True,exist_ok=True);main=[]
        for j,(text,name,nargs) in enumerate(mods):
            (d/('m%d.zig'%j)).write_bytes(text);main.append('const m%d = @import("m%d.zig");\n'%(j,j))
        body=''.join('            %d => m%d.%s(%s),\n'%(j,j,name,', '.join('x[%d]'%k for k in range(nargs))) for j,(_,name,nargs) in enumerate(mods))
        (d/'main.zig').write_text(''.join(main)+ZHEAD+body+ZTAIL)
        r=run(['nice','-n','19',s.zig,'build-exe','-O',mode,'-lc','main.zig','-femit-bin=main'],None,cwd=d,timeout=900,env=s.env);s.compiles+=1
        return r.returncode==0,r.stderr.decode(errors='replace'),d/'main'
    def execute(s,exe,lines):
        r=run([str(exe)],''.join(lines).encode(),timeout=300)
        return r.returncode,[int(x) for x in r.stdout.split()],r.stderr.decode(errors='replace')
    def evaluate_all(s):
        """Returns {tag: failure message} over every item and both modes."""
        failures={}
        keys={};mods=[]
        for tag,text,name,vecs,ref in s.items:
            k=(text,name,len(vecs[0]))
            if k not in keys:keys[k]=len(mods);mods.append(k)
        for mode in ZIG_MODES:
            ok,err,exe=s.build(s.out/('batch-'+mode),mods,mode)
            if not ok:
                bad=set()
                for j,m in enumerate(mods):
                    o,e,_=s.build(s.out/('single-%s-%d'%(mode,j)),[m],mode)
                    if not o:bad.add(j);failures.setdefault('module%d'%j,'zig %s compile: %s'%(mode,e[-600:]))
                for tag,text,name,vecs,ref in s.items:
                    if keys[(text,name,len(vecs[0]))] in bad:failures.setdefault(tag,'zig %s compile failed: %s'%(mode,failures['module%d'%keys[(text,name,len(vecs[0]))]]))
                keep=[m for j,m in enumerate(mods) if j not in bad]
                if not keep:continue
                ok,err,exe=s.build(s.out/('batch2-'+mode),keep,mode)
                check(ok,'zig batch rebuild failed: '+err[-600:]);idx={m:j for j,m in enumerate(keep)}
            else:idx={m:j for j,m in enumerate(mods)}
            lines=[];spans=[]
            for tag,text,name,vecs,ref in s.items:
                m=(text,name,len(vecs[0]))
                if m not in idx:continue
                spans.append((tag,len(lines),len(vecs),ref,m))
                for v in vecs:lines.append(' '.join(map(str,(idx[m],*v,*([0]*(6-len(v))))))+'\n')
            rc,got,err=s.execute(exe,lines)
            if rc!=0 or len(got)!=len(lines):
                # Attribute: run each item alone on the same binary.
                for tag,start,count,ref,m in spans:
                    r2,g2,e2=s.execute(exe,lines[start:start+count])
                    if r2!=0 or g2!=ref:failures.setdefault(tag,'zig %s run rc=%d %s'%(mode,r2,e2[-300:]))
                continue
            s.observations+=len(got)
            for tag,start,count,ref,m in spans:
                if got[start:start+count]!=ref:failures.setdefault(tag,'zig %s result != reference'%mode)
        return failures

class COracle:
    def __init__(s,out,cc):s.out=out/'c';s.out.mkdir();s.cc=cc;s.cache={};s.compiles=0;s.observations=0
    def values(s,text,name,vecs):
        nargs=len(vecs[0]);key=hashlib.sha256(name.encode()+b'\0'+text).hexdigest()
        if key not in s.cache:
            f=s.out/(key[:24]+'.c');exe=s.out/key[:24]
            body=('uint64_t x[6]={0};while(scanf("'+' '.join(['%" SCNu64 "']*nargs)+'",'+','.join('&x[%d]'%j for j in range(nargs))+')==%d)'%nargs+
                  'printf("%" PRIu64 "\\n",'+name+'('+','.join('x[%d]'%j for j in range(nargs))+'));') if nargs else 'printf("%" PRIu64 "\\n",'+name+'());'
            f.write_bytes(text+('\n#include <stdio.h>\n#include <inttypes.h>\nint main(void){'+body+'return 0;}\n').encode())
            r=run(['nice','-n','19',s.cc,'-std=c17','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter',str(f),'-o',str(exe)],None,timeout=180);s.compiles+=1
            check(r.returncode==0,'cc compile failed: '+r.stderr.decode(errors='replace')[-600:]);s.cache[key]=exe
        r=run([str(s.cache[key])],''.join(' '.join(map(str,v))+'\n' for v in vecs).encode())
        check(r.returncode==0,'cc program failed');s.observations+=len(vecs)
        return [int(x) for x in r.stdout.split()]

# ---- native realization (translation-v1 section 5): lower(N(U)) -> omc 734f0a39 -> bridge binding -> caller ----
CALLER='''#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
uint32_t omega_calc(uint64_t *result%s);
int main(void)
{
    char line[512];
    while (fgets(line, sizeof line, stdin)) {
        char *p = line;
        uint64_t a[6] = {0};
        for (int i = 0; i < 6; i++)
            a[i] = strtoull(p, &p, 10);
        (void)a;
        uint64_t r = UINT64_C(6148914691236517205);
        uint32_t s = omega_calc(&r%s);
        printf("%%" PRIu32 " %%" PRIu64 "\\n", s, r);
    }
    return 0;
}
'''
class Native:
    def __init__(s,out,lower,omc,bridge,cc):
        s.state='NOT_RUN';s.reason='';s.cache={};s.calls=0;s.cc=cc;s.lower=Path(lower)
        try:
            check(s.lower.is_file(),'lowering tool missing: '+lower)
            check(Path(omc).is_file() and hashlib.sha256(Path(omc).read_bytes()).hexdigest()==OMC_PIN,'native omc missing or not 734f0a39')
            src=run(['git','-C',bridge,'show',BRIDGE_COMMIT+':src/native.om'],None).stdout
            check(hashlib.sha256(src).hexdigest()==NATIVE_OM_PIN,'bridge native.om pin')
            r=run([omc],src);check(r.returncode==0,'bridge generator compile')
            s.dir=out/'native';s.dir.mkdir();s.gen=s.dir/'native';s.gen.write_bytes(r.stdout);s.gen.chmod(0o700);s.omc=omc;s.state='RUN'
        except (Fail,OSError,subprocess.SubprocessError) as e:s.reason=str(e)
    def values(s,nsem,nargs,vecs):
        key=identity(nsem)
        if key not in s.cache:
            d=s.dir/key[:24];d.mkdir()
            r=py(s.lower,semonly(nsem));check(r.returncode==0,'lower refused N(U) rc=%d %s'%(r.returncode,r.stderr))
            unit=r.stdout;c=run([s.omc],unit);check(c.returncode==0 and c.stdout,'omc function-mode compile failed')
            b=run([str(s.gen)],unit+b'\0'+b'%08d'%len(c.stdout)+c.stdout);check(b.returncode==0 and b.stdout,'bridge binding failed')
            (d/'omega_native.c').write_bytes(b.stdout)
            (d/'caller.c').write_text(CALLER%(''.join(', uint64_t i%d'%i for i in range(nargs)),''.join(', a[%d]'%i for i in range(nargs))))
            k=run([s.cc,'-std=gnu11','-O2','-Wall','-Wextra','-Werror','-pthread',str(d/'caller.c'),str(d/'omega_native.c'),'-o',str(d/'caller')],None,timeout=180)
            check(k.returncode==0,'native caller compile: '+k.stderr.decode(errors='replace')[-400:]);s.cache[key]=d/'caller'
        r=run([str(s.cache[key])],''.join(' '.join(map(str,v))+'\n' for v in vecs).encode())
        lines=r.stdout.decode().split('\n')[:-1];check(r.returncode==0 and len(lines)==len(vecs),'native caller run')
        out=[]
        for line in lines:
            st,val=line.split();check(st=='0','native status '+st);out.append(int(val))
        s.calls+=len(vecs);return out

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--out',required=True)
    ap.add_argument('--zig',default=str(HOME/'.local/toolchains/zig/zig'))
    ap.add_argument('--cc',default=os.environ.get('CC','cc'))
    ap.add_argument('--c-import',default=str(HOME/'workspace/universal-read/build/L589/c-import'))
    ap.add_argument('--c-export',default=os.environ.get('ZIG_SELFTEST_C_EXPORT',str(HOME/'workspace/u624-int/tools/adapters/c-export')),help='public tools/adapters/c-export of a passing G1 checkout')
    ap.add_argument('--lower',default=os.environ.get('ZIG_SELFTEST_LOWER',str(HOME/'workspace/u624-int/tools/adapters/lower')))
    ap.add_argument('--omc',default=os.environ.get('OMEGA_NATIVE_OMC',str(HOME/'workspace/omega-seedqual-20261025/seed/out/omc')))
    ap.add_argument('--bridge',default=os.environ.get('OMEGA_BRIDGE_REPO',str(HOME/'workspace/omega-bridge')))
    ap.add_argument('--sa-v2-root',default=os.environ.get('ZIG_SELFTEST_SA_V2_ROOT',str(ROOT)),help='tree with build/L832/sa (the #621 v2 codec)')
    ap.add_argument('--retarget',default=os.environ.get('ZIG_SELFTEST_RETARGET',str(ROOT/'tools/adapters/retarget') if (ROOT/'tools/adapters/retarget').is_file() else ''),
                    help='public tools/adapters/retarget; every use is cross-checked against the test-code R')
    a=ap.parse_args()
    out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=False)
    cimp=Path(a.c_import);cexp=Path(a.c_export);v2root=Path(a.sa_v2_root);rtool=Path(a.retarget) if a.retarget else None
    Z=Zig(out,a.zig);C=COracle(out,a.cc);NAT=Native(out,a.lower,a.omc,a.bridge,a.cc)
    rng=random.Random(740);failures=[];counts={};rows_by_tag={}
    def bump(k,n=1):counts[k]=counts.get(k,0)+n
    v2_state='RUN' if (v2root/'build/L832/sa').is_file() else 'NOT_RUN'
    def sa1(cmd,env):
        r=run(['sh','tools/source-aware/sa',cmd],env,cwd=ROOT);bump('sa_v1_requests');return r
    def sa2(cmd,env):
        r=run(['sh','tools/source-aware/sa',cmd],env,cwd=v2root,env=dict(os.environ,SA_BUILD='build/L832'));bump('sa_v2_requests');return r
    def zimport(src):
        r=py(IMPORT,src);bump('zig_import_runs');return r
    def zexport(env):
        r=py(EXPORT,env);bump('zig_export_runs');return r
    TOOLM={'C':'c','Rust':'rust','Python':'python','Cpp':'cpp','Zig':'zig','Go':'go'}
    def R(env,m):
        """R(U, M) as a semantic-only envelope: the retarget tool when given (must equal the test-code R), else test code."""
        want=semonly(retarget(split(env)[0],m))
        if rtool is None:return want
        r=py(rtool,env,TOOLM[m]);bump('retarget_runs')
        check(r.returncode==0 and r.stdout==want,'retarget %s rc=%d differs from test-code R %s'%(TOOLM[m],r.returncode,r.stderr))
        return r.stdout
    def native(sem,nargs,vecs,ref):
        if NAT.state!='RUN':return 'NOT_RUN ('+NAT.reason+')'
        check(NAT.values(N(sem),nargs,vecs)==ref,'native realization lower(N(U_A)) != reference');return 'PASS'

    f1=json.loads((ROOT/'tests/universal/translation/fixtures-f1.json').read_text())
    f2=json.loads((ROOT/'tests/universal/translation/fixtures-f2-zig.json').read_text())
    f1pos={p['id']:p for p in f1['positives']}
    check(len(f2['positives'])+len(f2['not_in_profile'])==len(f1pos),'F2-zig does not cover every F1 positive')
    check({p['f1_id'] for p in f2['positives']}|{p['f1_id'] for p in f2['not_in_profile']}==set(f1pos),'F2-zig F1 id set')

    # Oracle controls: a plain + must panic on overflow in Debug and ReleaseSafe (checked illegal behavior);
    # the same harness with +% wraps. Proves the build modes keep the safety checks a plain operator would hit.
    ctl_dir=out/'zig-control';oracle_control={}
    for mode in ZIG_MODES:
        okp,errp,exep=Z.build(ctl_dir/('plain-'+mode),[(b'pub fn calc(a: u64, b: u64) u64 {\n    return a + b;\n}\n','calc',2)],mode)
        okw,errw,exew=Z.build(ctl_dir/('wrap-'+mode),[(b'pub fn calc(a: u64, b: u64) u64 {\n    return a +% b;\n}\n','calc',2)],mode)
        rp=run([str(exep)],b'0 %d 1 0 0 0 0\n'%MASK) if okp else None
        rw=run([str(exew)],b'0 %d 1 0 0 0 0\n'%MASK) if okw else None
        good=okp and okw and rp.returncode!=0 and b'integer overflow' in rp.stderr and rw.returncode==0 and rw.stdout==b'0\n'
        oracle_control[mode]='PASS' if good else 'FAIL'
        if not good:failures.append({'name':'oracle-control-'+mode,'result':'FAIL'})

    def export_checks(sem,env_with_sidecar,tag):
        """Exporter: identical bytes for every family member, semantic-only; returns src_B."""
        r=zexport(env_with_sidecar);check(r.returncode==0 and r.stderr==b'',tag+' export refused rc=%d %s'%(r.returncode,r.stderr))
        text=r.stdout
        for m in MEMBERS:
            u=R(env_with_sidecar,m);rs=split(u)[0]
            check(N(rs)==N(sem),tag+' N(R(U,%s)) != N(U)'%m)
            if MEMBERS[m][0]==1:check(sa1('decode',u).returncode==0,tag+' R(U,%s) not a valid v1 unit'%m)
            elif v2_state=='RUN':check(sa2('decode',u).stdout==u,tag+' R(U,%s) not a valid v2 unit'%m)
            q=zexport(u);check(q.returncode==0 and q.stdout==text,tag+' export differs for member '+m+' (semantic-only)')
        bump('member_exports',len(MEMBERS))
        lines=text.split(b'\n')
        check(text.startswith(b'pub fn calc(') and text.endswith(b';\n}\n') and len(lines)==4 and lines[1].startswith(b'    return '),tag+' export form')
        check(not PLAIN.search(lines[1]),tag+' plain operator in export')
        return text

    def reimport_identity(sem_a,src_b,tag):
        r=zimport(src_b);check(r.returncode==0,tag+' re-import refused rc=%d %s'%(r.returncode,r.stderr))
        sem_b,side_b,ops_b=decode(r.stdout,'Zig');sidecar(sem_b,side_b,src_b,len(ops_b))
        check(N(strip(sem_b))==N(strip(sem_a)),tag+' identity N(strip(U_B)) != N(strip(U_A))')
        return sem_b,ops_b

    def row(tag,fn):
        item={'id':tag};rows_by_tag[tag]=item
        try:fn(item);item.setdefault('result','PASS')
        except (Fail,ValueError,KeyError,IndexError,struct.error,subprocess.TimeoutExpired) as e:
            item.update(result='FAIL',failure=str(e));failures.append(item)
        return item

    # ---- F2-zig positives ----
    def positive(fx):
        def body(item):
            src=fx['source'].encode();check(hashlib.sha256(src).hexdigest()==fx['sha256'],'fixture sha')
            p1=f1pos[fx['f1_id']];check(p1['sha256']==fx['f1_sha256'] and p1['inputs']==fx['inputs'] and p1['name']==fx['name'],'F1 link')
            r=zimport(src);check(r.returncode==0 and r.stderr==b'','import refused rc=%d %s'%(r.returncode,r.stderr))
            check(zimport(src).stdout==r.stdout,'import not byte-deterministic')
            env=r.stdout;sem,side,ops=decode(env,'Zig');spans=sidecar(sem,side,src,len(ops))
            check(normalized(ops),'importer must append the normalization pair')
            check(spans[-1]==spans[-2]==(0,0),'synthetic pair spans')
            for (op,_,_,imm),(lo,hi) in zip(ops[:-2],spans[:-2]):
                t=src[lo:hi]
                if op==1:check(t==b'%d'%imm,'literal span')
                elif op==9:check(len(t)>0 and (t==b'_' or t[:1].isalpha()) and t.isascii(),'input span')
                else:check(t==b'+%-%*%'[2*(op-2):2*(op-1)],'operator span')
            if v2_state=='RUN':
                ident=sa2('identity',env);check(ident.returncode==0 and ident.stdout.hex()==identity(sem),'sa v2 identity refused or differs')
                check(sa2('decode',env).stdout==env,'sa v2 decode changed unit');check(sa2('replay',env).stdout==src,'sa v2 replay differs from source')
            c=run([str(cimp)],p1['source'].encode());bump('c_import_runs');check(c.returncode==0,'C import of F1 refused')
            csem,_,cops=decode(c.stdout,'C');check(N(sem)==N(csem),'cross-importer identity: N(import_Zig(F2)) != N(import_C(F1))')
            exported=export_checks(sem,env,fx['id'])
            sem_b,ops_b=reimport_identity(sem,exported,fx['id'])
            check(sem_b==sem,'same-language round trip changed the semantic bytes')
            vec=vectors(fx['inputs'],rng);ref=[evaluate(ops,v) for v in vec]
            Z.add(fx['id'],src,fx['name'],vec,ref);Z.add(fx['id'],exported,'calc',vec,ref)
            check(C.values(p1['source'].encode(),p1['name'],vec)==ref,'cc(F1 C) != reference')
            s2=strip(sem);q=zexport(semonly(s2));check(q.returncode==0,'export of stripped unit refused')
            r2=zimport(q.stdout);check(r2.returncode==0,'re-import of stripped export refused')
            sem2,_,ops2=decode(r2.stdout,'Zig')
            check([evaluate(ops2,v) for v in vec]==[evaluate(records(s2),v) for v in vec],'stripped round trip evaluation differs')
            item.update(ops=len(ops),vectors=len(vec),semantic_identity=identity(sem),N_identity=identity(N(sem)),exported=exported.decode(),
                        stripped_reimport_identity_equal=N(strip(sem2))==N(s2))
        return body
    rows=[row(fx['id'],positive(fx)) for fx in f2['positives']]

    # ---- Zig import refusals ----
    refusals=[]
    for fx in f2['refusals']:
        src=bytes.fromhex(fx['source_hex']) if 'source_hex' in fx else fx['source'].encode()
        r=zimport(src);ok=hashlib.sha256(src).hexdigest()==fx['sha256'] and r.returncode==fx['status'] and r.stdout==b'' and r.stderr.startswith(b'ZIG_IMPORT_')
        it={'id':fx['id'],'name':fx['name'],'want':fx['status'],'exit':r.returncode,'stderr':r.stderr.decode(errors='replace').strip(),'result':'PASS' if ok else 'FAIL'}
        if not ok:failures.append(it)
        refusals.append(it)

    # ---- import boundary controls (exact and over capacity); accepted ones exported, re-imported, run ----
    head=b'pub fn calc(a: u64, b: u64) u64 {\n    return '
    base=head+b'a +% b;\n}\n'
    chain=lambda k:head+b'a'+b' +% a'*k+b' +% b;\n}\n'
    parens=lambda k:head+b'('*k+b'a'+b')'*k+b' +% b;\n}\n'
    asnest=lambda k:head+b'('*k+b'a +% @as(u64, 5)'+b')'*k+b' +% b;\n}\n'
    controls=[('exact-source-capacity',base+b' '*(4096-len(base)),0,12),('comment-at-capacity',base+b'//'+b'x'*(4096-len(base)-2),0,12),
              ('operation-capacity',chain(59),0,60*5+7),('nesting-limit',parens(16),0,12),('as-nesting-limit',asnest(15),0,17),
              ('over-source-capacity',base+b' '*(4097-len(base)),67,None),('over-operation-capacity',chain(60),67,None),
              ('over-nesting-limit',parens(17),67,None),('over-as-nesting-limit',asnest(16),67,None)]
    # Accepted spelling variants: each must import, round-trip and build/run under the Zig oracle (accepted => valid Zig).
    variants=[('no-whitespace',b'pub fn calc(a:u64,b:u64)u64{return a+%b*%a-%@as(u64,3);}',0,37),
              ('tabs-and-bare-cr',b'pub\tfn\tcalc(a: u64,\tb: u64) u64 {\r\treturn a\t+%\tb;\r}\r',0,12),
              ('comments-everywhere',b'pub // x\nfn calc(a: u64, // y\n b: u64) u64 { // z\n    return a +% // w\n b; // v\n} // end',0,12),
              ('return-paren',b'pub fn calc(a: u64, b: u64) u64 {\n    return(a)+%(b);\n}\n',0,12),
              ('as-spacing',b'pub fn calc(a: u64, b: u64) u64 {\n    return a +% @as( u64 , 5 ) +% b;\n}\n',0,17),
              ('four-slash-and-unicode-comment','//// \u00fc \U0001d11e \u2028 \u0085\npub fn calc(a: u64, b: u64) u64 {\n    return a +% b;\n}\n'.encode(),0,12),
              ('operator-line-start',b'pub fn calc(a: u64, b: u64) u64 {\n    return a\n        *% b\n        -% 1;\n}\n',0,34),
              ('discard-params',b'pub fn calc(_: u64, b: u64, _: u64) u64 {\n    return b *% b;\n}\n',0,None)]
    controls+=[('variant-'+n,s,w,v) for n,s,w,v in variants]
    ctl=[]
    for name,src,want,value in controls:
        def body(it,name=name,src=src,want=want,value=value):
            it['source_bytes']=len(src);r=zimport(src);it['exit']=r.returncode
            if want:check(r.returncode==want and not r.stdout and r.stderr==b'ZIG_IMPORT_LIMIT\n','%s rc=%d'%(name,r.returncode));return
            check(r.returncode==0,'%s refused rc=%d %s'%(name,r.returncode,r.stderr));sem,side,ops=decode(r.stdout,'Zig');sidecar(sem,side,src,len(ops))
            nargs=sum(1 for o in ops if o[0]==9)
            if value is not None:check(evaluate(ops,[5,7])==value,name+' value %d'%evaluate(ops,[5,7]))
            exported=export_checks(sem,r.stdout,name)
            sem_b,_=reimport_identity(sem,exported,name);check(sem_b==sem,name+' round trip')
            vec=vectors(nargs,rng);ref=[evaluate(ops,v) for v in vec]
            Z.add('control '+name,src,'calc',vec,ref);Z.add('control '+name,exported,'calc',vec,ref)
            it.update(ops=len(ops),exported_bytes=len(exported))
        ctl.append(row('control '+name,body))
    def closed(tool,msg):
        r=subprocess.run([str(ROOT/'build/L740'/tool)],stdin=subprocess.DEVNULL,capture_output=True,preexec_fn=lambda:os.close(0),timeout=10)
        return {'id':tool+'-closed-stdin','exit':r.returncode,'result':'PASS' if r.returncode==77 and not r.stdout and r.stderr==msg else 'FAIL'}
    ctl+=[closed('zig-import',b'ZIG_IMPORT_IO\n'),closed('zig-export',b'ZIG_EXPORT_IO\n')]
    for tool in (IMPORT,EXPORT):
        r=py(tool,base,'unexpected');ctl.append({'id':tool.name+'-arguments-refused','exit':r.returncode,'result':'PASS' if r.returncode==64 and not r.stdout and b'BAD_COMMAND' in r.stderr else 'FAIL'})
    r=py(IMPORT,base);d=run([str(ROOT/'build/L740/zig-import')],base)
    ctl.append({'id':'public-import-identical-to-native','result':'PASS' if r.returncode==0 and r.stdout==d.stdout else 'FAIL'})
    closure=['src/source-aware/vendor/sha256.om','src/source-aware/core-v2.om','src/adapters/zig-u64-import.om','src/adapters/zig-u64-export.om','build/L740/build.json','build/L740/zig-import','build/L740/zig-export']
    for tool,victims in ((IMPORT,['src/source-aware/vendor/sha256.om','src/adapters/zig-u64-import.om','build/L740/zig-import']),
                         (EXPORT,['src/source-aware/core-v2.om','src/adapters/zig-u64-export.om','build/L740/zig-export'])):
        for victim in victims:
            with tempfile.TemporaryDirectory(prefix='closed-build-',dir=out) as td:
                t=Path(td)
                for rel in ['tools/adapters/'+tool.name]+closure:
                    p=t/rel;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/rel,p);p.chmod(0o700)
                with (t/victim).open('ab') as f:f.write(b'changed')
                r=run([sys.executable,str(t/'tools/adapters'/tool.name)],base)
                ok=r.returncode==78 and not r.stdout and b'STALE_OR_MISSING_BUILD' in r.stderr
                ctl.append({'id':tool.name+'-stale-'+victim,'exit':r.returncode,'result':'PASS' if ok else 'FAIL'})
    failures+=[c for c in ctl if c['result']=='FAIL' and c not in failures]

    # ---- exporter refusals ----
    good=zimport(base).stdout
    U=65535
    def unit(member,ops,typ=1,ver=None,lang=None,prof=None,runtime=0):
        v,l,p,_=MEMBERS[member]
        v=v if ver is None else ver;l=l if lang is None else lang;p=p if prof is None else prof
        sem=b'OMSA'+bytes([v,l,p,1,runtime,1])+struct.pack('<H',len(ops))+CONTRACT
        for op,attr,eff,alias,obl,x,y,imm in ops:sem+=struct.pack('<8BHHQ',l,1,op,typ,attr,eff,alias,obl,x,y,imm)
        return semonly(sem)
    def shared(k):  # t_j = add(t_{j-1}, t_{j-1}): tree size 2^k
        return unit('Zig',[(9,0,0,0,1,U,U,0)]+[(2,1,0,0,1,j,j,0) for j in range(k)])
    def rnest(k):  # x0 +% (x0 +% (...)): parenthesis depth k-1
        return [(9,0,0,0,1,U,U,0)]+[(2,1,0,0,1,0,j,0) for j in range(k)]
    def asdeep(k):  # x0 +% (x0 +% (... (@as(u64, 5) +% 6))): k parenthesised levels around the @as leaf
        ops=[(9,0,0,0,1,U,U,0),(1,1,0,0,0,U,U,5),(1,1,0,0,0,U,U,6),(2,1,0,0,1,1,2,0)]
        for _ in range(k-1):ops.append((2,1,0,0,1,0,len(ops)-1,0))
        return ops
    flipped=bytearray(good);flipped[-3]^=1
    xr=[('truncated',good[:-1],65),('bad-magic',b'X'+good[1:],65),('sidecar-source-mismatch',bytes(flipped),65),('oversize',good+b'\0'*(8193-len(good)),65),
        ('semantic-version-3',bytes(good[:17])+b'\x03'+good[18:],65),('version-1-profile-8',unit('Zig',[(9,0,0,0,1,U,U,0)],ver=1),68),
        ('version-2-profile-1',unit('C',[(9,0,0,0,1,U,U,0)],ver=2),68),
        ('profile3-bigint',unit('Python',[(9,0,0,0,1,U,U,0),(9,0,0,0,1,U,U,1),(2,2,20,255,8,0,1,0)],typ=2,prof=3,runtime=1),68),
        ('zig-div-op5',unit('Zig',[(9,0,0,0,1,U,U,0),(9,0,0,0,1,U,U,1),(5,1,0,0,3,0,1,0)]),68),
        ('c-div-op5',unit('C',[(9,0,0,0,1,U,U,0),(9,0,0,0,1,U,U,1),(5,1,0,0,3,0,1,0)]),68),
        ('slot-gap',unit('Zig',[(9,0,0,0,1,U,U,1)]),68),('language-profile-mismatch',bytes(good[:18])+b'\x05'+good[19:],68),
        ('shared-ops-over-capacity',shared(6),67),('depth-over-limit',unit('Zig',rnest(18)),67),('as-depth-over-limit',unit('Zig',asdeep(17)),67),
        ('size-over-limit',shared(9),67)]
    xrows=[]
    for name,env,want in xr:
        it={'id':'export '+name,'want':want}
        if name in ('profile3-bigint','c-div-op5'):it['v1_codec_accepts']=sa1('decode',env).returncode==0
        if name=='zig-div-op5':it['v2_codec_accepts']=(sa2('decode',env).returncode==0) if v2_state=='RUN' else 'NOT_RUN'
        r=zexport(env);ok=r.returncode==want and not r.stdout and r.stderr.startswith(b'ZIG_EXPORT_') and it.get('v1_codec_accepts',True) is not False and it.get('v2_codec_accepts',True) is not False
        it.update(exit=r.returncode,stderr=r.stderr.decode(errors='replace').strip(),result='PASS' if ok else 'FAIL')
        if not ok:failures.append(it)
        xrows.append(it)
    # Accepted exporter boundaries: shared SSA expanded to exactly 64 re-import operations; depth exactly 16.
    exact=[(9,0,0,0,1,U,U,0)]+[(2,1,0,0,1,j,j,0) for j in range(5)]+[(2,1,0,0,1,5,4,0),(2,1,0,0,1,6,3,0),(2,1,0,0,1,7,2,0),(2,1,0,0,1,8,1,0)]
    for name,env,count in (('shared-ops-at-capacity',unit('Zig',exact),64),('depth-at-limit',unit('Zig',rnest(17)),20),('as-depth-at-limit',unit('Zig',asdeep(16)),21)):
        def body(it,name=name,env=env,count=count):
            sem,_=split(env);ops=records(sem);r=zexport(env);check(r.returncode==0,name+' refused rc=%d'%r.returncode)
            q=zimport(r.stdout);check(q.returncode==0,name+' re-import refused');_,_,ops2=decode(q.stdout,'Zig')
            check(len(ops2)==count,name+' re-import operation count %d'%len(ops2))
            vec=vectors(1,rng);ref=[evaluate(ops,v) for v in vec]
            check([evaluate(ops2,v) for v in vec]==ref,name+' evaluation');Z.add('export '+name,r.stdout,'calc',vec,ref)
            it.update(reimport_ops=len(ops2),identity_equal=N(strip(split(q.stdout)[0]))==N(strip(sem)))
        xrows.append(row('export '+name,body))

    # ---- composition C -> Zig: F1 positives and the four accepted C import boundary controls ----
    ch=b'#include <stdint.h>\nuint64_t calc(uint64_t a,uint64_t b){return a+b;}'
    cctl=[('C-exact-source-capacity',ch+b' '*(4096-len(ch)),'calc',2),('C-comment-at-capacity',ch+b'//'+b'x'*(4096-len(ch)-2),'calc',2),
          ('C-operation-capacity',b'#include <stdint.h>\nuint64_t calc(uint64_t a,uint64_t b){return '+b'+'.join([b'a']*61)+b';}','calc',2),
          ('C-nesting-limit',b'#include <stdint.h>\nuint64_t calc(uint64_t a,uint64_t b){return '+b'('*16+b'a'+b')'*16+b';}','calc',2)]
    comp=[]
    for tag,csrc,name,nargs in [(p['id'],p['source'].encode(),p['name'],p['inputs']) for p in f1['positives']]+cctl:
        def body(it,tag=tag,csrc=csrc,name=name,nargs=nargs):
            c=run([str(cimp)],csrc);bump('c_import_runs');check(c.returncode==0,'C import refused')
            sem_a,_,ops_a=decode(c.stdout,'C')
            src_b=export_checks(sem_a,c.stdout,tag)
            sem_b,ops_b=reimport_identity(sem_a,src_b,tag)
            vec=vectors(nargs,rng);ref=[evaluate(ops_a,v) for v in vec]
            check(C.values(csrc,name,vec)==ref,'cc(original C) != reference(U_A)')
            check([evaluate(ops_b,v) for v in vec]==ref,'reference(U_B) != reference(U_A)')
            Z.add('C->Zig '+tag,src_b,'calc',vec,ref)
            it.update(identity=identity(N(strip(sem_a))),vectors=len(vec),native_realization=native(sem_a,nargs,vec,ref))
        comp.append(row('C->Zig '+tag,body))

    # ---- composition Zig -> C: export_C(R(U_A, C)), then C import ----
    zc=[];zig_to_c='RUN' if cexp.is_file() else 'NOT_RUN'
    if zig_to_c=='RUN':
        for fx in f2['positives']:
            def body(it,fx=fx):
                src=fx['source'].encode();r=zimport(src);sem_a,_,ops_a=decode(r.stdout,'Zig')
                q=py(cexp,R(r.stdout,'C'));bump('c_export_runs');check(q.returncode==0,'c-export refused R(U,C) rc=%d %s'%(q.returncode,q.stderr))
                c=run([str(cimp)],q.stdout);bump('c_import_runs');check(c.returncode==0,'C import of exported C refused')
                sem_b,_,ops_b=decode(c.stdout,'C');check(N(strip(sem_b))==N(strip(sem_a)),'identity N(strip(U_B)) != N(strip(U_A))')
                vec=vectors(fx['inputs'],rng);ref=[evaluate(ops_a,v) for v in vec]
                check(C.values(q.stdout,'calc',vec)==ref,'cc(src_B) != reference(U_A)')
                check([evaluate(ops_b,v) for v in vec]==ref,'reference(U_B) != reference(U_A)')
                Z.add('Zig->C '+fx['id'],src,fx['name'],vec,ref)
                it.update(vectors=len(vec),exported_c=q.stdout.decode(),native_realization=native(sem_a,fx['inputs'],vec,ref))
            zc.append(row('Zig->C '+fx['id'],body))

    # ---- deferred Zig oracle: one harness per build mode over every requested program ----
    zfail=Z.evaluate_all()
    for tag,msg in zfail.items():
        item=rows_by_tag.get(tag)
        if item is None:failures.append({'id':tag,'result':'FAIL','failure':msg});continue
        if item['result']=='PASS':item.update(result='FAIL',failure=msg);failures.append(item)
    for item in rows_by_tag.values():
        if item['result']=='PASS':item['zig_modes']=list(ZIG_MODES)

    zv=run([a.zig,'version'],None).stdout.decode().strip()
    report={'schema':'omega.zig-u64.selftest.v1','result':'FAIL' if failures else 'PASS','status_flags':'TESTED (author self-test), not QUALIFIED',
            'evidence':'REFERENCE + DIFFERENTIAL (Zig/cc/native NATIVE AArch64); no PROOF','source_commit':subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip(),
            'build':json.loads((ROOT/'build/L740/build.json').read_text()),'zig':{'version':zv,'sha256':hashlib.sha256(Path(a.zig).read_bytes()).hexdigest(),'modes':list(ZIG_MODES)},
            'cc':run([a.cc,'--version'],None).stdout.decode().split('\n')[0],'oracle_control':oracle_control,
            'c_import_sha256':hashlib.sha256(cimp.read_bytes()).hexdigest(),'sa_v1_binary_sha256':hashlib.sha256((ROOT/'build/L574/sa').read_bytes()).hexdigest(),
            'sa_v2':v2_state if v2_state=='NOT_RUN' else {'root':str(v2root),'binary_sha256':hashlib.sha256((v2root/'build/L832/sa').read_bytes()).hexdigest()},
            'c_export':str(cexp) if zig_to_c=='RUN' else 'NOT_RUN','native_realization':NAT.state if NAT.state=='RUN' else 'NOT_RUN ('+NAT.reason+')',
            'retarget':{'tool':str(rtool),'sha256':hashlib.sha256((rtool.parent.parent.parent/'build/L833/retarget').read_bytes()).hexdigest(),'cross_check':'every tool output equals the test-code R'} if rtool else 'R applied in test code (tools/adapters/retarget not given)',
            'positives':len(rows),'positives_passed':sum(r['result']=='PASS' for r in rows),'not_in_profile':len(f2['not_in_profile']),
            'refusals':len(refusals),'refusals_passed':sum(r['result']=='PASS' for r in refusals),
            'controls':len(ctl),'controls_passed':sum(r['result']=='PASS' for r in ctl),'export_cases':len(xrows),'export_cases_passed':sum(r['result']=='PASS' for r in xrows),
            'c_to_zig':len(comp),'c_to_zig_passed':sum(r['result']=='PASS' for r in comp),'zig_to_c':zig_to_c,'zig_to_c_cases':len(zc),'zig_to_c_passed':sum(r['result']=='PASS' for r in zc),
            'zig_compiles':Z.compiles,'zig_observations':Z.observations,'cc_compiles':C.compiles,'cc_observations':C.observations,'native_calls':NAT.calls,
            'counts':counts,'cases':rows,'refusal_cases':refusals,'control_cases':ctl,'export_rows':xrows,'c_to_zig_cases':comp,'zig_to_c_rows':zc,'failures':failures}
    (out/'result.json').write_text(json.dumps(report,indent=1,ensure_ascii=False)+'\n')
    keys=['positives','positives_passed','not_in_profile','refusals','refusals_passed','controls','controls_passed','export_cases','export_cases_passed',
          'c_to_zig','c_to_zig_passed','zig_to_c','zig_to_c_cases','zig_to_c_passed','zig_compiles','zig_observations','cc_observations','native_calls']
    print(report['result'],' '.join('%s=%s'%(k,report[k]) for k in keys),'sa_v2=%s'%('RUN' if v2_state=='RUN' else 'NOT_RUN'),
          'retarget=%s'%('TOOL' if rtool else 'TEST_CODE'),'native=%s'%NAT.state,'oracle_control=%s'%json.dumps(oracle_control),' '.join('%s=%s'%kv for kv in counts.items()))
    for f in failures[:20]:print('FAIL',json.dumps(f,ensure_ascii=False)[:600])
    return 1 if failures else 0

if __name__=='__main__':raise SystemExit(main())
