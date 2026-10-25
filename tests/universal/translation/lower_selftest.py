#!/usr/bin/env python3
"""Lowering lane self-test (TESTED evidence for translation-v1 section 5, not qualification).

Every F1 positive, the accepted c_import_controls boundaries and crafted semantic-only S1 units go
upper envelope -> tools/adapters/lower -> pinned omc 734f0a39 (function mode) -> pinned omega-bridge
native.om 012860db binding -> C caller, and every native result is compared with a Python bigint
evaluation of the upper records. Expected unit bytes are rebuilt here from the records and compared
exactly. Refusals check status, empty stdout and the named stderr message. Tools run as separate
processes, one at a time.
"""
from pathlib import Path
import argparse,hashlib,json,os,random,struct,subprocess,sys
ROOT=Path(__file__).resolve().parents[3]
MASK=(1<<64)-1
OMC_PIN='734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61'
BRIDGE_COMMIT='012860dbf26ee62a9838594b9dfd1e3e94a540a9'
NATIVE_OM_PIN='2c2c53f61d4ab302f1a2092c3c32189335dd53f6cf2b388f47a475f99f2c3940'
BOUNDARY=[0,1,2,(1<<32)-1,1<<32,(1<<63)-1,1<<63,MASK-1,MASK]
SEMDOM=b'omega.source.semantic.v1'
CONTRACT=hashlib.sha256((ROOT/'spec/universal/semantics.md').read_bytes()).digest()
LOWER=[sys.executable,str(ROOT/'tools/adapters/lower')]
IMPORT=[sys.executable,str(ROOT/'tools/adapters/c-import')]
fails=[]

def check(ok,what):
    if not ok:
        fails.append(what);print('FAIL',what,flush=True)
    return ok

def run(cmd,data,timeout=60):
    r=subprocess.run(cmd,input=data,capture_output=True,timeout=timeout)
    return r.returncode,r.stdout,r.stderr

def records(env):
    """Independent parse of the semantic bytes (structure only; the native core does validation)."""
    ns=struct.unpack_from('<I',env,5)[0];sem=env[13:13+ns]
    count=struct.unpack_from('<H',sem,10)[0]
    ops=[struct.unpack_from('<8BHHQ',sem,44+20*i) for i in range(count)]
    return sem,[(o[2],o[8],o[9],o[10]) for o in ops]

def evaluate(ops,args):
    vals=[]
    for op,a,b,imm in ops:
        v={9:lambda:args[imm],1:lambda:imm,2:lambda:vals[a]+vals[b],3:lambda:vals[a]-vals[b],4:lambda:vals[a]*vals[b]}[op]()
        vals.append(v&MASK)
    return vals[-1]

def expected_unit(sem,ops,name):
    n=sum(1 for o in ops if o[0]==9)
    sig=''.join(f' i{s} ulong' for s in range(n))
    lines=['note omega.universal.lower.v1 semantic '+hashlib.sha256(SEMDOM+sem).hexdigest(),f'fn {name}{sig} to ulong',f'v = new ulong {len(ops)} 0']
    for k,(op,a,b,imm) in enumerate(ops):
        if op==9:lines.append(f'put v {k} i{imm}')
        elif op==1:lines.append(f'put v {k} {imm}')
        else:lines.append(f'put v {k} {"add sub mul".split()[op-2]} get v {a} get v {b}')
    lines+=[f'ret get v {len(ops)-1}','end']
    return ('\n'.join(lines)+'\n').encode(),n

def envelope(ops,lang=1,prof=1,runtime=0,abi=1,contract=CONTRACT):
    """Semantic-only OMSB v1 envelope from full records (d,v,op,type,attr,effects,alias,oblig,a,b,imm)."""
    sem=b'OMSA'+bytes([1,lang,prof,1,runtime,abi])+struct.pack('<H',len(ops))+contract
    for o in ops:sem+=struct.pack('<8BHHQ',*o)
    return b'OMSB\x01'+struct.pack('<II',len(sem),0)+sem

def semantic_only(env):
    ns=struct.unpack_from('<I',env,5)[0]
    return env[:9]+b'\0\0\0\0'+env[13:13+ns]

U=65535
def inp(s,lang=1):return (lang,1,9,1,0,0,0,1,U,U,s)
def const(v,lang=1):return (lang,1,1,1,1,0,0,0,U,U,v)
def arith(op,a,b,lang=1,obl=1):return (lang,1,op,1,1,0,0,obl,a,b,0)

def vectors(n,rng):
    if n==0:return [()]
    if n<=2:
        vs=[tuple(v) for v in __import__('itertools').product(BOUNDARY,repeat=n)]
    else:
        vs=[tuple([b]*n) for b in BOUNDARY]
        for i in range(n):
            for b in BOUNDARY:
                v=[rng.getrandbits(64) for _ in range(n)];v[i]=b;vs.append(tuple(v))
    vs+=[tuple(rng.getrandbits(64) for _ in range(n)) for _ in range(32)]
    return vs

def caller_c(name,n):
    params=''.join(', uint64_t i%d'%i for i in range(n));args=''.join(', a[%d]'%i for i in range(n))
    return f'''#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
uint32_t omega_{name}(uint64_t *result{params});
int main(void)
{{
    char line[512];
    while (fgets(line, sizeof line, stdin)) {{
        char *p = line;
        uint64_t a[6] = {{0}};
        for (int i = 0; i < {n}; i++)
            a[i] = strtoull(p, &p, 10);
        (void)a;
        (void)p;
        uint64_t r = UINT64_C(6148914691236517205);
        uint32_t s = omega_{name}(&r{args});
        printf("%" PRIu32 " %" PRIu64 "\\n", s, r);
    }}
    return 0;
}}
'''

class Native:
    def __init__(self,out,omc,bridge):
        self.out=out;self.omc=omc
        check(hashlib.sha256(Path(omc).read_bytes()).hexdigest()==OMC_PIN,'omc sha256 pin')
        src=subprocess.run(['git','-C',bridge,'show',f'{BRIDGE_COMMIT}:src/native.om'],capture_output=True,check=True).stdout
        if not check(hashlib.sha256(src).hexdigest()==NATIVE_OM_PIN,'bridge native.om sha256 pin'):raise SystemExit(1)
        rc,gen,err=run([omc],src)
        if not check(rc==0,'bridge native.om compiles: '+err.decode()):raise SystemExit(1)
        self.gen=out/'native';self.gen.write_bytes(gen);self.gen.chmod(0o700)
        self.calls=0;self.vectors=0
    def realize(self,tag,unit,name,n,ops,rng):
        d=self.out/tag;d.mkdir()
        (d/'unit.om').write_bytes(unit)
        rc,code,err=run([self.omc],unit)
        if not check(rc==0 and len(code)>0,f'{tag}: omc function-mode compile rc={rc} {err.decode().strip()}'):return
        (d/'unit.bin').write_bytes(code)
        frame=unit+b'\0'+b'%08d'%len(code)+code
        rc,c,err=run([str(self.gen)],frame)
        if not check(rc==0 and c,f'{tag}: bridge binding rc={rc} {err.decode().strip()}'):return
        (d/'omega_native.c').write_bytes(c);(d/'caller.c').write_text(caller_c(name,n))
        r=subprocess.run(['cc','-std=gnu11','-O2','-Wall','-Wextra','-Werror','-pthread',str(d/'caller.c'),str(d/'omega_native.c'),'-o',str(d/'caller')],capture_output=True)
        if not check(r.returncode==0,f'{tag}: cc caller: '+r.stderr.decode()[-400:]):return
        vs=vectors(n,rng)
        rc,o,err=run([str(d/'caller')],''.join(' '.join(map(str,v))+'\n' for v in vs).encode())
        got=o.decode().split('\n')[:-1]
        if not check(rc==0 and len(got)==len(vs),f'{tag}: caller ran {len(got)}/{len(vs)} rc={rc}'):return
        for v,line in zip(vs,got):
            want=f'0 {evaluate(ops,v)}'
            check(line==want,f'{tag}: omega_{name}{v} -> {line}, reference {want}')
        self.vectors+=len(vs);self.calls+=len(got)

def lower_ok(tag,env,name,expected):
    cmd=LOWER+([name] if name is not None else [])
    rc,o,err=run(cmd,env)
    ok=check(rc==0 and err==b'',f'{tag}: lower rc={rc} {err.decode().strip()}')
    ok=ok and check(o==expected,f'{tag}: lowered unit differs from the section-5 form')
    rc2,o2,_=run(cmd,env)
    ok=ok and check(rc2==0 and o2==o,f'{tag}: lower not byte-deterministic')
    return ok

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--out',required=True)
    ap.add_argument('--omc',default=os.environ.get('OMEGA_NATIVE_OMC',str(Path.home()/'workspace/omega-seedqual-20261025/seed/out/omc')))
    ap.add_argument('--bridge',default=os.environ.get('OMEGA_BRIDGE_REPO',str(Path.home()/'workspace/omega-bridge')))
    a=ap.parse_args();out=ROOT/a.out;out.mkdir(parents=True)
    rng=random.Random(624831)
    nat=Native(out,a.omc,a.bridge)
    f1=json.loads((ROOT/'tests/universal/translation/fixtures-f1.json').read_text())
    cases=[]
    for p in f1['positives']:
        src=p['source'].encode()
        check(hashlib.sha256(src).hexdigest()==p['sha256'],p['id']+': fixture sha256')
        cases.append((p['id'],src,None if p['name']=='calc' else p['name']))
    # Accepted boundary controls, constructed exactly as tests/universal/c_import_controls.py does.
    header='#include <stdint.h>\n'
    base=(header+'uint64_t calc(uint64_t a,uint64_t b){return a+b;}').encode()
    cases+=[('C-exact-source-capacity',base+b' '*(4096-len(base)),None),
            ('C-comment-at-capacity',base+b'//'+b'x'*(4096-len(base)-2),None),
            ('C-operation-capacity',(header+'uint64_t calc(uint64_t a,uint64_t b){return '+'+'.join(['a']*61)+';}').encode(),None),
            ('C-nesting-limit',(header+'uint64_t calc(uint64_t a,uint64_t b){return '+'('*16+'a'+')'*16+';}').encode(),None)]
    p05=None;fixtures=0
    for tag,src,name in cases:
        rc,env,err=run(IMPORT,src)
        if not check(rc==0,f'{tag}: import rc={rc} {err.decode().strip()}'):continue
        sem,ops=records(env)
        uname=name or 'calc'
        exp,n=expected_unit(sem,ops,uname)
        if tag=='C-operation-capacity':check(len(ops)==64,f'{tag}: 64 upper operations, got {len(ops)}')
        if not lower_ok(tag,env,name,exp):continue
        check(lower_ok(tag+'/semantic-only',semantic_only(env),name,exp),f'{tag}: semantic-only envelope lowers identically')
        if tag=='F1-P05':p05=exp;(out/'F1-P05.om').write_bytes(exp)
        nat.realize(tag,exp,uname,n,ops,rng);fixtures+=1
    # Crafted semantic-only S1 units: input records out of slot order, lone extreme constants,
    # and a 64-result unit with six inputs in reverse slot order and every arithmetic op.
    big=[inp(s) for s in range(5,-1,-1)]+[const(0),const(MASK),const(1<<63)]
    while len(big)<64:
        k=len(big);big.append(arith(rng.choice([2,3,4]),rng.randrange(k),rng.randrange(k)))
    crafted=[('X-permuted-inputs',[inp(1),inp(0),arith(3,0,1)],'perm'),
             ('X-const-zero',[const(0)],None),('X-const-max',[const(MASK)],None),
             ('X-64-results-6-inputs',big,'six'),
             ('X-name-63',[inp(0),inp(1),arith(4,0,1)],'N'+'_'*61+'9')]
    for tag,recs,name in crafted:
        env=envelope(recs);sem,ops=records(env)
        exp,n=expected_unit(sem,ops,name or 'calc')
        if lower_ok(tag,env,name,exp):nat.realize(tag,exp,name or 'calc',n,ops,rng);fixtures+=1
    for name in ['_x','Calc','i6','mainx','vv','get_v']:
        env=envelope([inp(0),inp(1),arith(2,0,1)]);sem,ops=records(env)
        lower_ok('accept-name-'+name,env,name,expected_unit(sem,ops,name)[0])
    # Refusals: status, no stdout, named stderr.
    rc,p05env,_=run(IMPORT,f1['positives'][5]['source'].encode())
    def flip(env,off,val):b=bytearray(env);b[off]=val;return bytes(b)
    div=[inp(0),inp(1),arith(5,0,1,obl=3)]
    rust=[inp(0,2),inp(1,2),arith(2,0,1,2)]
    refusals=[('truncated-envelope',p05env[:-1],[],65),('empty-input',b'',[],65),
              ('over-8192-bytes',p05env+b'\0'*(8193-len(p05env)),[],65),('bad-magic',flip(p05env,0,0),[],65),
              ('forward-operand',envelope([inp(0),arith(2,0,2),const(1)]),[],65),
              ('add-obligation-0',envelope([inp(0),arith(2,0,0,obl=0)]),[],65),
              ('wrong-semantics-hash',envelope([const(1)],contract=b'\0'*32),[],65),
              ('sidecar-mismatch',flip(p05env,len(p05env)-2,0x41),[],65),
              ('trailing-byte',p05env+b'\0',[],65),
              ('op5-div',envelope(div),[],68),('wrong-profile-byte',flip(p05env,19,2),[],68),
              ('runtime-byte',flip(semantic_only(p05env),21,1),[],68),
              ('rust-profile-2',envelope(rust,lang=2,prof=2),[],68),
              ('input-slot-gap',envelope([inp(1),const(3),arith(2,0,1)]),[],68)]
    for bad in ['get','u64','argcnt','v','i0','i5','main','a-b','1a','','a'*64,'é']:
        refusals.append(('name-'+repr(bad),p05env,[bad],64))
    refusals.append(('two-names',p05env,['calc','x'],64))
    names={64:b'LOWER_BAD_COMMAND\n',65:b'LOWER_MALFORMED\n',68:b'LOWER_PROFILE_MISMATCH\n'}
    refused=0
    for tag,data,argv,want in refusals:
        rc,o,err=run(LOWER+argv,data)
        if check(rc==want and o==b'' and err==names[want],f'refusal {tag}: rc={rc} want {want} stdout={len(o)} stderr={err!r}'):refused+=1
    report={'schema':'omega.upper-lower.selftest.v1','evidence':'TESTED (lane self-test), DIFFERENTIAL + REFERENCE, NATIVE',
            'fixtures':fixtures,'vectors':nat.vectors,'native_calls':nat.calls,'refusals':refused,'refusal_cases':len(refusals),'failures':fails}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    if p05 is not None:print(p05.decode(),end='')
    if fails:
        print(f'FAIL lower selftest: {len(fails)} failures');return 1
    print(f'PASS lower selftest fixtures={fixtures} vectors={nat.vectors} native_calls={nat.calls} refusals={refused}/{len(refusals)}')
    return 0

if __name__=='__main__':raise SystemExit(main())
