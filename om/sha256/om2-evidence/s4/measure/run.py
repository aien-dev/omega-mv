import json, os, random, subprocess, sys
ROUNDS=31; WARM=3; REPS=31; SEED=20261025
random.seed(SEED)
arms={"A":"/tmp/zl/bench/bench-A","B":"/tmp/zl/bench/bench-B"}
def run(p):
    out=subprocess.run(["taskset","-c","3","nice","-n","19",p,str(REPS)],capture_output=True,text=True)
    if out.returncode!=0: sys.exit("arm failed: "+p+" "+out.stderr[:300])
    rows={}
    for line in out.stdout.strip().splitlines():
        f=line.split()
        if f[0]=="stream":
            rows[f"stream:{f[1]}"]={"min":float(f[3]),"med":float(f[4]),"p90":float(f[5]),"max":float(f[6]),"MBps":float(f[7]),"dig":f[8]}
        else:
            rows["hash:"+f[1]]={"min":float(f[2]),"med":float(f[3]),"p90":float(f[4]),"max":float(f[5]),"nspb":float(f[6]),"dig":f[7]}
    return rows
data={a:{} for a in arms}
for r in range(WARM+ROUNDS):
    order=list(arms); random.shuffle(order)
    for a in order:
        rows=run(arms[a])
        if r>=WARM:
            for k,v in rows.items(): data[a].setdefault(k,[]).append(v)
json.dump(data,open("/tmp/zl/bench/raw.json","w"),indent=0)
def med(xs): xs=sorted(xs); n=len(xs); return xs[n//2] if n%2 else (xs[n//2-1]+xs[n//2])/2
print(f"{'case':22s} {'A med ns':>10s} {'A min..max':>22s} {'B med ns':>10s} {'B min..max':>22s} {'ratio B/A':>9s} {'rmin..rmax':>14s}")
for k in data["A"]:
    A=data["A"][k]; B=data["B"][k]
    am=med([x["med"] for x in A]); bm=med([x["med"] for x in B])
    ratios=sorted(b["med"]/a["med"] for a,b in zip(A,B))
    units="MB/s" if k.startswith("stream") else "ns"
    print(f"{k:22s} {am:10.1f} {min(x['min'] for x in A):9.1f}..{max(x['max'] for x in A):<11.1f} {bm:10.1f} {min(x['min'] for x in B):9.1f}..{max(x['max'] for x in B):<11.1f} {med(ratios):9.3f} {ratios[0]:6.3f}..{ratios[-1]:<6.3f} {units}")
