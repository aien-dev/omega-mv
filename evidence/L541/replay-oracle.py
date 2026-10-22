"""Separate host oracle: pure integer trace accounting, no native guard calls."""
import collections,csv,hashlib,json,pathlib,subprocess
repo=pathlib.Path(__file__).resolve().parents[2]
sweep=pathlib.Path('/home/drakestapleton/handoffs/omega-issue-sweep-20261022')
source=sweep/'L541';base=source/'evidence/EXPLORATION-600/campaign/run-002/heldout'
rows=list(csv.DictReader((base/'rows.csv').open()));histories=collections.defaultdict(list)
for r in csv.DictReader((base/'trace.csv').open()):histories[(r['family'],r['seed'],r['arm'])].append(r)
published=list(csv.DictReader((source/'evidence/EX600-CONTINUATION-L523/counterfactual-rows.csv').open()))
actual=list(csv.DictReader((sweep/'L541-replay/rows.csv').open()))
failed=[];n=0
for rec in actual:
 k=tuple(rec[x] for x in ['family','seed','arm']);h=int(rec['horizon']);cap=int(rec['risk_cap']);best=100;spent=0;previous=0;overhead=0;front=2;trials=0;why=4
 for step in histories[k]:
  charge=int(step['spent'])-previous
  if spent+1>160:why=2;break
  if spent+1>cap:why=3;break
  spent+=1;overhead+=1
  if front==0:why=4;break
  if best==0:why=5;break
  if spent+charge>160:why=2;break
  if spent+charge>cap:why=3;break
  if best*h<=charge:why=6;break
  why=1;spent+=charge;trials+=1;previous=int(step['spent']);best=int(step['best']);front=bin(int(step['frontier'])).count('1')
 expected={'best':best,'trials':trials,'total_spent':spent,'search_spent':previous,'decision_cost':overhead,'stop':why,'net':h*(100-best)-spent}
 for f,v in expected.items():
  if int(rec[f])!=v:failed.append({'row':n,'field':f,'want':v,'actual':rec[f]})
 n+=1
immutable=[]
for p in ['research/exploration-600/policy.osc','research/exploration-600/FREEZE.md','research/exploration-600/AMENDMENTS.md','evidence/EXPLORATION-600/campaign/run-002/heldout/rows.csv','evidence/EXPLORATION-600/campaign/run-002/heldout/trace.csv']:
 contents=[subprocess.check_output(['git','-C',str(repo),'show',ref+':'+p]) for ref in ['b59631c510ea4555535578ef04041f402e05eaa9','dc24fcf27bbc6c36ca6dfcac020452bb06004bae']]
 immutable.append({'path':p,'unchanged':contents[0]==contents[1],'sha256':hashlib.sha256(contents[1]).hexdigest()})
# Check frozen finite family7 independently of observed policy outcome: every reachable cost >= root100.
# Locate published landscape evidence and inspect generator formula separately in source review.
a=[r for r in rows if r['arm']=='2' and r['horizon']=='32']
family7=[r for r in a if r['family']=='7']
report={'rows_checked':n,'field_checks':n*8,'failures':failed,'published_rows_match':actual==published,'original_rows':len(rows),'original_rows_immutable':immutable,'C_original_h32':{'positive':sum(int(r['net'])>0 for r in a),'total':sum(int(r['net']) for r in a),'family7_rows':family7}}
(repo/'evidence/L541/replay-oracle.json').write_text(json.dumps(report,indent=2)+'\n')
assert n==600 and not failed and actual==published and all(r['unchanged'] for r in immutable)
print(json.dumps({'rows':n,'checks':n*8,'failed':len(failed),'published_match':actual==published,'immutability':all(r['unchanged'] for r in immutable)}))
