#!/usr/bin/env python3
"""Test-only big-integer oracle; native guard owns the admission decision."""
import json,random,subprocess,sys
M=(1<<64)-1
binary=sys.argv[1]
def oracle(q):
    ver,base,best,h,spent,nxt,budget,cap,front,cost=q
    if ver!=1 or not h or not nxt or best>base or front>8: return None
    reason=None; paid=spent
    if spent+cost>budget: reason=2
    elif spent+cost>cap: reason=3
    else:
        paid+=cost
        if not front: reason=4
        elif best==0: reason=5
        elif paid+nxt>budget: reason=2
        elif paid+nxt>cap: reason=3
        elif h*best<=nxt: reason=6
        else: reason=1
    be=paid//(base-best)+1 if base>best else 0
    return [reason,paid,min(be,M),int(be>M)]
def run(q):
    return subprocess.run([binary],input=(' '.join(map(str,q))+'\n').encode(),capture_output=True,timeout=3)
cases=[
 [1,100,100,1,0,129,160,160,2,1],
 [1,100,100,32,0,9,160,32,2,1],
 [1,100,100,32,30,9,160,32,2,1],
 [1,100,100,32,0,9,160,160,0,1],
 [1,100,0,32,5,9,160,160,2,1],
 [1,100,50,1,200,1,400,400,2,1], # sunk loss must not veto incremental improvement
 [1,100,50,1,0,50,160,160,2,0], # strict equality has no positive opportunity
 [1,M,1,M,0,M-1,M,M,1,0], # product would overflow native u64
 [1,1,0,1,M,1,M,M,1,0], # break-even exceeds u64
 [1,1,0,1,M,1,M,M,1,1], # decision itself cannot be paid
 [1,100,100,0,0,1,160,160,2,1],
 [1,100,101,1,0,1,160,160,2,1],
 [2,100,100,1,0,1,160,160,2,1],
 [1,100,100,1,0,0,160,160,2,1],
 [1,100,100,1,0,1,160,160,9,1],
]
rng=random.Random(523)
for _ in range(400):
    base=rng.choice([0,1,7,100,1024,M]); best=rng.choice([0,base,base//2]); h=rng.choice([1,8,32,M]); spent=rng.choice([0,1,31,128,M-1,M]); nxt=rng.choice([1,9,129,M]); budget=rng.choice([32,160,M]); cap=rng.choice([0,32,160,M]); front=rng.randrange(9); cost=rng.choice([0,1,9,M]); cases.append([1,base,best,h,spent,nxt,budget,cap,front,cost])
failed=[]
for n,q in enumerate(cases):
    r=run(q); expected=oracle(q)
    if expected is None:
        ok=r.returncode==20 and not r.stdout
    else:
        try: actual=list(map(int,r.stdout.split()))
        except ValueError: actual=None
        ok=r.returncode==0 and actual==expected
    if not ok: failed.append({'case':n,'input':q,'expected':expected,'exit':r.returncode,'actual':r.stdout.decode(errors='replace')})
malformed=['','1','1 100 100 1 0 1 160 160 2 1 0','1 100 100 1 0 1 160 160 2 -1','1 100 100 1 0 1 160 160 2 18446744073709551616','1 100 100 1 0 1 160 160 2 1x','+1 100 100 1 0 1 160 160 2 1','1 100 100 1 0 1 160 160 2 1\x00']
for n,text in enumerate(malformed):
    r=subprocess.run([binary],input=text.encode(),capture_output=True,timeout=3)
    if r.returncode!=20 or r.stdout: failed.append({'malformed':n,'exit':r.returncode,'actual':repr(r.stdout)})
print(json.dumps({'checks':len(cases)+len(malformed),'passed':len(cases)+len(malformed)-len(failed),'failed':len(failed),'failures':failed[:12],'scope':'native synthetic continuation, not campaign success'}))
sys.exit(bool(failed))
