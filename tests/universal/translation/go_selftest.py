#!/usr/bin/env python3
"""Go lane (#741) author self-test: TESTED evidence only (translation-v1 section 10), never QUALIFIED.

For every F2-go fixture: Go import, independent decode, #621 v2 codec `sa identity|decode|replay`
(build/L832, or NOT_RUN), cross-importer identity against the C import of the F1 source, export of the
unit and of R(U, M) for all six family members (identical bytes, gofmt-clean), re-import identity
(translation-compose-v1 section 4.1), and Go 1.27.3 DIFFERENTIAL (original and exported Go) against a
bigint evaluation of the records. The untyped-constant rule of go-u64-v1.md section 1.1 is checked
against `go build` on the refusal fixtures and on seeded random constant/variable trees. Compositions
C->Go and Go->C (R(U, C) in test code until tools/adapters/retarget lands; the G1 C exporter accepts
v1 C units only), with the native realization lower(N(U_A)) when the lowering, omc 734f0a39 and the
bridge are present (otherwise NOT_RUN).
"""
from pathlib import Path
import argparse,hashlib,json,os,random,shutil,struct,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[3]
HOME=Path.home()
MASK=(1<<64)-1
SEMDOM=b'omega.source.semantic.v1'
CONTRACT=hashlib.sha256((ROOT/'spec/universal/semantics.md').read_bytes()).digest()
# translation-compose-v2 section 1: OMSA version, language, profile, arithmetic obligations (dialect = language)
MEMBERS={'C':(1,1,1,1),'Rust':(1,2,2,1),'Python':(1,3,4,5),'C++':(2,5,7,1),'Zig':(2,6,8,1),'Go':(2,7,9,1)}
BOUNDARY=[0,1,2,(1<<32)-1,1<<32,(1<<63)-1,1<<63,MASK-1,MASK]
IMPORT=ROOT/'tools/adapters/go-import';EXPORT=ROOT/'tools/adapters/go-export'
OMC_PIN='734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61'
BRIDGE_COMMIT='012860dbf26ee62a9838594b9dfd1e3e94a540a9'
NATIVE_OM_PIN='2c2c53f61d4ab302f1a2092c3c32189335dd53f6cf2b388f47a475f99f2c3940'
H=b'package main\n\n'

class Fail(Exception):pass
def check(c,msg):
    if not c:raise Fail(msg)

def run(cmd,data,cwd=None,timeout=60,env=None):
    return subprocess.run(cmd,input=data,capture_output=True,timeout=timeout,cwd=cwd,env=env)

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
    """R(U, M), translation-compose-v2 section 2 (semantic bytes; callers wrap them semantic-only)."""
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

# ---- oracles ----
GO_HARNESS='''package main

import (
	"bufio"
	"fmt"
	"os"
	"strconv"
	"strings"
)

func main() {
	sc := bufio.NewScanner(os.Stdin)
	for sc.Scan() {
		f := strings.Fields(sc.Text())
		if len(f) != %d {
			panic("arity")
		}
		v := make([]uint64, 6)
		for i, s := range f {
			x, err := strconv.ParseUint(s, 10, 64)
			if err != nil {
				panic(err)
			}
			v[i] = x
		}
		_ = v
		fmt.Println(%s)
	}
}
'''

class Oracles:
    def __init__(s,out,go,gofmt,cc):
        s.out=out;s.go=go;s.gofmt=gofmt;s.cc=cc;s.cache={};s.compiles=0;s.go_builds=0;s.gofmt_runs=0
        s.env=dict(os.environ,GOTOOLCHAIN='local',GOFLAGS='-mod=mod',GOPROXY='off',GOWORK='off',CGO_ENABLED='0',
                   GOCACHE=str(out/'gocache'),GOPATH=str(out/'gopath'),GOTELEMETRY='off')
    def gomodule(s,text,harness):
        d=Path(tempfile.mkdtemp(prefix='go-',dir=s.out/'oracle'))
        (d/'go.mod').write_text('module oracle\n\ngo 1.27\n');(d/'unit.go').write_bytes(text);(d/'zz_oracle.go').write_text(harness)
        return d
    def gobuild(s,text,harness):
        (s.out/'oracle').mkdir(exist_ok=True);d=s.gomodule(text,harness)
        r=run(['nice','-n','19',s.go,'build','-trimpath','-o',str(d/'calc'),'.'],None,cwd=d,timeout=300,env=s.env);s.go_builds+=1
        return r,d/'calc'
    def gobare(s,text):
        """go build of the unit with a harness that only declares func main: True iff Go accepts the file."""
        r,_=s.gobuild(text,'package main\n\nfunc main() {}\n');return r.returncode==0,r.stderr.decode(errors='replace').strip()
    def build(s,kind,text,name,nargs):
        key=hashlib.sha256(kind.encode()+b'\0'+name.encode()+b'\0'+text+b'\0%d'%nargs).hexdigest()
        if key in s.cache:return s.cache[key]
        if kind=='go':
            r,exe=s.gobuild(text,GO_HARNESS%(nargs,name+'('+', '.join('v[%d]'%j for j in range(nargs))+')'))
            check(r.returncode==0,'go oracle build failed: '+r.stderr.decode(errors='replace')[-800:])
        else:
            d=s.out/'oracle';d.mkdir(exist_ok=True);exe=d/key[:24];f=d/(key[:24]+'.c')
            body=('uint64_t x[6]={0};while(scanf("'+' '.join(['%" SCNu64 "']*nargs)+'",'+','.join('&x[%d]'%j for j in range(nargs))+')==%d)'%nargs+
                  'printf("%" PRIu64 "\\n",'+name+'('+','.join('x[%d]'%j for j in range(nargs))+'));') if nargs else 'printf("%" PRIu64 "\\n",'+name+'());'
            f.write_bytes(text+('\n#include <stdio.h>\n#include <inttypes.h>\nint main(void){'+body+'return 0;}\n').encode())
            r=run(['nice','-n','19',s.cc,'-std=c17','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter',str(f),'-o',str(exe)],None,timeout=180)
            check(r.returncode==0,'cc oracle compile failed: '+r.stderr.decode(errors='replace')[-800:])
        s.compiles+=1;s.cache[key]=exe;return exe
    def values(s,kind,text,name,vectors):
        nargs=len(vectors[0]);exe=s.build(kind,text,name,nargs)
        r=run([str(exe)],''.join(' '.join(map(str,v))+'\n' for v in vectors).encode(),timeout=60)
        check(r.returncode==0,kind+' oracle run failed: '+r.stderr.decode(errors='replace')[-400:])
        return [int(x) for x in r.stdout.split()]
    def gofmt_clean(s,text):
        r=run([s.gofmt],text);s.gofmt_runs+=1
        return r.returncode==0 and r.stdout==text

class Native:
    """lower(N(U)) -> omc 734f0a39 function mode -> bridge native.om binding -> C caller (translation-v1 section 5)."""
    def __init__(s,out,lower,omc,bridge,cc):
        s.out=out/'native';s.out.mkdir();s.lower=lower;s.omc=omc;s.cc=cc;s.cache={};s.calls=0
        check(hashlib.sha256(Path(omc).read_bytes()).hexdigest()==OMC_PIN,'omc sha256 pin')
        src=subprocess.run(['git','-C',bridge,'show',f'{BRIDGE_COMMIT}:src/native.om'],capture_output=True,check=True).stdout
        check(hashlib.sha256(src).hexdigest()==NATIVE_OM_PIN,'bridge native.om sha256 pin')
        r=run([omc],src);check(r.returncode==0,'bridge native.om compile')
        s.gen=s.out/'native-gen';s.gen.write_bytes(r.stdout);s.gen.chmod(0o700)
    def values(s,nsem,vectors):
        key=identity(nsem);nargs=len(vectors[0])
        if key not in s.cache:
            d=s.out/key[:24];d.mkdir()
            r=py(s.lower,semonly(nsem));check(r.returncode==0,'lower refused N(U) rc=%d %s'%(r.returncode,r.stderr))
            unit=r.stdout;(d/'unit.om').write_bytes(unit)
            c=run([s.omc],unit);check(c.returncode==0 and c.stdout,'omc function-mode compile failed')
            b=run([str(s.gen)],unit+b'\0'+b'%08d'%len(c.stdout)+c.stdout);check(b.returncode==0 and b.stdout,'bridge binding failed')
            params=''.join(', uint64_t i%d'%i for i in range(nargs));args=''.join(', a[%d]'%i for i in range(nargs))
            caller=('#include <inttypes.h>\n#include <stdio.h>\n#include <stdlib.h>\nuint32_t omega_calc(uint64_t *result%s);\n'
                    'int main(void){char line[512];while(fgets(line,sizeof line,stdin)){char *p=line;uint64_t a[6]={0};'
                    'for(int i=0;i<%d;i++)a[i]=strtoull(p,&p,10);(void)a;(void)p;uint64_t r=UINT64_C(6148914691236517205);'
                    'uint32_t st=omega_calc(&r%s);printf("%%" PRIu32 " %%" PRIu64 "\\n",st,r);}return 0;}\n')%(params,nargs,args)
            (d/'omega_native.c').write_bytes(b.stdout);(d/'caller.c').write_text(caller)
            k=run([s.cc,'-std=gnu11','-O2','-Wall','-Wextra','-Werror','-pthread',str(d/'caller.c'),str(d/'omega_native.c'),'-o',str(d/'caller')],None,timeout=180)
            check(k.returncode==0,'native caller compile: '+k.stderr.decode(errors='replace')[-400:])
            s.cache[key]=d/'caller'
        r=run([str(s.cache[key])],''.join(' '.join(map(str,v))+'\n' for v in vectors).encode())
        lines=r.stdout.decode().split('\n')[:-1];check(r.returncode==0 and len(lines)==len(vectors),'native caller run')
        out=[]
        for line in lines:
            st,val=line.split();check(st=='0','native status '+st);out.append(int(val))
        s.calls+=len(lines);return out

def vectors(nargs,rng):
    if nargs==0:return [()]
    if nargs<=2:base=[(x,) for x in BOUNDARY] if nargs==1 else [(x,y) for x in BOUNDARY for y in BOUNDARY]
    else:base=[tuple(BOUNDARY[(j+k)%len(BOUNDARY)] for k in range(nargs)) for j in range(len(BOUNDARY))]
    return base+[tuple(rng.getrandbits(64) for _ in range(nargs)) for _ in range(32)]

# ---- random constant/variable trees for the untyped-constant rule ----
CONSTS=[0,1,2,3,(1<<32)-1,1<<32,(1<<63)-1,1<<63,MASK-1,MASK]
def rtree(rng,d):
    if d==0 or rng.random()<0.3:
        return ('v',rng.randrange(2)) if rng.random()<0.35 else ('c',rng.choice(CONSTS))
    return (rng.choice('+-*'),rtree(rng,d-1),rtree(rng,d-1))
def isconst(t):return t[0]=='c' or (t[0] in '+-*' and isconst(t[1]) and isconst(t[2]))
def exact(t):
    if t[0]=='c':return t[1]
    a,b=exact(t[1]),exact(t[2]);return a+b if t[0]=='+' else a-b if t[0]=='-' else a*b
def profile_ok(t):
    """go-u64-v1 section 1.1: every constant-only node of the tree evaluates exactly within 0..2^64-1."""
    if t[0]=='v':return True
    if t[0]=='c':return 0<=t[1]<=MASK
    return profile_ok(t[1]) and profile_ok(t[2]) and (not isconst(t) or 0<=exact(t)<=MASK)
def bigconst(t):
    """types2 rejects any untyped integer constant operation result wider than 512 bits (constant overflow)."""
    if t[0]!='c' and isconst(t):return abs(exact(t)).bit_length()>512 or bigconst(t[1]) or bigconst(t[2])
    return False
def go_ok(t):
    """The Go rule: every maximal constant expression (operand of a uint64 operation, or the returned value)
    is exact and must be representable as uint64; intermediate constant values are unbounded up to 512 bits."""
    if t[0]=='v':return True
    if isconst(t):return 0<=exact(t)<=MASK and not bigconst(t)
    return go_ok(t[1]) and go_ok(t[2])
PREC={'+':4,'-':4,'*':5}
def text(t,lang,parent=None,side=None):
    if t[0]=='v':return 'ab'[t[1]]
    if t[0]=='c':return str(t[1])+('ULL' if lang=='c' else '')
    p=PREC[t[0]];s=text(t[1],lang,p,'l')+' '+t[0]+' '+text(t[2],lang,p,'r')
    if parent is not None and (p<parent or (side=='r' and p<=parent)):s='('+s+')'
    return s
def tval(t,args):
    if t[0]=='v':return args[t[1]]
    if t[0]=='c':return t[1]
    a,b=tval(t[1],args),tval(t[2],args);return (a+b if t[0]=='+' else a-b if t[0]=='-' else a*b)&MASK

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--out',required=True)
    ap.add_argument('--c-import',default=str(HOME/'workspace/universal-read/build/L589/c-import'))
    ap.add_argument('--sa1-root',default=str(ROOT),help='checkout holding tools/source-aware/sa and build/L574/sa (#621 v1 codec)')
    ap.add_argument('--sa2-root',default=str(ROOT),help='checkout holding tools/source-aware/sa and build/L832/sa (#621 v2 codec)')
    ap.add_argument('--c-export',default=os.environ.get('GO_SELFTEST_C_EXPORT',str(HOME/'workspace/c-export-622/tools/adapters/c-export')),help='public c-export of a passing #622 checkout')
    ap.add_argument('--lower',default=str(ROOT/'tools/adapters/lower'))
    ap.add_argument('--omc',default=os.environ.get('OMEGA_NATIVE_OMC',str(HOME/'workspace/omega-seedqual-20261025/seed/out/omc')))
    ap.add_argument('--bridge',default=os.environ.get('OMEGA_BRIDGE_REPO',str(HOME/'workspace/omega-bridge')))
    ap.add_argument('--go',default=str(HOME/'.local/toolchains/go/bin/go'))
    ap.add_argument('--gofmt',default=str(HOME/'.local/toolchains/go/bin/gofmt'))
    ap.add_argument('--cc',default=os.environ.get('CC','cc'))
    ap.add_argument('--random-trees',type=int,default=160)
    a=ap.parse_args()
    out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=False)
    cimp=Path(a.c_import);sa1root=Path(a.sa1_root);sa1=sa1root/'build/L574/sa';sa2root=Path(a.sa2_root)
    O=Oracles(out,a.go,a.gofmt,a.cc);rng=random.Random(741);failures=[];counts={}
    def bump(k,n=1):counts[k]=counts.get(k,0)+n
    have_sa2=(sa2root/'build/L832/sa').is_file() and (sa2root/'tools/source-aware/sa').is_file()
    codec_v2='RUN' if have_sa2 else 'NOT_RUN (no build/L832/sa)'
    def sa2(cmd,env):
        r=run(['bash','tools/source-aware/sa',cmd],env,cwd=sa2root,env=dict(os.environ,SA_BUILD='build/L832'));bump('sa2_requests');return r
    def sa1run(cmd,env):
        e=dict(os.environ);e.pop('SA_BUILD',None)
        r=run(['bash','tools/source-aware/sa',cmd],env,cwd=sa1root,env=e);bump('sa1_requests');return r
    def gimport(src):
        r=py(IMPORT,src);bump('go_import_runs');return r
    def gexport(env):
        r=py(EXPORT,env);bump('go_export_runs');return r
    native=None;native_state='NOT_RUN'
    try:
        if Path(a.lower).is_file() and (ROOT/'build/L831/lower').is_file() and Path(a.omc).is_file():
            native=Native(out,Path(a.lower),a.omc,a.bridge,a.cc);native_state='RUN'
        else:native_state='NOT_RUN (lowering build, omc or bridge missing)'
    except (Fail,subprocess.CalledProcessError,OSError) as e:native_state='NOT_RUN ('+str(e)+')'
    cexp=Path(a.c_export) if a.c_export and Path(a.c_export).is_file() else None

    f1=json.loads((ROOT/'tests/universal/translation/fixtures-f1.json').read_text())
    f2=json.loads((ROOT/'tests/universal/translation/fixtures-f2-go.json').read_text())
    f1pos={p['id']:p for p in f1['positives']}
    check(len(f2['positives'])+len(f2['not_in_profile'])==len(f1pos),'F2-go does not cover every F1 positive')
    check({p['f1_id'] for p in f2['positives']}|{p['f1_id'] for p in f2['not_in_profile']}==set(f1pos),'F2-go F1 id set')

    def codec_checks(env,sem,src):
        if not have_sa2:return
        ident=sa2('identity',env);check(ident.returncode==0 and ident.stdout.hex()==identity(sem),'v2 sa identity refused or differs')
        check(sa2('decode',env).stdout==env,'v2 sa decode changed unit');check(sa2('replay',env).stdout==src,'v2 sa replay differs from source')
        r=sa1run('decode',env);check(r.returncode==66 and not r.stdout,'v1 codec must refuse a v2 unit with 66')

    def export_checks(sem,env_with_sidecar,tag):
        """Exporter: identical bytes for every family member with or without sidecar, gofmt-clean; returns src_B."""
        r=gexport(env_with_sidecar);check(r.returncode==0 and r.stderr==b'',tag+' export refused rc=%d %s'%(r.returncode,r.stderr))
        text=r.stdout
        for m in MEMBERS:
            u=semonly(retarget(sem,m))
            if MEMBERS[m][0]==1:check(sa1run('decode',u).returncode==0,tag+' R(U,%s) not a valid v1 unit'%m)
            elif have_sa2:check(sa2('decode',u).returncode==0,tag+' R(U,%s) not a valid v2 unit'%m)
            q=gexport(u);check(q.returncode==0 and q.stdout==text,tag+' export differs for member '+m+' (semantic-only)')
        bump('member_exports',len(MEMBERS))
        check(text.startswith(H+b'func calc(') and text.endswith(b'\n}\n') and text.count(b'\n')==5,tag+' export form')
        check(O.gofmt_clean(text),tag+' export is not gofmt-clean')
        return text

    def reimport_identity(sem_a,src_b,tag):
        r=gimport(src_b);check(r.returncode==0,tag+' re-import refused rc=%d %s'%(r.returncode,r.stderr))
        sem_b,side_b,ops_b=decode(r.stdout,'Go');sidecar(sem_b,side_b,src_b,len(ops_b))
        check(N(strip(sem_b))==N(strip(sem_a)),tag+' identity N(strip(U_B)) != N(strip(U_A))')
        return sem_b,ops_b

    def go_positive(tag,src,name,nargs,f1src=None,f1name=None):
        item={'id':tag}
        r=gimport(src);check(r.returncode==0 and r.stderr==b'','import refused rc=%d %s'%(r.returncode,r.stderr))
        env=r.stdout;sem,side,ops=decode(env,'Go');spans=sidecar(sem,side,src,len(ops))
        check(normalized(ops),'importer must append the normalization pair')
        check(spans[-1]==spans[-2]==(0,0),'synthetic pair spans')
        for (op,_,_,imm),(lo,hi) in zip(ops[:-2],spans[:-2]):
            t=src[lo:hi]
            if op==1:check(t==b'%d'%imm,'literal span')
            elif op==9:check(len(t)>0 and t.isascii() and t.decode().isidentifier(),'input span')
            else:check(t=={2:b'+',3:b'-',4:b'*'}[op],'operator span')
        codec_checks(env,sem,src)
        if f1src is not None:
            c=run([str(cimp)],f1src);bump('c_import_runs');check(c.returncode==0,'C import of F1 refused')
            csem,_,_=decode(c.stdout,'C');check(N(sem)==N(csem),'cross-importer identity: N(import_Go(F2)) != N(import_C(F1))')
        exported=export_checks(sem,env,tag)
        sem_b,_=reimport_identity(sem,exported,tag)
        check(sem_b==sem,'same-language round trip changed the semantic bytes')
        vec=vectors(nargs,rng);ref=[evaluate(ops,v) for v in vec]
        check(O.values('go',src,name,vec)==ref,'go(original Go) != reference')
        check(O.values('go',exported,'calc',vec)==ref,'go(exported Go) != reference')
        bump('go_observations',2*len(vec))
        if f1src is not None:check(O.values('c',f1src,f1name,vec)==ref,'cc(F1 C) != reference');bump('cc_observations',len(vec))
        s2=strip(sem);q=gexport(semonly(s2));check(q.returncode==0,'export of stripped unit refused')
        r2=gimport(q.stdout);check(r2.returncode==0,'re-import of stripped export refused')
        _,_,ops2=decode(r2.stdout,'Go');sops=[(o[2],o[8],o[9],o[10]) for o in ops_of(s2)]
        check([evaluate(ops2,v) for v in vec]==[evaluate(sops,v) for v in vec],'stripped round trip evaluation differs')
        item.update(result='PASS',ops=len(ops),vectors=len(vec),semantic_identity=identity(sem),N_identity=identity(N(sem)),exported=exported.decode())
        return item

    rows=[]
    for fx in f2['positives']:
        try:
            src=fx['source'].encode();check(hashlib.sha256(src).hexdigest()==fx['sha256'],'fixture sha')
            p1=f1pos[fx['f1_id']];check(p1['sha256']==fx['f1_sha256'] and p1['inputs']==fx['inputs'] and p1['name']==fx['name'],'F1 link')
            rows.append(go_positive(fx['id'],src,fx['name'],fx['inputs'],p1['source'].encode(),p1['name']))
        except (Fail,ValueError,KeyError,IndexError,struct.error,subprocess.TimeoutExpired) as e:
            it={'id':fx['id'],'result':'FAIL','failure':str(e)};failures.append(it);rows.append(it)
    extra_rows=[]
    for fx in f2['extra_positives']:
        try:
            src=fx['source'].encode();check(hashlib.sha256(src).hexdigest()==fx['sha256'],'fixture sha')
            extra_rows.append(go_positive(fx['id'],src,fx['function'],fx['inputs']))
        except (Fail,ValueError,KeyError,IndexError,struct.error,subprocess.TimeoutExpired) as e:
            it={'id':fx['id'],'result':'FAIL','failure':str(e)};failures.append(it);extra_rows.append(it)

    # ---- Go import refusals; go_build records what Go itself does with the refused file ----
    refusals=[]
    for fx in f2['refusals']:
        src=bytes.fromhex(fx['source_hex']) if 'source_hex' in fx else fx['source'].encode()
        r=gimport(src);ok=hashlib.sha256(src).hexdigest()==fx['sha256'] and r.returncode==fx['status'] and r.stdout==b'' and r.stderr.startswith(b'GO_IMPORT_')
        it={'id':fx['id'],'name':fx['name'],'want':fx['status'],'exit':r.returncode,'stderr':r.stderr.decode(errors='replace').strip()}
        if fx.get('go_build') in ('accept','reject'):
            acc,err=O.gobare(src);it['go_build']='accept' if acc else 'reject'
            if not acc:it['go_error']=err.splitlines()[-1][:200] if err else ''
            ok=ok and it['go_build']==fx['go_build']
        it['result']='PASS' if ok else 'FAIL'
        if not ok:failures.append(it)
        refusals.append(it)

    # ---- untyped-constant rule on seeded random trees (go-u64-v1 section 1.1) ----
    crow={'trees':0,'profile_accepted':0,'profile_refused':0,'go_accepts_profile_refused':0,'go_rejects':0,'exporter_refusals_67':0,'failures':0}
    seen=set()
    while crow['trees']<a.random_trees:
        t=rtree(rng,rng.randint(1,4))
        gsrc=H+b'func calc(a uint64, b uint64) uint64 { return '+text(t,'go').encode()+b' }\n'
        if gsrc in seen:continue
        seen.add(gsrc);crow['trees']+=1
        it={'source':gsrc.decode()}
        try:
            pok,gok=profile_ok(t),go_ok(t)
            check(gok or not pok,'rule model: profile-accepted tree outside the Go rule')
            r=gimport(gsrc)
            if pok:
                check(r.returncode==0,'profile-accepted tree refused rc=%d'%r.returncode);crow['profile_accepted']+=1
                _,_,ops=decode(r.stdout,'Go');vec=vectors(2,rng)[:24]
                ref=[tval(t,v) for v in vec];check([evaluate(ops,v) for v in vec]==ref,'imported records != tree')
                check(O.values('go',gsrc,'calc',vec)==ref,'go build/run of a profile-accepted tree != modulo-2^64 tree')
            else:
                check(r.returncode==68 and r.stderr==b'GO_IMPORT_UNSUPPORTED_ARITHMETIC\n','profile-refused tree rc=%d'%r.returncode);crow['profile_refused']+=1
                acc,_=O.gobare(gsrc);check(acc==gok,'go build verdict %s != modelled Go rule %s'%(acc,gok))
                crow['go_accepts_profile_refused' if acc else 'go_rejects']+=1
            c=run([str(cimp)],b'#include <stdint.h>\nuint64_t calc(uint64_t a, uint64_t b) { return '+text(t,'c').encode()+b'; }\n');bump('c_import_runs')
            check(c.returncode==0,'C import of the tree refused')
            x=gexport(c.stdout)
            if pok:check(x.returncode==0,'exporter refused a profile-accepted tree');csem,_=split(c.stdout);reimport_identity(csem,x.stdout,'tree')
            else:check(x.returncode==67 and not x.stdout and x.stderr==b'GO_EXPORT_LIMIT\n','exporter must refuse 67, got %d'%x.returncode);crow['exporter_refusals_67']+=1
        except (Fail,ValueError,struct.error,subprocess.TimeoutExpired) as e:
            it.update(result='FAIL',failure=str(e));failures.append(it);crow['failures']+=1

    # ---- import boundary controls (exact and over capacity), each accepted one exported and re-imported ----
    head=H+b'func calc(a uint64, b uint64) uint64 { return '
    base=head+b'a + b }'
    chain=lambda k:head+b'a'+b' + a'*k+b' }'
    parens=lambda k:head+b'('*k+b'a'+b')'*k+b' }'
    controls=[('exact-source-capacity',base+b' '*(4096-len(base)),0,12),('comment-at-capacity',base+b'// '+b'x'*(4096-len(base)-3),0,12),
              ('operation-capacity',chain(60),0,61*5),('nesting-limit',parens(16),0,5),
              ('over-source-capacity',base+b' '*(4097-len(base)),67,None),('over-operation-capacity',chain(61),67,None),
              ('over-nesting-limit',parens(17),67,None)]
    ctl=[]
    for name,src,want,value in controls:
        it={'name':name,'source_bytes':len(src)}
        try:
            r=gimport(src)
            if want:check(r.returncode==want and not r.stdout and r.stderr==b'GO_IMPORT_LIMIT\n','%s rc=%d'%(name,r.returncode))
            else:
                check(r.returncode==0,'%s refused rc=%d %s'%(name,r.returncode,r.stderr));sem,side,ops=decode(r.stdout,'Go');sidecar(sem,side,src,len(ops))
                check(evaluate(ops,[5,7])==value,name+' value');codec_checks(r.stdout,sem,src);exported=export_checks(sem,r.stdout,name)
                sem_b,_=reimport_identity(sem,exported,name);check(sem_b==sem,name+' round trip')
                vec=vectors(2,rng);ref=[evaluate(ops,v) for v in vec]
                check(O.values('go',src,'calc',vec)==ref and O.values('go',exported,'calc',vec)==ref,name+' go differential')
                it.update(ops=len(ops),exported_bytes=len(exported))
            it.update(result='PASS',exit=r.returncode)
        except (Fail,ValueError,struct.error) as e:it.update(result='FAIL',failure=str(e));failures.append(it)
        ctl.append(it)
    r=subprocess.run([str(ROOT/'build/L741/go-import')],stdin=subprocess.DEVNULL,capture_output=True,preexec_fn=lambda:os.close(0),timeout=10)
    ctl.append({'name':'import-closed-stdin','exit':r.returncode,'result':'PASS' if r.returncode==77 and not r.stdout and r.stderr==b'GO_IMPORT_IO\n' else 'FAIL'})
    r=subprocess.run([str(ROOT/'build/L741/go-export')],stdin=subprocess.DEVNULL,capture_output=True,preexec_fn=lambda:os.close(0),timeout=10)
    ctl.append({'name':'export-closed-stdin','exit':r.returncode,'result':'PASS' if r.returncode==77 and not r.stdout and r.stderr==b'GO_EXPORT_IO\n' else 'FAIL'})
    for tool in (IMPORT,EXPORT):
        r=run([sys.executable,str(tool),'unexpected'],base);ctl.append({'name':tool.name+'-arguments-refused','exit':r.returncode,'result':'PASS' if r.returncode==64 and not r.stdout and b'BAD_COMMAND' in r.stderr else 'FAIL'})
    r=run([sys.executable,str(IMPORT)],base);d=run([str(ROOT/'build/L741/go-import')],base)
    ctl.append({'name':'public-import-identical-to-native','result':'PASS' if r.returncode==0 and r.stdout==d.stdout else 'FAIL'})
    closure=['src/source-aware/vendor/sha256.om','src/source-aware/core-v2.om','src/adapters/go-u64-import.om','src/adapters/go-u64-export.om','build/L741/build.json','build/L741/go-import','build/L741/go-export']
    for tool,victims in ((IMPORT,['src/adapters/go-u64-import.om','src/source-aware/vendor/sha256.om','build/L741/go-import']),(EXPORT,['src/adapters/go-u64-export.om','src/source-aware/core-v2.om','build/L741/go-export'])):
        for victim in victims:
            with tempfile.TemporaryDirectory(prefix='closed-build-',dir=out) as td:
                t=Path(td)
                for rel in ['tools/adapters/'+tool.name]+closure:
                    p=t/rel;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/rel,p);p.chmod(0o700)
                with (t/victim).open('ab') as f:f.write(b'changed')
                r=run([sys.executable,str(t/'tools/adapters'/tool.name)],base)
                ok=r.returncode==78 and not r.stdout and b'STALE_OR_MISSING_BUILD' in r.stderr
                ctl.append({'name':tool.name+'-stale-'+victim,'exit':r.returncode,'result':'PASS' if ok else 'FAIL'})
    failures+=[c for c in ctl if c['result']=='FAIL' and c not in failures]

    # ---- exporter refusals and accepted exporter boundaries ----
    good=gimport(base).stdout
    def unit(ver,lang,prof,runtime,abi,ops,typ=1):
        sem=b'OMSA'+bytes([ver,lang,prof,1,runtime,abi])+struct.pack('<H',len(ops))+CONTRACT
        for op,attr,eff,alias,obl,a_,b_,imm in ops:sem+=struct.pack('<8BHHQ',lang,1,op,typ,attr,eff,alias,obl,a_,b_,imm)
        return semonly(sem)
    U=65535
    def shared(k):  # t_j = add(t_{j-1}, t_{j-1}): tree size 2^k, depth k
        return unit(2,7,9,0,1,[(9,0,0,0,1,U,U,0)]+[(2,1,0,0,1,j,j,0) for j in range(k)])
    bigint=unit(1,3,3,1,1,[(9,0,0,0,1,U,U,0),(9,0,0,0,1,U,U,1),(2,2,20,255,8,0,1,0)],typ=2)
    divc=unit(1,1,1,0,1,[(9,0,0,0,1,U,U,0),(9,0,0,0,1,U,U,1),(5,1,0,0,3,0,1,0)])
    divgo=unit(2,7,9,0,1,[(9,0,0,0,1,U,U,0),(9,0,0,0,1,U,U,1),(5,1,0,0,3,0,1,0)])
    gap=unit(2,7,9,0,1,[(9,0,0,0,1,U,U,1)])
    v1go=unit(1,7,9,0,1,[(9,0,0,0,1,U,U,0)])
    v3=bytearray(good);v3[17]=3
    flipped=bytearray(good);flipped[-3]^=1
    deep=[(9,0,0,0,1,U,U,0)]
    for j in range(18):deep.append((2,1,0,0,1,0,j,0))  # i0 + (i0 + (...)): 18 adds = parenthesis depth 17
    cneg=unit(1,1,1,0,1,[(9,0,0,0,1,U,U,0),(1,1,0,0,0,U,U,0),(1,1,0,0,0,U,U,1),(3,1,0,0,1,1,2,0),(2,1,0,0,1,0,3,0)])
    cdead=unit(1,1,1,0,1,[(9,0,0,0,1,U,U,0),(1,1,0,0,0,U,U,0),(1,1,0,0,0,U,U,1),(3,1,0,0,1,1,2,0),(4,1,0,0,1,0,0,0)])
    xr=[('truncated',good[:-1],65),('bad-magic',b'X'+good[1:],65),('sidecar-source-mismatch',bytes(flipped),65),('oversize',good+b'\0'*(8193-len(good)),65),
        ('unknown-version-3',bytes(v3),65),('profile3-bigint',bigint,68),('c-div-op5',divc,68),('go-div-op5',divgo,68),('slot-gap',gap,68),
        ('go-profile-version-1',v1go,68),('language-profile-mismatch',bytes(good[:18])+b'\x01'+good[19:],68),
        ('shared-ops-over-capacity',shared(6),67),('depth-over-limit',unit(2,7,9,0,1,deep),67),('size-over-limit',shared(9),67),
        ('constant-subtree-negative',cneg,67)]
    xrows=[]
    for name,env,want in xr:
        if have_sa2 and name in ('profile3-bigint','c-div-op5','go-div-op5','constant-subtree-negative'):check(sa2('decode',env).returncode==0,name+' control is not a valid unit')
        r=gexport(env);ok=r.returncode==want and not r.stdout and r.stderr.startswith(b'GO_EXPORT_')
        it={'name':name,'want':want,'exit':r.returncode,'stderr':r.stderr.decode(errors='replace').strip(),'result':'PASS' if ok else 'FAIL'}
        if not ok:failures.append(it)
        xrows.append(it)
    exact=[(9,0,0,0,1,U,U,0)]+[(2,1,0,0,1,j,j,0) for j in range(5)]+[(2,1,0,0,1,5,4,0),(2,1,0,0,1,6,3,0),(2,1,0,0,1,7,2,0),(2,1,0,0,1,8,1,0)]
    for name,env,nops in (('shared-ops-at-capacity',unit(2,7,9,0,1,exact),64),('depth-at-limit',unit(2,7,9,0,1,deep[:18]),20),('unreachable-negative-constant',cdead,4)):
        it={'name':name}
        try:
            sem,_=split(env);ops=[(o[2],o[8],o[9],o[10]) for o in ops_of(sem)];r=gexport(env);check(r.returncode==0,name+' refused rc=%d'%r.returncode)
            check(O.gofmt_clean(r.stdout),name+' export is not gofmt-clean')
            q=gimport(r.stdout);check(q.returncode==0,name+' re-import refused');_,_,ops2=decode(q.stdout,'Go')
            check(len(ops2)==nops,name+' re-import operation count %d'%len(ops2))
            vec=vectors(1,rng);ref=[evaluate(ops,v) for v in vec]
            check([evaluate(ops2,v) for v in vec]==ref and O.values('go',r.stdout,'calc',vec)==ref,name+' evaluation')
            it.update(result='PASS',reimport_ops=len(ops2),identity_equal=N(strip(split(q.stdout)[0]))==N(strip(sem)))
        except (Fail,ValueError,struct.error) as e:it.update(result='FAIL',failure=str(e));failures.append(it)
        xrows.append(it)

    # ---- random SSA units of every member (shared results, unused inputs, slot permutations) ----
    def model(ops,root,ni):
        """Exporter limits of go-u64-v1 section 4 on the expanded tree: (re-import ops, paren depth, constants ok)."""
        pr=[];dp=[];cn=[];kv=[]
        for op,x,y,imm in ops:
            if op in (1,9):pr.append(6);dp.append(0);cn.append(int(op==1));kv.append(imm if op==1 else None);continue
            p=5 if op==4 else 4;pa=int(pr[x]<p);pb=int(pr[y]<=p)
            pr.append(p);dp.append(max(dp[x]+pa,dp[y]+pb));cn.append(cn[x]+cn[y]+1)
            kv.append(None if kv[x] is None or kv[y] is None else kv[x]+kv[y] if op==2 else kv[x]-kv[y] if op==3 else kv[x]*kv[y])
        seen=set();stack=[root];cok=True
        while stack:
            k=stack.pop()
            if k in seen:continue
            seen.add(k)
            if kv[k] is not None and not 0<=kv[k]<=MASK:cok=False
            if ops[k][0] in (2,3,4):stack+=[ops[k][1],ops[k][2]]
        return ni+cn[root]+2,dp[root],cok
    drow={'units':0,'exported':0,'refused_67':0,'go_checked':0,'failures':0}
    for k in range(80):
        m=rng.choice(list(MEMBERS));ver,lang,prof,arob=MEMBERS[m];ni=rng.randint(0,6);slots=list(range(ni));rng.shuffle(slots)
        ops=[(9,U,U,s) for s in slots];recs=[(9,0,0,0,1,U,U,s) for s in slots]
        for j in range(rng.randint(1,22)):
            if not ops or rng.random()<0.2:
                v=rng.choice(CONSTS) if rng.random()<0.7 else rng.getrandbits(64);ops.append((1,U,U,v));recs.append((1,1,0,0,0,U,U,v))
            else:
                op=rng.choice((2,3,4));x=rng.randrange(len(ops));y=rng.randrange(len(ops))
                if rng.random()<0.5:x=len(ops)-1
                ops.append((op,x,y,0));recs.append((op,1,0,0,arob,x,y,0))
        if len(ops)>64:continue
        drow['units']+=1;env=unit(ver,lang,prof,0,1,recs);sem,_=split(env);it={'member':m,'ops':len(ops)}
        try:
            root=len(ops)-1
            if normalized(ops):root=ops[-1][1]
            nops,depth,cok=model(ops,root,ni);ok=nops<=64 and depth<=16 and cok
            r=gexport(env)
            if not ok:check(r.returncode==67 and not r.stdout,'model predicts 67 (ops %d depth %d constants %s), got %d'%(nops,depth,cok,r.returncode));drow['refused_67']+=1;continue
            check(r.returncode==0,'model predicts export, got %d %s'%(r.returncode,r.stderr));drow['exported']+=1
            check(O.gofmt_clean(r.stdout),'random unit export is not gofmt-clean')
            q=gimport(r.stdout);check(q.returncode==0,'re-import refused %d'%q.returncode);_,_,ops2=decode(q.stdout,'Go')
            check(len(ops2)==nops,'re-import operation count %d != model %d'%(len(ops2),nops))
            vec=vectors(ni,rng)[:20];ref=[evaluate(ops,v) for v in vec];check([evaluate(ops2,v) for v in vec]==ref,'re-import evaluation differs')
            if drow['go_checked']<16:check(O.values('go',r.stdout,'calc',vec)==ref,'go(export) != reference');drow['go_checked']+=1
        except (Fail,ValueError,struct.error) as e:it.update(result='FAIL',failure=str(e));failures.append(it);drow['failures']+=1

    # ---- composition C -> Go: F1 positives and the four accepted C import boundary controls ----
    ch=b'#include <stdint.h>\nuint64_t calc(uint64_t a,uint64_t b){return a+b;}'
    cctl=[('C-exact-source-capacity',ch+b' '*(4096-len(ch)),'calc',2),('C-comment-at-capacity',ch+b'//'+b'x'*(4096-len(ch)-2),'calc',2),
          ('C-operation-capacity',b'#include <stdint.h>\nuint64_t calc(uint64_t a,uint64_t b){return '+b'+'.join([b'a']*61)+b';}','calc',2),
          ('C-nesting-limit',b'#include <stdint.h>\nuint64_t calc(uint64_t a,uint64_t b){return '+b'('*16+b'a'+b')'*16+b';}','calc',2)]
    comp=[]
    for tag,csrc,name,nargs in [(p['id'],p['source'].encode(),p['name'],p['inputs']) for p in f1['positives']]+cctl:
        it={'id':'C->Go '+tag}
        try:
            c=run([str(cimp)],csrc);bump('c_import_runs');check(c.returncode==0,'C import refused')
            sem_a,_,ops_a=decode(c.stdout,'C')
            src_b=export_checks(sem_a,c.stdout,tag)
            sem_b,ops_b=reimport_identity(sem_a,src_b,tag)
            vec=vectors(nargs,rng);ref=[evaluate(ops_a,v) for v in vec]
            check(O.values('c',csrc,name,vec)==ref,'cc(original C) != reference(U_A)')
            check(O.values('go',src_b,'calc',vec)==ref,'go(src_B) != reference(U_A)')
            check([evaluate(ops_b,v) for v in vec]==ref,'reference(U_B) != reference(U_A)')
            if native:check(native.values(N(sem_a),vec)==ref,'native lower(N(U_A)) != reference(U_A)')
            it.update(result='PASS',identity=identity(N(strip(sem_a))),vectors=len(vec),native_realization='PASS' if native else native_state)
        except (Fail,ValueError,struct.error) as e:it.update(result='FAIL',failure=str(e));failures.append(it)
        comp.append(it)

    # ---- composition Go -> C: R(U, C) in test code (retarget not landed), then the #622 C exporter ----
    gc_rows=[];go_to_c='NOT_RUN (no c-export)'
    if cexp:
        go_to_c='RUN'
        for fx in f2['positives']:
            it={'id':'Go->C '+fx['id']}
            try:
                src=fx['source'].encode();r=gimport(src);check(r.returncode==0,'Go import refused');sem_a,_,ops_a=decode(r.stdout,'Go')
                q=py(cexp,semonly(retarget(sem_a,'C')));bump('c_export_runs');check(q.returncode==0,'c-export refused R(U,C) rc=%d %s'%(q.returncode,q.stderr))
                q2=py(cexp,semonly(sem_a));check(q2.returncode!=0 and not q2.stdout,'v1-core c-export accepted a v2 unit');bump('c_export_v2_refusal_status_%d'%q2.returncode)
                c=run([str(cimp)],q.stdout);bump('c_import_runs');check(c.returncode==0,'C import of exported C refused')
                sem_b,_,ops_b=decode(c.stdout,'C');check(N(strip(sem_b))==N(strip(sem_a)),'identity N(strip(U_B)) != N(strip(U_A))')
                vec=vectors(fx['inputs'],rng);ref=[evaluate(ops_a,v) for v in vec]
                check(O.values('go',src,fx['name'],vec)==ref,'go(original Go) != reference')
                check(O.values('c',q.stdout,'calc',vec)==ref,'cc(src_B) != reference')
                check([evaluate(ops_b,v) for v in vec]==ref,'reference(U_B) != reference(U_A)')
                if native:check(native.values(N(sem_a),vec)==ref,'native lower(N(U_A)) != reference(U_A)')
                it.update(result='PASS',vectors=len(vec),exported_c=q.stdout.decode(),native_realization='PASS' if native else native_state)
            except (Fail,ValueError,struct.error) as e:it.update(result='FAIL',failure=str(e));failures.append(it)
            gc_rows.append(it)

    def ver(cmd):
        try:return subprocess.check_output(cmd,text=True,env=O.env).strip()
        except (OSError,subprocess.CalledProcessError) as e:return 'unavailable: '+str(e)
    report={'schema':'omega.go-u64.selftest.v1','result':'FAIL' if failures else 'PASS','status_flags':'TESTED (author self-test), not QUALIFIED',
            'evidence':'REFERENCE + DIFFERENTIAL (go/cc NATIVE AArch64%s); no PROOF'%(', Omega native realization' if native else ''),
            'source_commit':subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip(),
            'build':json.loads((ROOT/'build/L741/build.json').read_text()),'go':ver([a.go,'version']),'go_sha256':hashlib.sha256(Path(a.go).read_bytes()).hexdigest(),
            'gofmt_sha256':hashlib.sha256(Path(a.gofmt).read_bytes()).hexdigest(),'cc':ver([a.cc,'--version']).splitlines()[0],
            'c_import_sha256':hashlib.sha256(cimp.read_bytes()).hexdigest(),'sa1_sha256':hashlib.sha256(sa1.read_bytes()).hexdigest(),
            'codec_v2':codec_v2,'sa2_sha256':hashlib.sha256((sa2root/'build/L832/sa').read_bytes()).hexdigest() if have_sa2 else None,
            'c_export':str(cexp) if cexp else None,'native_realization':native_state,
            'positives':len(rows),'positives_passed':sum(r['result']=='PASS' for r in rows),'not_in_profile':len(f2['not_in_profile']),
            'extra_positives':len(extra_rows),'extra_positives_passed':sum(r['result']=='PASS' for r in extra_rows),
            'refusals':len(refusals),'refusals_passed':sum(r['result']=='PASS' for r in refusals),
            'refusals_go_checked':sum('go_build' in r for r in refusals),'constant_rule':crow,'random_units':drow,
            'controls':len(ctl),'controls_passed':sum(r['result']=='PASS' for r in ctl),'export_cases':len(xrows),'export_cases_passed':sum(r['result']=='PASS' for r in xrows),
            'c_to_go':len(comp),'c_to_go_passed':sum(r['result']=='PASS' for r in comp),'go_to_c':go_to_c,'go_to_c_cases':len(gc_rows),'go_to_c_passed':sum(r['result']=='PASS' for r in gc_rows),
            'native_calls':native.calls if native else 0,'oracle_compiles':O.compiles,'go_builds':O.go_builds,'gofmt_runs':O.gofmt_runs,'counts':counts,
            'cases':rows,'extra_cases':extra_rows,'refusal_cases':refusals,'control_cases':ctl,'export_rows':xrows,'c_to_go_cases':comp,'go_to_c_rows':gc_rows,'failures':failures}
    (out/'result.json').write_text(json.dumps(report,indent=1)+'\n')
    keys=['positives','positives_passed','not_in_profile','extra_positives','extra_positives_passed','refusals','refusals_passed','refusals_go_checked','controls','controls_passed',
          'export_cases','export_cases_passed','c_to_go','c_to_go_passed','go_to_c','go_to_c_cases','go_to_c_passed','codec_v2','native_realization','native_calls','oracle_compiles','go_builds','gofmt_runs']
    print(report['result'],' '.join('%s=%s'%(k,report[k]) for k in keys),'constant_rule='+json.dumps(crow,separators=(',',':')),'random_units='+json.dumps(drow,separators=(',',':')),' '.join('%s=%s'%kv for kv in counts.items()))
    for f in failures[:20]:print('FAIL',json.dumps(f)[:600])
    return 1 if failures else 0

if __name__=='__main__':raise SystemExit(main())
