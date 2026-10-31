#!/usr/bin/env python3
"""Independent development gates for the bounded native C expression importer."""
from pathlib import Path
import argparse,hashlib,json,os,random,struct,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
HEADER='#include <stdint.h>\n'
EXPRESSIONS=['a','b','a+b','a-b','a*b','(a+b)*(a-b)','a+0ULL','a-1ULL','a*2ULL','0ULL-a','18446744073709551615ULL+a','a*b+3ULL','a+a+b','a+(a+b)','a-b-b','a-(b-b)','a*b*a','a*(b*a)','123ULL','((a))','(a+b)*(b+1ULL)','a+b*2ULL','(a+b)*2ULL','a-2ULL*b','(a-2ULL)*b','a+18446744073709551615ULL*b','2ULL*3ULL+4ULL','2ULL*(3ULL+4ULL)','a-b+a*b','(a+a)*(b+b)']
POSITIVES=[(HEADER+f'uint64_t calc(uint64_t a, uint64_t b) {{ return {e}; }}\n','calc',2) for e in EXPRESSIONS]
POSITIVES += [(HEADER+'uint64_t calc(void) { return 7ULL; }\n','calc',0),(HEADER+'uint64_t calc(uint64_t x) { return x; }\n','calc',1),(HEADER+'uint64_t calc(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f) { return a+b+c+d+e+f; }\n','calc',6),(HEADER+'/* α comment retained */\nuint64_t calc(uint64_t left,uint64_t right) { /* keep */ return left*right; }\r\n','calc',2)]
POSITIVES += [(HEADER+'uint64_t omega_fold(uint64_t a,uint64_t b){return a*b+42ULL;}\n','omega_fold',2)]
NEGATIVE_BODIES=['uint32_t calc(uint64_t a,uint64_t b){return a+b;}','int64_t calc(uint64_t a,uint64_t b){return a+b;}','uint8_t calc(uint8_t a,uint8_t b){return a+b;}','uint64_t calc(uint64_t *a){return *a;}','uint64_t calc(uint64_t a,uint64_t b){return a/b;}','uint64_t calc(uint64_t a,uint64_t b){return a%b;}','uint64_t calc(uint64_t a,uint64_t b){return a<<b;}','uint64_t calc(uint64_t a,uint64_t b){return a>>b;}','uint64_t calc(uint64_t a,uint64_t b){return a&b;}','uint64_t calc(uint64_t a,uint64_t b){return a|b;}','uint64_t calc(uint64_t a,uint64_t b){return a^b;}','uint64_t calc(uint64_t a,uint64_t b){return a==b;}','uint64_t calc(uint64_t a,uint64_t b){return a?b:a;}','uint64_t calc(uint64_t a,uint64_t b){return -a;}','uint64_t calc(uint64_t a,uint64_t b){return a++;}','uint64_t calc(uint64_t a,uint64_t b){return ~a;}','uint64_t calc(uint64_t a,uint64_t b){if(a)return b;return a;}','uint64_t calc(uint64_t a,uint64_t b){for(;;){}return a;}','uint64_t calc(uint64_t a,uint64_t b){volatile uint64_t c=a;return c;}','uint64_t calc(uint64_t a,uint64_t b){return other(a);}','uint64_t calc(uint64_t a,uint64_t b){return sizeof(a);}','uint64_t calc(uint64_t a,uint64_t b){return (uint32_t)a;}','uint64_t calc(uint64_t a,uint64_t b){return 18446744073709551616ULL;}','uint64_t calc(uint64_t a,uint64_t b){return 1.0;}','uint64_t calc(uint64_t a,uint64_t b){return "abc";}','uint64_t calc(uint64_t a,uint64_t b){return unknown;}','uint64_t calc(uint64_t a,uint64_t a){return a;}','uint64_t calc(){return 1ULL;}','uint64_t calc(uint64_t a,uint64_t b){return a+b;} uint64_t extra(void){return 1ULL;}','uint64_t calc(uint64_t a,uint64_t b){return 2;}','uint64_t calc(uint64_t a,uint64_t b){return 1ull;}','uint64_t calc(uint64_t a,uint64_t b){return a+b','uint64_t calc(uint64_t return){return 1ULL;}','uint64_t calc(uint64_t _private){return _private;}','uint64_t calc(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g){return a;}']
NEGATIVES=[(HEADER+b).encode() for b in NEGATIVE_BODIES]+[b'#include <stdio.h>\nuint64_t calc(void){return 1ULL;}\n',b'#define X 1\n'+HEADER.encode()+b'uint64_t calc(void){return X;}',HEADER.encode()+b'/*unterminated',HEADER.encode()+b'uint64_t calc(void){return 1ULL;}\x00',b'x'*4097]
# Source spelling must not smuggle octal or pre-lexical C translation phases.
NEGATIVES += [(HEADER+'uint64_t calc(void){'+body+'}\n').encode() for body in ['return 010ULL;','return 00ULL;','return 09ULL;','return 1ULL // comment '+chr(92)+'\n +2ULL\n;','return 1ULL // comment ??/\n +2ULL\n;']]
NEGATIVES += [(HEADER+body).encode() for body in ['uint64_t calc(uint64_t UINT64_MAX){return 1ULL;}','uint64_t uint32_t(void){return 1ULL;}','uint64_t malloc(void){return 1ULL;}']]
MASK=(1<<64)-1

def decode(buf,src,arity):
    assert len(buf)>=13 and buf[:5]==b'OMSB\x01','bad envelope'
    ns,np=struct.unpack_from('<II',buf,5);assert len(buf)==13+ns+np
    s=buf[13:13+ns];p=buf[13+ns:]
    assert s[:10]==b'OMSA\x01\x01\x01\x01\x00\x01','wrong C profile'
    count=struct.unpack_from('<H',s,10)[0];assert 1<=count<=64 and len(s)==44+20*count
    assert s[12:44]==hashlib.sha256((ROOT/'spec/universal/semantics.md').read_bytes()).digest(),'wrong semantic contract'
    assert p[:6]==b'OMSP\x01\x00' and struct.unpack_from('<I',p,6)[0]==len(src)
    assert p[10:42]==hashlib.sha256(s).digest() and p[42:74]==hashlib.sha256(src).digest(),'bad provenance binding'
    assert len(p)==74+8*count+len(src) and p[74+8*count:]==src,'source replay mismatch'
    for i in range(count):
        lo,hi=struct.unpack_from('<II',p,74+8*i);assert 0<=lo<=hi<=len(src),'bad span'
    ops=[];slots=[]
    for i in range(count):
        d,v,op,t,attr,effects,alias,obligations,aa,bb,imm=struct.unpack_from('<8BHHQ',s,44+20*i)
        assert d==1 and v==1 and t==1 and effects==alias==0
        if op==9:
            assert attr==0 and obligations==1 and aa==bb==65535 and imm<arity
            slots.append(imm)
        elif op==1:assert attr==1 and obligations==0 and aa==bb==65535
        else:assert op in [2,3,4] and attr==1 and obligations==1 and aa<i and bb<i and imm==0
        ops.append((op,aa,bb,imm))
    assert sorted(slots)==list(range(arity)),'parameter boundary changed'
    return ops

def evaluate(ops,args):
    values=[]
    for op,a,b,imm in ops:
        if op==9:v=args[imm]
        elif op==1:v=imm
        elif op==2:v=values[a]+values[b]
        elif op==3:v=values[a]-values[b]
        else:v=values[a]*values[b]
        values.append(v&MASK)
    return values[-1]

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--importer',required=True);ap.add_argument('--out',required=True);a=ap.parse_args()
    out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=False);exe=Path(a.importer).resolve();assert exe.read_bytes()[:4]==b'\x7fELF'
    rows=[];failures=[];rng=random.Random(622);cc=os.environ.get('CC','cc');observations=0;sourceaware_requests=0
    for i,(text,name,nargs) in enumerate(POSITIVES):
        src=text.encode();r=subprocess.run([str(exe)],input=src,capture_output=True,timeout=10)
        item={'case':i,'source_sha256':hashlib.sha256(src).hexdigest(),'exit':r.returncode,'stderr':r.stderr.decode(errors='replace')}
        try:
            assert r.returncode==0, f'import refused rc={r.returncode}'
            ops=decode(r.stdout,src,nargs)
            legality=b''.join(bytes([3,0 if op==1 else 1]) for op,aa,bb,imm in ops)
            for cmd,expected_bytes in [('decode',r.stdout),('replay',src),('legality',legality)]:
                payload=(bytes([2,0,1,0]) if cmd=='legality' else b'')+r.stdout
                sa=subprocess.run([str(ROOT/'tools/source-aware/sa'),cmd],input=payload,capture_output=True,timeout=10);sourceaware_requests+=1
                assert sa.returncode==0 and sa.stdout==expected_bytes,'existing source-aware '+cmd+' refused or changed imported unit: '+sa.stderr.decode(errors='replace')
            # The independent native C compiler evaluates the actual original source.
            c=out/f'oracle-{i}.c';binary=out/f'oracle-{i}';c.write_text(text+'\n#include <stdio.h>\n#include <inttypes.h>\nint main(void){\n'+('uint64_t x[6]={0};while(scanf("'+' '.join(['%" SCNu64 "']*nargs)+'",'+','.join('&x['+str(j)+']' for j in range(nargs))+')=='+str(nargs)+')printf("%" PRIu64 "\\n",'+name+'('+','.join('x['+str(j)+']' for j in range(nargs))+'));' if nargs else 'printf("%" PRIu64 "\\n",'+name+'());')+'return 0;}\n')
            # Unused parameters are valid C17 and are deliberately retained to
            # test the imported function boundary; they are not a source refusal.
            compile_run=subprocess.run([cc,'-std=c17','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter',str(c),'-o',str(binary)],capture_output=True,timeout=30)
            assert compile_run.returncode==0, 'oracle compile failed: '+compile_run.stderr.decode()
            boundary=[0,1,2,(1<<32)-1,1<<32,1<<63,MASK]
            vectors=[tuple(boundary[(j+k)%len(boundary)] for k in range(nargs)) for j in range(len(boundary))]+[tuple(rng.getrandbits(64) for _ in range(nargs)) for _ in range(17)] if nargs else [()]
            payload=''.join(' '.join(map(str,v))+'\n' for v in vectors).encode();native=subprocess.run([str(binary)],input=payload,capture_output=True,timeout=10)
            assert native.returncode==0,native.stderr.decode()
            expected=[evaluate(ops,v) for v in vectors];actual=[int(x) for x in native.stdout.splitlines()]
            if actual!=expected:
                item['mismatched_rows']=[{'input':list(v),'native_C':x,'upper_reference':y} for v,x,y in zip(vectors,actual,expected) if x!=y]
            assert actual==expected,'native C versus imported semantic arithmetic mismatch'
            item.update({'result':'PASS','ops':len(ops),'observations':len(vectors),'upper_sha256':hashlib.sha256(r.stdout).hexdigest(),'native_stdout_sha256':hashlib.sha256(native.stdout).hexdigest(),'oracle_source_sha256':hashlib.sha256(c.read_bytes()).hexdigest(),'oracle_binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest()});observations+=len(vectors)
            (out/f'case-{i}.native.out').write_bytes(native.stdout)
            (out/f'case-{i}.upper-values.out').write_text(''.join(str(v)+'\n' for v in expected))
            (out/f'case-{i}.source.c').write_bytes(src);(out/f'case-{i}.upper').write_bytes(r.stdout)
        except (AssertionError,ValueError,IndexError,struct.error) as exc:
            item['result']='FAIL';item['failure']=str(exc);failures.append(item)
        rows.append(item)
    negatives=[]
    for i,src in enumerate(NEGATIVES):
        r=subprocess.run([str(exe)],input=src,capture_output=True,timeout=10)
        ok=r.returncode!=0 and r.stdout==b'' and b'C_IMPORT_' in r.stderr
        item={'case':i,'source_sha256':hashlib.sha256(src).hexdigest(),'exit':r.returncode,'stdout_bytes':len(r.stdout),'stderr':r.stderr.decode(errors='replace'),'result':'PASS' if ok else 'FAIL'}
        if not ok:failures.append(item)
        negatives.append(item)
    report={'schema':'omega.c-u64-import.development.v1','result':'FAIL' if failures else 'PASS','source_commit':subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip(),'classification':'DIFFERENTIAL_COMPATIBILITY','proof':False,'source_replay_verified':not failures,'positive_executed':len(rows),'positive_passed':sum(r['result']=='PASS' for r in rows),'negative_executed':len(negatives),'negative_passed':sum(r['result']=='PASS' for r in negatives),'source_aware_requests':sourceaware_requests,'source_aware_binary_sha256':hashlib.sha256((ROOT/'build/L574/sa').read_bytes()).hexdigest(),'native_C_observations':observations,'cases':rows,'negative_cases':negatives,'failures':failures,'importer_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'test_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'cc_flags':['-std=c17','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter'],'cc_version':subprocess.check_output([cc,'--version'],text=True),'upstream_621_qualification':'SEPARATE_REQUIRED_GATE','not_implemented':['C exporter','u8/u16/u32','control flow','division','OSC lowering','checker-bound translation proof','x86/RV64 execution']}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k] for k in ['result','positive_executed','positive_passed','negative_executed','negative_passed','native_C_observations']},indent=2));return 1 if failures else 0
if __name__=='__main__':raise SystemExit(main())
