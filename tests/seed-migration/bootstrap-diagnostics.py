import pathlib,subprocess,tempfile,os,sys,json,hashlib
w=pathlib.Path(sys.argv[1]).resolve();base=w/'build/manual-migration';base.mkdir(parents=True,exist_ok=True)
archive=subprocess.check_output(['git','-C',str(w),'archive','HEAD','seed'])
script=(w/'seed/omega-from-seed.sh').read_text()
if '--baseline' in sys.argv:script=subprocess.check_output(['git','-C',str(w),'show','413f87c84e9b4233e4a56487dc6643b2139b5d0c:seed/omega-from-seed.sh'],text=True)
fixtures={'cycle':'omc: line 7: error 9\n','assign':'omc: line 2: error 7\n'}
rows=[]
for case in ['intact','grep-blocked','cycle-prefix','cycle-wrong-line','assign-suffix','missing-fixture']:
 with tempfile.TemporaryDirectory(prefix='diagnostic-',dir=base) as td:
  d=pathlib.Path(td);subprocess.run(['tar','-x','-C',str(d)],input=archive,check=True)
  f=d/'seed/native/diagnostics';f.mkdir(exist_ok=True)
  for name,text in fixtures.items():(f/(name+'.txt')).write_text(text)
  s=script;anchor='probe assign r5b_assign.om 7';assert s.count(anchor)==1
  inserts={'cycle-prefix':'printf "omc: line 7: error 90\\n" > "$B/cycle.err"','cycle-wrong-line':'printf "omc: line 99: error 9\\n" > "$B/cycle.err"','assign-suffix':'printf "unexpected trailing text\\n" >> "$B/assign.err"'}
  if case in inserts:s=s.replace(anchor,anchor+'\n'+inserts[case])
  if case=='missing-fixture':(f/'cycle.txt').unlink()
  (d/'seed/omega-from-seed.sh').write_text(s)
  env=os.environ.copy();log=d/'grep.log'
  if case=='grep-blocked':
   b=d/'blocked';b.mkdir();p=b/'grep';p.write_text('#!/bin/sh\nprintf called > "$DENIED_LOG"\nexit 97\n');p.chmod(0o700);env['PATH']=str(b)+':'+env['PATH'];env['DENIED_LOG']=str(log)
  r=subprocess.run(['sh','seed/omega-from-seed.sh'],cwd=d,env=env,capture_output=True,text=True,timeout=60)
  if case in ['intact','grep-blocked']:
   digest=hashlib.sha256((d/'seed/out/omc').read_bytes()).hexdigest()
   ok=r.returncode==0 and 'RC=0' in r.stdout and not log.exists() and digest=='01b1545b23e8ff40cb5691e20b5df910ea4acaefde3495f0d62d270465b98cd6'
  else:
   reason='assign: diagnostic differs' if case=='assign-suffix' else 'cycle: diagnostic differs'
   ok=r.returncode!=0 and 'RC=0' not in r.stdout and reason in r.stderr
  rows.append({'case':case,'pass':ok,'exit':r.returncode,'stderr':r.stderr,'stdout':r.stdout,'denied_grep_called':log.exists()})
print(json.dumps({'cases':len(rows),'failures':sum(not x['pass'] for x in rows),'results':rows},indent=2));sys.exit(any(not x['pass'] for x in rows))
