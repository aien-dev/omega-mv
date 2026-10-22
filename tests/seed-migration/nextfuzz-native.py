import pathlib,sys,subprocess,tempfile,os,json,hashlib,re,shlex
w=pathlib.Path(sys.argv[1]).resolve();base=w/'build/manual-migration';base.mkdir(parents=True,exist_ok=True)
old=subprocess.check_output(['git','-C',str(w),'show','2f879107e100b7509a3d5f594eaf4466b0efa8af:seed/check/nextfuzz.sh'],text=True);new=(w/'seed/check/nextfuzz.sh').read_text();results=[]
with tempfile.TemporaryDirectory(prefix='fuzz-equivalence-',dir=base) as td:
 d=pathlib.Path(td);blocked=d/'blocked';blocked.mkdir();log=d/'denied.log'
 for name in ['sed','cmp']:
  p=blocked/name;p.write_text('#!/bin/sh\nprintf "%s\\n" "'+name+'" >> "$DENIED_LOG"\nexit 97\n');p.chmod(0o700)
 p=blocked/'grep';p.write_text('#!/bin/sh\nif [ "$1" = -q ]; then printf "grep-q\\n" >> "$DENIED_LOG"; exit 97; fi\nexec /usr/bin/grep "$@"\n');p.chmod(0o700)
 ref_wrapper=d/'bad-reference';ref_wrapper.write_text('#!/usr/bin/env python3\nimport sys,subprocess\nr=subprocess.run(['+repr(str(w/'seed/rung4/om4.bin'))+'],input=sys.stdin.buffer.read(),capture_output=True)\nsys.stdout.buffer.write(r.stdout)\nsys.stderr.buffer.write(b"om4: line 1: error 127\\n" if r.returncode else r.stderr)\nsys.exit(r.returncode)\n');ref_wrapper.chmod(0o700)
 wrapper=d/'bad-compiler';wrapper.write_text('#!/bin/sh\n'+shlex.quote(str(w/'seed/out/omc'))+'\nrc=$?\nprintf x\nexit "$rc"\n');wrapper.chmod(0o700)
 for label,script,deny,bad,n in [('baseline',old,False,False,20),('baseline-blocked',old,True,False,1),('native-blocked',new,True,False,20),('wrong-compiler',new,True,True,1),('wrong-reference',new,True,False,1)]:
  work=d/label;env=os.environ.copy();env['NFW']=str(work);env['DENIED_LOG']=str(log)
  if log.exists():log.unlink()
  if deny:env['PATH']=str(blocked)+':'+env['PATH']
  with tempfile.NamedTemporaryFile(mode='w',suffix='.sh',prefix='fuzz-probe-',dir=w/'seed/check',delete=False) as f:f.write(script);runner=pathlib.Path(f.name)
  args=['sh',str(runner),str(n),str(ref_wrapper if label=='wrong-reference' else w/'seed/rung4/om4.bin')]
  if label!='wrong-reference':args.append(str(wrapper if bad else w/'seed/out/omc'))
  try:r=subprocess.run(args,cwd=w,env=env,capture_output=True,text=True,timeout=120)
  finally:runner.unlink()
  summary=next((line for line in r.stdout.splitlines() if line.startswith('nextfuzz:')),None)
  corpus={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in work.glob('v.*')}
  item={'case':label,'exit':r.returncode,'summary':summary,'denied_tools':log.read_text().splitlines() if log.exists() else [],'last_seed_fixture_count':len(corpus),'last_seed_fixture_hash':hashlib.sha256(json.dumps(corpus,sort_keys=True).encode()).hexdigest(),'stderr':r.stderr}
  if label in ['baseline','native-blocked']:assert r.returncode==0 and summary and ', 0 bad;' in summary and not item['denied_tools'],item
  elif label=='baseline-blocked':assert r.returncode!=0 and item['denied_tools'],item
  else:assert r.returncode==1 and summary and re.search(r', [1-9][0-9]* bad;',summary) and not item['denied_tools'],item
  results.append(item)
 assert results[0]['summary']==results[2]['summary'];assert results[0]['last_seed_fixture_hash']==results[2]['last_seed_fixture_hash']
print(json.dumps({'cases':len(results),'failures':0,'results':results,'same_full_summary_and_final_seed_bytes':True},indent=2))
