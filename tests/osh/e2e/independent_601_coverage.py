#!/usr/bin/env python3
"""Verify exact case membership and observed values, not just child exit status."""
import pathlib,json,gzip,hashlib,copy,sys
FIXTURE=pathlib.Path(__file__).parent/'fixtures/l601-complete-observed-reference.json.gz'
def load_reference():
 packed=FIXTURE.read_bytes();meta=json.loads((FIXTURE.parent/'l601-reference-provenance.json').read_text());raw=gzip.decompress(packed)
 if hashlib.sha256(raw).hexdigest()!=meta['raw_sha256']:raise ValueError('changed reference fixture')
 ref=json.loads(raw)['rows']
 if len(ref)!=1224 or not all(r['ok'] for r in ref):raise ValueError('incomplete observed reference')
 return ref
def error_class(s):
 return 'numeric' if 'numeric argument required' in s else 'count' if 'too many arguments' in s else 'none' if s=='' else 'other'
def identity(r):return (r['key'],r['core'])
def verify(reference,rows,leg):
 expected={identity(r):r for r in reference}
 if len(expected)!=1224:raise ValueError('duplicate reference')
 found={}
 for row in rows:
  k=identity(row)
  if k in found:raise ValueError('duplicate observed case')
  if k not in expected:raise ValueError('unexpected observed case')
  for process in (row['bash'],row['got']):
   if not {'status','signal','error','stdout','stderr'}.issubset(process) or process['status'] is None or process['signal'] is not None or process['error'] is not None:raise ValueError('abnormal process observation')
  original=expected[k]
  if row['text']!=original['text']:raise ValueError('changed program text')
  if row['leg']!=leg:raise ValueError('wrong executable leg')
  a,b=row['bash'],original['bash']
  if (a['status'],a['stdout'],error_class(a['stderr']))!=(b['status'],b['stdout'],error_class(b['stderr'])):raise ValueError('changed Bash oracle')
  got=row['got']
  if row['ok'] is not True or (got['status'],got['stdout'],error_class(got['stderr']))!=(a['status'],a['stdout'],error_class(a['stderr'])) or error_class(got['stderr'])=='other':raise ValueError('behavioral assertion failed')
  found[k]=row
 if set(found)!=set(expected):raise ValueError('missing observed cases')
 return {'rows':len(rows),'unique_cases':len(found),'programs':len({r['key'].rsplit('-',2)[0] for r in rows}),'scope':'exact observed membership and behavior, not fresh execution by this checker'}
def controls(legacy=False):
 ref=load_reference();variants=[]
 variants.append(('complete',copy.deepcopy(ref),False,None))
 variants.append(('missing',copy.deepcopy(ref[:-1]),True,'missing observed cases'))
 variants.append(('duplicate',copy.deepcopy(ref)+[copy.deepcopy(ref[0])],True,'duplicate observed case'))
 changed=copy.deepcopy(ref);changed[0]['text']+='\n';variants.append(('changed-input',changed,True,'changed program text'))
 changed=copy.deepcopy(ref);changed[0]['bash']['status']=99;variants.append(('changed-oracle',changed,True,'changed Bash oracle'))
 changed=copy.deepcopy(ref);changed[0]['got']['status']=99;variants.append(('false-ok',changed,True,'behavioral assertion failed'))
 changed=copy.deepcopy(ref);changed[0]['got']['signal']='SIGSEGV';variants.append(('abnormal',changed,True,'abnormal process observation'))
 changed=copy.deepcopy(ref);changed[0]['leg']='sanitizer';variants.append(('wrong-leg',changed,True,'wrong executable leg'))
 results=[]
 for name,rows,reject,message in variants:
  actual=None
  try:
   if legacy:
    if any(not r['ok'] for r in rows):raise ValueError('failed rows')
   else:verify(ref,rows,'plain')
  except ValueError as e:actual=str(e)
  ok=(actual==message) if reject else actual is None
  results.append({'case':name,'expected_refusal':message,'actual_refusal':actual,'pass':ok})
 print(json.dumps({'controls':results,'scope':'SYNTHETIC_COVERAGE_CONTROLS_MUTATING_REPLAYED_REAL_ROWS; NOT new program executions'}));return all(x['pass'] for x in results)
if __name__=='__main__':
 if len(sys.argv)==2 and sys.argv[1] in ('--self-test','--legacy-acceptance'):
  sys.exit(not controls(sys.argv[1]=='--legacy-acceptance'))
 if len(sys.argv)<4:raise SystemExit('usage: coverage.py LEG OUTPUT_JSON SHARD_RESULTS...')
 leg,out,*inputs=sys.argv[1:];rows=[]
 for name in inputs:
  shard=json.loads(pathlib.Path(name).read_text());actual=len({r['key'].rsplit('-',2)[0] for r in shard['rows']})
  if shard['cases']!=actual:raise ValueError('declared shard case count mismatch')
  rows.extend(shard['rows'])
 summary=verify(load_reference(),rows,leg);pathlib.Path(out).write_text(json.dumps({'cases':summary['programs'],'rows':rows,'coverage':summary},indent=2)+'\n');print(json.dumps(summary))
