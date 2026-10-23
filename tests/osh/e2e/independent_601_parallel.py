#!/usr/bin/env python3
"""Run disjoint real-process matrix shards; require every observed case once."""
import pathlib,subprocess,sys,json,hashlib,os
binary=pathlib.Path(sys.argv[1]).resolve();out=pathlib.Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=False)
leg='sanitizer' if binary.name.endswith('_asan') else 'plain';count=4;children=[];files=[]
try:
 for index in range(count):
  root=out/('shard-'+str(index));log=out/('shard-'+str(index)+'.log');f=log.open('w');files.append(f)
  command=['node','tests/osh/e2e/exit_independent_601.mjs',str(binary),str(root),str(index)+'/'+str(count)]
  p=subprocess.Popen(command,stdout=f,stderr=subprocess.STDOUT);children.append((p,command,root,log))
 results=[]
 for p,command,root,log in children:
  rc=p.wait();results.append({'command':command,'exit':rc,'log':str(log),'root':str(root)})
 (out/'dispatch.json').write_text(json.dumps({'binary':str(binary),'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'leg':leg,'workers':count,'results':results},indent=2)+'\n')
 if any(r['exit']!=0 for r in results):raise RuntimeError('a real matrix shard failed; no coverage PASS')
 paths=[str(root/'independent-results.json') for _,_,root,_ in children]
 command=[sys.executable,'tests/osh/e2e/independent_601_coverage.py',leg,str(out/'independent-results.json'),*paths]
 p=subprocess.run(command,capture_output=True,text=True);(out/'coverage.log').write_text(p.stdout+p.stderr)
 if p.returncode:raise RuntimeError('coverage or observed assertion verification failed: '+p.stderr)
 print(json.dumps({'leg':leg,'workers':count,'coverage':json.loads(p.stdout),'scope':'fresh real Bash and both host-core executions; 20s per-command bounds unchanged'}))
finally:
 for p,_,_,_ in children:
  if p.poll() is None:p.terminate();p.wait()
 for f in files:f.close()
