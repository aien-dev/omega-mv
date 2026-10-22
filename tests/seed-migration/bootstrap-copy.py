import pathlib,subprocess,tempfile,os,sys,json
w=pathlib.Path(sys.argv[1]).resolve();base=w/'build/manual-migration';base.mkdir(parents=True,exist_ok=True)
archive=subprocess.check_output(['git','-C',str(w),'archive','HEAD','seed']);script=(w/'seed/omega-from-seed.sh').read_text()
if '--baseline' in sys.argv:script=subprocess.check_output(['git','-C',str(w),'show','dd6e5b92d2e94a94314ac2ce529d5a6c7f01d705:seed/omega-from-seed.sh'],text=True)
rows=[]
for case in ['intact','cp-blocked','copy-error','empty-copy','corrupt-copy']:
 with tempfile.TemporaryDirectory(prefix='copy-integration-',dir=base) as td:
  d=pathlib.Path(td);subprocess.run(['tar','-x','-C',str(d)],input=archive,check=True)
  source=(w/'seed/native/streamcopy.om').read_text()
  if case=='copy-error':source='fn main to int\nret 2\nend\n'
  if case=='empty-copy':source='fn main to int\nret 0\nend\n'
  if case=='corrupt-copy':source='text Bad "x"\nfn main to int\nw = write 1 Bad 0 1\nret 0\nend\n'
  (d/'seed/native/streamcopy.om').write_text(source);(d/'seed/omega-from-seed.sh').write_text(script)
  env=os.environ.copy();log=d/'denied.log'
  if case=='cp-blocked':
   b=d/'blocked';b.mkdir();p=b/'cp';p.write_text('#!/bin/sh\nprintf called > "$DENIED_LOG"\nexit 97\n');p.chmod(0o700);env['PATH']=str(b)+':'+env['PATH'];env['DENIED_LOG']=str(log)
  r=subprocess.run(['sh','seed/omega-from-seed.sh'],cwd=d,env=env,capture_output=True,text=True,timeout=60)
  if case in ['intact','cp-blocked']:ok=r.returncode==0 and 'RC=0' in r.stdout and not log.exists() and (d/'seed/out/omc').read_bytes()==(d/'build/seed/birth/stage3').read_bytes()
  else:
   reason='native compiler copy failed' if case=='copy-error' else 'copied compiler differs'
   ok=r.returncode!=0 and 'RC=0' not in r.stdout and reason in r.stderr
  rows.append({'case':case,'pass':ok,'exit':r.returncode,'stderr':r.stderr,'stdout':r.stdout,'denied_cp_called':log.exists()})
print(json.dumps({'cases':len(rows),'failures':sum(not x['pass'] for x in rows),'results':rows},indent=2));sys.exit(any(not x['pass'] for x in rows))
