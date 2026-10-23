#!/usr/bin/env python3
"""Partition real focused runs; preserve normal assertion failures for mutants."""
import pathlib,subprocess,json,sys,hashlib
binary=pathlib.Path(sys.argv[1]).resolve();out=pathlib.Path(sys.argv[2]).resolve();reference=json.loads(pathlib.Path(sys.argv[3]).read_text());out.mkdir(parents=True,exist_ok=False)
def key(r):return (r['name'],r['mode'],r['core'])
expected={key(r):r for r in reference['rows']};assert len(expected)==504 and all(r['pass'] for r in expected.values())
children=[];logs=[]
try:
 for index in range(4):
  root=out/('shard-'+str(index));f=(out/('shard-'+str(index)+'.log')).open('w');logs.append(f);command=['node','tests/osh/e2e/exit_abort_status.mjs',str(binary),str(root),str(index)+'/4'];p=subprocess.Popen(command,stdout=f,stderr=subprocess.STDOUT);children.append((p,root,command))
 dispatch=[];rows=[]
 for p,root,command in children:
  rc=p.wait();dispatch.append({'command':command,'exit':rc});
  if rc not in (0,1) or not (root/'results.json').is_file():raise RuntimeError('incomplete/abnormal shard, not a behavioral kill')
  data=json.loads((root/'results.json').read_text());assert data['programs']==len({r['name'] for r in data['rows']});rows.extend(data['rows'])
 observed={}
 for row in rows:
  identity=key(row)
  if identity in observed or identity not in expected:raise RuntimeError('duplicate/unexpected observed identity')
  old=expected[identity]
  if row['text']!=old['text']:raise RuntimeError('changed program input')
  oracle=row['bash'];original=old['bash'];got=row['got']
  if (oracle['status'],oracle['stdout'],oracle['errorClass'])!=(original['status'],original['stdout'],original['errorClass']):raise RuntimeError('changed actual Bash oracle')
  for proc in (oracle,got):
   if proc.get('signal') is not None or proc.get('error') is not None or proc['status'] is None:raise RuntimeError('abnormal process observation')
  actual=oracle['status']==got['status'] and oracle['stdout']==got['stdout'] and oracle['errorClass']==got['errorClass'] and got['errorClass']!='other'
  if row['pass']!=actual:raise RuntimeError('false assertion flag')
  observed[identity]=row
 if set(observed)!=set(expected):raise RuntimeError('missing observed cases')
 (out/'dispatch.json').write_text(json.dumps({'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'shards':dispatch},indent=2)+'\n');(out/'results.json').write_text(json.dumps({'programs':len({r['name'] for r in rows}),'rows':rows},indent=2)+'\n');failed=sum(not r['pass'] for r in rows);print(json.dumps({'comparisons':len(rows),'failed':failed,'unique_identities':len(observed),'scope':'fresh Bash/candidate observations; per-command20s unchanged'}));sys.exit(bool(failed))
finally:
 for p,_,_ in children:
  if p.poll() is None:p.terminate();p.wait()
 for f in logs:f.close()
