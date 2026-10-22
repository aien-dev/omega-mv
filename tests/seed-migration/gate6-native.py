import pathlib,subprocess,tempfile,os,sys,json,shutil
w=pathlib.Path(sys.argv[1]).resolve();base=w/'build/manual-migration';base.mkdir(parents=True,exist_ok=True)
archive=subprocess.check_output(['git','-C',str(w),'archive','HEAD','seed']);script=(w/'seed/omc/gate6.sh').read_text()
if '--baseline' in sys.argv:script=subprocess.check_output(['git','-C',str(w),'show','cd4436e8ffd5fc0b61d355cd6686f23a9f97472b:seed/omc/gate6.sh'],text=True)
rows=[]
for case in ['intact','host-tools-blocked','wrong-diagnostic','missing-diagnostic','mutant-not-swapped','empty-executable']:
 with tempfile.TemporaryDirectory(prefix='gate6-native-',dir=base) as td:
  d=pathlib.Path(td);subprocess.run(['tar','-x','-C',str(d)],input=archive,check=True)
  t=d/'seed/omc/tests/struct';shutil.copytree(w/'seed/omc/tests/struct/expected',t/'expected',dirs_exist_ok=True);shutil.copyfile(w/'seed/omc/tests/struct/t1x-swap.om',t/'t1x-swap.om')
  candidate=script
  if case=='empty-executable':
   anchor='  [ "$crc" = 0 ] || bad "$1: compiler exit $crc ($(cat "$B/$1.err"))"';assert candidate.count(anchor)==1
   candidate=candidate.replace(anchor,anchor+'\n  : > "$B/$1.bin"')
  (d/'seed/omc/gate6.sh').write_text(candidate)
  if case=='wrong-diagnostic':(t/'expected/bad_field.txt').write_text('omc: line 4: error 30\n')
  if case=='missing-diagnostic':(t/'expected/bad_field.txt').unlink()
  if case=='mutant-not-swapped':shutil.copyfile(t/'t1x.om',t/'t1x-swap.om')
  env=os.environ.copy();log=d/'denied.log'
  if case=='host-tools-blocked':
   b=d/'blocked';b.mkdir()
   for tool in ['sha256sum','cut','cp','grep','sed']:
    p=b/tool;p.write_text('#!/bin/sh\nprintf "%s\\n" "'+tool+'" >> "$DENIED_LOG"\nexit 97\n');p.chmod(0o700)
   env['PATH']=str(b)+':'+env['PATH'];env['DENIED_LOG']=str(log)
  r=subprocess.run(['sh','seed/omc/gate6.sh'],cwd=d,env=env,capture_output=True,text=True,timeout=60)
  if case in ['intact','host-tools-blocked']:ok=r.returncode==0 and 'PASS gate6 26 cases\nRC=0' in r.stdout and 'PASS fixpoint 01b1545b23e8ff40cb5691e20b5df910ea4acaefde3495f0d62d270465b98cd6' in r.stdout and not log.exists()
  else:
   reason='t1: invalid ELF' if case=='empty-executable' else ('mut ran 20, wanted 22' if case=='mutant-not-swapped' else 'bad_field: diagnostic differs')
   ok=r.returncode!=0 and 'RC=0' not in r.stdout and reason in r.stderr
  rows.append({'case':case,'pass':ok,'exit':r.returncode,'stderr':r.stderr,'stdout':r.stdout,'denied_tools':log.read_text().splitlines() if log.exists() else []})
print(json.dumps({'cases':len(rows),'failures':sum(not x['pass'] for x in rows),'results':rows},indent=2));sys.exit(any(not x['pass'] for x in rows))
