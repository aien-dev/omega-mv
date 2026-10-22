#!/usr/bin/env python3
"""Counterfactual prefix replay of known synthetic histories, not a new campaign."""
import csv,hashlib,json,pathlib,subprocess,sys,collections
root=pathlib.Path(__file__).resolve().parents[3]
binary=pathlib.Path(sys.argv[1]); out=pathlib.Path(sys.argv[2]); out.mkdir(parents=True,exist_ok=False)
base=root/'evidence/EXPLORATION-600/campaign/run-002/heldout'
raw=(base/'rows.csv').read_bytes(); before=hashlib.sha256(raw).hexdigest()
rows=list(csv.DictReader(raw.decode().splitlines())); histories=collections.defaultdict(list)
for r in csv.DictReader((base/'trace.csv').open()): histories[(r['family'],r['seed'],r['arm'])].append(r)
assert len(rows)==300 and len(histories)==100
results=[]
for (family,seed,arm),hist in sorted(histories.items()):
    for horizon in (1,8,32):
        original=next(r for r in rows if (r['family'],r['seed'],r['arm'],r['horizon'])==(family,seed,arm,str(horizon)))
        for cap in (32,160):
            paid=0; best=100; source_spent=0; frontier=2; accepted=0; reason=4; overhead=0
            for step in hist:
                charge=int(step['spent'])-source_spent
                q=[1,100,best,horizon,paid,charge,160,cap,frontier,1]
                p=subprocess.run([str(binary)],input=(' '.join(map(str,q))+'\n').encode(),capture_output=True,check=True,timeout=3)
                why,newpaid,be,overflow=map(int,p.stdout.split())
                overhead+=newpaid-paid; paid=newpaid; reason=why
                assert 0<=paid<=cap and paid<=160
                if why!=1: break
                paid+=charge; accepted+=1; source_spent=int(step['spent']); best=int(step['best']); frontier=int(step['frontier']).bit_count()
                assert paid<=cap and paid<=160
            results.append({'family':int(family),'seed':int(seed),'arm':int(arm),'horizon':horizon,'risk_cap':cap,'source_trials':int(original['trials']),'trials':accepted,'search_spent':source_spent,'decision_cost':overhead,'total_spent':paid,'best':best,'stop':reason,'source_net':int(original['net']),'net':100*horizon-paid-horizon*best,'source_allows_loss':int(original['net'])<0,'qualification':'KNOWN_SYNTHETIC_COUNTERFACTUAL_PREFIX_ONLY'})
assert len(results)==600
with (out/'rows.csv').open('w') as f:
    w=csv.DictWriter(f,fieldnames=results[0]);w.writeheader();w.writerows(results)
summary=[]
for horizon in (1,8,32):
    for cap in (32,160):
        a=[r for r in results if r['horizon']==horizon and r['risk_cap']==cap and r['arm']==2]
        summary.append({'arm':'C','horizon':horizon,'risk_cap':cap,'cases':len(a),'positive_cases':sum(r['net']>0 for r in a),'total_net_synthetic':sum(r['net'] for r in a),'worst_net_synthetic':min(r['net'] for r in a),'total_decision_cost':sum(r['decision_cost'] for r in a),'lost_discoveries_vs_source':0})
# Count lost discoveries explicitly without hiding conversions inside summaries.
for s in summary:
    a=[r for r in results if r['horizon']==s['horizon'] and r['risk_cap']==s['risk_cap'] and r['arm']==2]
    s['lost_discoveries_vs_source']=sum(r['best']>int(next(x for x in rows if (x['family'],x['seed'],x['arm'],x['horizon'])==(str(r['family']),str(r['seed']),str(r['arm']),str(r['horizon'])))['best_cost']) for r in a)
assert hashlib.sha256((base/'rows.csv').read_bytes()).hexdigest()==before
packet={'scope':'known synthetic trace prefix replay; no fresh heldout or physical result','rows':len(results),'original_rows_sha256':before,'original_scientific_verdict':'FAIL_UNCHANGED','guard_cost':'declared synthetic1 per decision, not measured CPU cost','regimes':summary,'warnings':['No guarantee of positive net or superiority to random','Caps can prevent useful delayed payoff; report lost discoveries','Decision overhead can reduce net even when all original trials fit','Historical trace truncation is not a full causal learning experiment']}
(out/'summary.json').write_text(json.dumps(packet,indent=2)+'\n');print(json.dumps(packet))
