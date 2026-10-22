from pathlib import Path
import hashlib,json,subprocess,datetime
h=Path.home();w=h/'workspace/omega-osc-manual-migration';old=h/'workspace/omega-retirement-integration/evidence/seed-retirement/retirement-manifest.json'
m=json.loads(old.read_text());rows=[]
for a in m['artifacts']:
 obs=a.get('observation')
 if not obs:continue
 rows.append({'id':a['id'],'prior_observation':obs})
print(json.dumps(rows[:1],indent=2))
report={'observed_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'scope':'read-only source-catalog reconciliation, not production retirement qualification','historical_manifest_sha256':hashlib.sha256(old.read_bytes()).hexdigest(),'historical_observed_utc':m['observed_utc'],'coverage_complete':False,'repositories':[]}
for name,source in m['sources'].items():
 p=Path(source['repo_path']);r={'name':name,'path':str(p),'historical_default':source['default'],'owner_record':source['owner']}
 for key,args in [('current_head',['rev-parse','HEAD']),('current_remote_main',['rev-parse','refs/remotes/origin/main'])]:
  q=subprocess.run(['git','-C',str(p),*args],capture_output=True,text=True)
  r[key]=q.stdout.strip() if q.returncode==0 else None
 r['default_ref_changed']=r['current_remote_main'] is not None and r['current_remote_main']!=source['default']
 report['repositories'].append(r)
report['counts']={'catalog_repositories':len(report['repositories']),'changed_default_refs':sum(r['default_ref_changed'] for r in report['repositories']),'unresolved_default_refs':sum(r['current_remote_main'] is None for r in report['repositories'])}
out=w/'evidence/OSC-MANUAL-MIGRATION/system-catalog-reconciliation.json';out.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report['counts']));print('Coverage remains incomplete; this check does not fetch remotes or prove installed artifact identities.')
