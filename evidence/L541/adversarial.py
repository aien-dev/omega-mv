"""L541 host-only independent review probes. Never edits author sources."""
import csv,hashlib,importlib.util,json,pathlib,random,shutil,subprocess,sys
repo=pathlib.Path(__file__).resolve().parents[2]
out=pathlib.Path('/home/drakestapleton/handoffs/omega-issue-sweep-20261022')
binary=out/'L541-guard'
M=2**64-1
records=[]
def expected(q):
    v,base,best,h,spent,nxt,budget,cap,front,cost=q
    if v!=1 or h==0 or nxt==0 or best>base or front>8:return 20,b''
    paid=spent
    if cost>budget-spent: reason=2
    elif cost>cap-spent:reason=3
    else:
        paid=spent+cost
        if front==0:reason=4
        elif best==0:reason=5
        elif nxt>budget-paid:reason=2
        elif nxt>cap-paid:reason=3
        elif (h*best)-nxt<=0:reason=6
        else:reason=1
    gain=base-best
    be=0 if gain==0 else (paid+gain)//gain
    return 0, ('\n'.join(map(str,[reason,paid,min(be,M),int(be>M)]))+'\n').encode()
def check(q):
    want=expected(q);p=subprocess.run([str(binary)],input=(' '.join(map(str,q))).encode(),capture_output=True,timeout=3)
    if (p.returncode,p.stdout)!=want:records.append({'q':q,'expected':[want[0],want[1].decode()],'actual':[p.returncode,p.stdout.decode()]})
rng=random.Random(541)
for _ in range(1500):
    base=rng.randrange(M+1);best=rng.randrange(base+1);h=rng.choice([1,2,M,rng.randrange(1,M+1)])
    paid=rng.randrange(M+1);cost=rng.randrange(M+1);nxt=rng.choice([1,M,rng.randrange(1,M+1)])
    limit=min(M,paid+cost+rng.choice([-1,0,1]));limit=max(0,limit)
    check([1,base,best,h,paid,nxt,limit,rng.choice([limit,M]),rng.randrange(9),cost])
for h in [1,2,3,7,32,2**32,M]:
 for best in [1,2,3,2**32,M]:
  for delta in [-1,0,1]:
   nxt=h*best+delta
   if 0<nxt<=M:check([1,M,best,h,0,nxt,M,M,8,0])
for spent in [0,1,M-1,M]:
 for delta in [0,1,2,M]:
  check([1,M,M-delta,1,spent,1,M,M,1,0])
protocol=[]
base=b'1 100 50 2 0 1 160 160 2 1'
for name,data in [('CRLF',base.replace(b' ',b'\r\n')),('tabs',base.replace(b' ',b'\t')),('vertical_tab',base.replace(b' ',b'\v')),('form_feed',base.replace(b' ',b'\f')),('512_tokens',b'1 '*512),('buffer_limit',base+b' '*(16384-len(base))),('buffer_limit_minus1',base+b' '*(16383-len(base))),('decimal_leading_zeroes',b'000'+base),('non_ascii',base+b'\xa0')]:
 p=subprocess.run([str(binary)],input=data,capture_output=True,timeout=3);protocol.append({'case':name,'input_bytes':len(data),'exit':p.returncode,'stdout':p.stdout.decode()})
report={'seed':541,'numeric_checks':1500+sum(1 for h in [1,2,3,7,32,2**32,M] for b in [1,2,3,2**32,M] for d in [-1,0,1] if 0<h*b+d<=M)+16,'numeric_failures':records,'protocol':protocol}
(repo/'evidence/L541/guard-adversarial.json').write_text(json.dumps(report,indent=2)+'\n')
# Use fresh, caller-owned fixture. Copy before linking an ancestor, never mutate author tree.
fixture=out/'L541-corpus-tests/export';linked=out/'L541-ancestor-export';shutil.copytree(fixture,linked)
verifier=repo/'tests/compiler/verify_decoder_corpus.py';checks=[]
def verify(name):
 p=subprocess.run([sys.executable,str(verifier),str(linked),str(repo)],capture_output=True,text=True)
 checks.append({'case':name,'exit':p.returncode,'stderr':p.stderr,'manifest_programs':json.loads(p.stdout)['corpus_programs'] if p.returncode==0 else None})
verify('ordinary')
for rel in ['src','tests/compiler','tests/physics0']:
 path=linked/rel;outside=out/('L541-external-'+rel.replace('/','-'));path.rename(outside);path.symlink_to(outside,target_is_directory=True);verify('linked_ancestor:'+rel);path.unlink();outside.rename(path)
path=linked/'src/osh';outside=out/'L541-external-leaf';path.rename(outside);path.symlink_to(outside,target_is_directory=True);verify('linked_direct_corpus_directory');path.unlink();outside.rename(path)
(repo/'evidence/L541/corpus-adversarial.json').write_text(json.dumps(checks,indent=2)+'\n')
print(json.dumps({'numeric_failures':len(records),'protocol':protocol,'corpus':checks}))
