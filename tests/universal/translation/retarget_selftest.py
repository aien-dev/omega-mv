#!/usr/bin/env python3
"""Retarget lane self-test (TESTED evidence for translation-compose-v2 section 2, not qualification).

For every F1 positive and accepted c_import_controls boundary (C imports) and every member M of the
extended S1 family, tools/adapters/retarget M must give exactly the R(U, M) bytes rebuilt here from the
records; R must compose (R(R(U, A), B) == R(U, B), so N(R(U, M)) == N(U)); R(U, c) of a C import is
the import with its sidecar stripped; every output decodes unchanged under the v2 codec build/L832/sa;
the frozen v1 sa accepts the v1 members and refuses v2 members with 66; lower(R(U, c)) == lower(U).
Refusals check status, empty stdout and the named stderr line. Tools run as separate processes, one at
a time. A check whose tool is missing is NOT_RUN, never PASS, and the run exits 2.
"""
from pathlib import Path
import argparse,hashlib,json,os,struct,subprocess,sys
ROOT=Path(__file__).resolve().parents[3]
MASK=(1<<64)-1
MEMBERS=['c','rust','python','cpp','zig','go']
ROW={'c':(1,1,1,1),'rust':(1,2,2,1),'python':(1,3,4,5),'cpp':(2,5,7,1),'zig':(2,6,8,1),'go':(2,7,9,1)}
CONTRACT=hashlib.sha256((ROOT/'spec/universal/semantics.md').read_bytes()).digest()
RETARGET=[sys.executable,str(ROOT/'tools/adapters/retarget')]
LOWER=[sys.executable,str(ROOT/'tools/adapters/lower')]
IMPORT=[sys.executable,str(ROOT/'tools/adapters/c-import')]
NAMES={64:'BAD_COMMAND',65:'MALFORMED',66:'UNKNOWN_VERSION',67:'LIMIT',68:'PROFILE_MISMATCH',69:'UNKNOWN_OPERATION',
       70:'TYPE_MISMATCH',71:'EFFECT_ALIAS_MISMATCH',72:'OBLIGATION_MISMATCH',74:'PROVENANCE_MISMATCH',78:'CONTRACT_MISMATCH'}
fails=[];notrun=set()

def check(ok,what):
    if not ok:
        fails.append(what);print('FAIL',what,flush=True)
    return ok

def run(cmd,data,timeout=60):
    r=subprocess.run(cmd,input=data,capture_output=True,timeout=timeout)
    return r.returncode,r.stdout,r.stderr

def expected_r(env,m):
    """Independent R(U, M): header version/language/profile, op dialects, arithmetic obligations; no sidecar."""
    ns=struct.unpack_from('<I',env,5)[0];s=env[13:13+ns]
    count=struct.unpack_from('<H',s,10)[0];ver,lang,prof,obl=ROW[m]
    sem=b'OMSA'+bytes([ver,lang,prof,1,0,1])+struct.pack('<H',count)+s[12:44]
    for i in range(count):
        d,v,op,t,attr,eff,al,ob,a,b,imm=struct.unpack_from('<8BHHQ',s,44+20*i)
        sem+=struct.pack('<8BHHQ',lang,v,op,t,attr,eff,al,obl if op in (2,3,4) else ob,a,b,imm)
    return b'OMSB\x01'+struct.pack('<II',len(sem),0)+sem

def semantic_only(env):
    ns=struct.unpack_from('<I',env,5)[0]
    return env[:9]+b'\0\0\0\0'+env[13:13+ns]

U=65535
def envelope(ops,ver=1,lang=1,prof=1,runtime=0,abi=1,contract=CONTRACT,count=None):
    sem=b'OMSA'+bytes([ver,lang,prof,1,runtime,abi])+struct.pack('<H',len(ops) if count is None else count)+contract
    for o in ops:sem+=struct.pack('<8BHHQ',*o)
    return b'OMSB\x01'+struct.pack('<II',len(sem),0)+sem
def inp(s,d=1,t=1,obl=1):return (d,1,9,t,0,0,0,obl,U,U,s)
def const(v,d=1,t=1,attr=1,eff=0):return (d,1,1,t,attr,eff,0,0,U,U,v)
def arith(op,a,b,d=1,obl=1,t=1,attr=1,eff=0,al=0):return (d,1,op,t,attr,eff,al,obl,a,b,0)

class Codec:
    """One sa binary: first byte = command (2 decode), then the envelope."""
    def __init__(self,path,tag):
        self.path=Path(path);self.tag=tag;self.ok=self.path.is_file() and os.access(self.path,os.X_OK)
        self.sha=hashlib.sha256(self.path.read_bytes()).hexdigest() if self.ok else None
        if not self.ok:notrun.add(f'{tag} ({path} missing)');print('NOT_RUN',tag,path,flush=True)
    def decode(self,env):return run([str(self.path)],b'\x02'+env)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--out',required=True)
    ap.add_argument('--v2-sa',default=str(ROOT/'build/L832/sa'))
    ap.add_argument('--v1-sa',default=str(Path.home()/'workspace/universal-read/build/L574/sa'))
    a=ap.parse_args();out=ROOT/a.out;out.mkdir(parents=True)
    v2=Codec(a.v2_sa,'v2-codec-decode');v1=Codec(a.v1_sa,'frozen-v1-codec')
    f1=json.loads((ROOT/'tests/universal/translation/fixtures-f1.json').read_text())
    cases=[]
    for p in f1['positives']:
        src=p['source'].encode()
        check(hashlib.sha256(src).hexdigest()==p['sha256'],p['id']+': fixture sha256')
        cases.append((p['id'],src,None if p['name']=='calc' else p['name']))
    header='#include <stdint.h>\n'
    base=(header+'uint64_t calc(uint64_t a,uint64_t b){return a+b;}').encode()
    cases+=[('C-exact-source-capacity',base+b' '*(4096-len(base)),None),
            ('C-comment-at-capacity',base+b'//'+b'x'*(4096-len(base)-2),None),
            ('C-operation-capacity',(header+'uint64_t calc(uint64_t a,uint64_t b){return '+'+'.join(['a']*61)+';}').encode(),None),
            ('C-nesting-limit',(header+'uint64_t calc(uint64_t a,uint64_t b){return '+'('*16+'a'+')'*16+';}').encode(),None)]
    c={'fixtures':0,'retargets':0,'compositions':0,'v2_decodes':0,'v1_accepts':0,'v1_refusals_66':0,'lower_equal':0}
    p05env=None
    for tag,src,name in cases:
        rc,env,err=run(IMPORT,src)
        if not check(rc==0,f'{tag}: import rc={rc} {err.decode().strip()}'):continue
        if tag=='F1-P05':p05env=env
        outs={}
        for m in MEMBERS:
            rc,o,err=run(RETARGET+[m],env)
            if not check(rc==0 and err==b'',f'{tag} -> {m}: retarget rc={rc} {err.decode().strip()}'):continue
            check(o==expected_r(env,m),f'{tag} -> {m}: output differs from R(U, {m})')
            rc2,o2,_=run(RETARGET+[m],env);check(rc2==0 and o2==o,f'{tag} -> {m}: not byte-deterministic')
            outs[m]=o;c['retargets']+=1
            if tag in ('F1-P05','C-operation-capacity'):(out/f'{tag}.{m}.omsb').write_bytes(o)
        if len(outs)!=6:continue
        check(outs['c']==semantic_only(env),f'{tag}: R(U, c) is not the C import with its sidecar stripped')
        for m,o in outs.items():
            for m2 in MEMBERS:
                rc,o3,err=run(RETARGET+[m2],o)
                if check(rc==0 and o3==outs[m2],f'{tag}: R(R(U, {m}), {m2}) != R(U, {m2}) rc={rc} {err.decode().strip()}'):c['compositions']+=1
            if v2.ok:
                rc,d,err=v2.decode(o)
                if check(rc==0 and d==o,f'{tag} -> {m}: v2 codec decode rc={rc} {err.decode().strip()}'):c['v2_decodes']+=1
            if v1.ok:
                rc,d,err=v1.decode(o)
                if ROW[m][0]==1:
                    if check(rc==0 and d==o,f'{tag} -> {m}: frozen v1 codec decode rc={rc}'):c['v1_accepts']+=1
                elif check(rc==66 and d==b'',f'{tag} -> {m}: frozen v1 codec gave rc={rc}, want 66'):c['v1_refusals_66']+=1
        if tag.startswith('F1-'):
            cmd=LOWER+([name] if name else [])
            rc,l1,e1=run(cmd,env);rc2,l2,e2=run(cmd,outs['c'])
            if check(rc==0 and rc2==0 and l1==l2,f'{tag}: lower(R(U, c)) != lower(U) rc={rc}/{rc2} {e1.decode().strip()}{e2.decode().strip()}'):c['lower_equal']+=1
        c['fixtures']+=1
    # Refusals. Decoder statuses follow source-aware-v2.md section 5 decode order.
    def flip(e,off,val):b=bytearray(e);b[off]=val;return bytes(b)
    so=semantic_only(p05env)
    cpp=lambda ops,**k:envelope(ops,ver=2,lang=5,prof=7,**k)
    refusals=[('no-argument',p05env,[],64),('two-arguments',p05env,['c','go'],64),('upper-case-C',p05env,['C'],64),
              ('unknown-member',p05env,['cxx'],64),('empty-member',p05env,[''],64),('prefix-member',p05env,['pyth'],64),
              ('over-16-byte-member',p05env,['python'*3],64),
              ('empty-input',b'',['c'],65),('truncated-envelope',p05env[:-1],['c'],65),('bad-magic',flip(p05env,0,0),['c'],65),
              ('trailing-byte',p05env+b'\0',['c'],65),('envelope-version-2',flip(p05env,4,2),['c'],66),
              ('semantic-version-3',flip(so,17,3),['c'],66),('over-8192-bytes',p05env+b'\0'*(8193-len(p05env)),['c'],67),
              ('zero-operations',envelope([],count=0),['c'],67),('wrong-semantics-hash',envelope([const(1)],contract=b'\0'*32),['c'],78),
              ('opcode-10',envelope([inp(0),(1,1,10,1,1,0,0,1,0,0,0)]),['c'],69),('forward-operand',envelope([inp(0),arith(2,0,2),const(1)]),['c'],70),
              ('const-effects',envelope([const(1,eff=1)]),['c'],71),
              ('python-arith-obligation-1',envelope([inp(0,3),inp(1,3),arith(2,0,1,d=3,obl=1)],lang=3,prof=4),['c'],72),
              ('sidecar-source-hash',flip(p05env,len(p05env)-2,0x41),['c'],74),
              ('c-div-op5',envelope([inp(0),inp(1),arith(5,0,1,obl=3)]),['go'],68),
              ('cpp-div-op5',cpp([inp(0,5),inp(1,5),arith(5,0,1,d=5,obl=3)]),['c'],68),
              ('version-2-profile-1',flip(so,17,2),['c'],68),('version-1-profile-7',flip(cpp([inp(0,5)]),17,1),['c'],68),
              ('profile-3-bigint',envelope([inp(0,3,t=2,obl=1),const(5,d=3,t=2,attr=2)],lang=3,prof=3,runtime=1),['c'],68),
              ('profile-5-osc',envelope([inp(0,4),inp(1,4),arith(2,0,1,d=4)],lang=4,prof=5),['c'],68),
              ('profile-6-dynamic',envelope([inp(0,3)],lang=3,prof=6,runtime=1,abi=2),['c'],68),
              ('input-slot-gap',envelope([inp(1),const(3),arith(2,0,1)]),['c'],68)]
    refused=0
    for tag,data,argv,want in refusals:
        rc,o,err=run(RETARGET+argv,data)
        if check(rc==want and o==b'' and err==f'RETARGET_{NAMES[want]}\n'.encode(),f'refusal {tag}: rc={rc} want {want} stdout={len(o)} stderr={err!r}'):refused+=1
    c['refusals']=refused;c['refusal_cases']=len(refusals)
    report={'schema':'omega.upper-retarget.selftest.v1','evidence':'TESTED (lane self-test), byte identity, NATIVE',
            'v2_sa_sha256':v2.sha,'v1_sa_sha256':v1.sha,'counts':c,'not_run':sorted(notrun),'failures':fails}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    summary=' '.join(f'{k}={v}' for k,v in c.items() if k!='refusal_cases')+f'/{len(refusals)}'
    if fails:
        print(f'FAIL retarget selftest: {len(fails)} failures; {summary}');return 1
    if notrun:
        print(f'INCOMPLETE retarget selftest (no failures) {summary}; NOT_RUN: {"; ".join(sorted(notrun))}');return 2
    print(f'PASS retarget selftest {summary}');return 0

if __name__=='__main__':raise SystemExit(main())
