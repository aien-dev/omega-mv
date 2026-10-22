import pathlib,sys,subprocess,tempfile,os,hashlib,json
w=pathlib.Path(sys.argv[1]).resolve();base=w/'build/manual-migration';base.mkdir(parents=True,exist_ok=True)
archive=subprocess.check_output(['git','-C',str(w),'archive','HEAD','seed'])
script=(w/'seed/omega-from-seed.sh').read_bytes()
if '--baseline' in sys.argv:script=subprocess.check_output(['git','-C',str(w),'show','837a3d6c7f5450ecf2086af1bdfbff61ee7d933d:seed/omega-from-seed.sh'])
pin='01b1545b23e8ff40cb5691e20b5df910ea4acaefde3495f0d62d270465b98cd6'
results=[]
cases=['intact','stage3-extra-byte','missing-hasher','invalid-comparator','empty-hash-output']
for case in cases:
 with tempfile.TemporaryDirectory(prefix='bootstrap-native-',dir=base) as td:
  d=pathlib.Path(td);subprocess.run(['tar','-x','-C',str(d)],input=archive,check=True)
  s=script.decode()
  if case=='stage3-extra-byte':
   anchor='chmod +x "$B/stage3"';assert s.count(anchor)==1;s=s.replace(anchor,anchor+'\nprintf x >> "$B/stage3"')
  (d/'seed/omega-from-seed.sh').write_text(s)
  if case=='missing-hasher':(d/'seed/native/sha256.om').unlink()
  if case=='invalid-comparator':(d/'seed/native/byteequal.om').write_text('not valid Omega\n')
  if case=='empty-hash-output':(d/'seed/native/sha256.om').write_text('fn main to int\nret 0\nend\n')
  b=d/'blocked';b.mkdir();log=d/'denied.log'
  for tool in ['sha256sum','cut']:
   p=b/tool;p.write_text('#!/bin/sh\nprintf "%s\\n" "'+tool+'" >> "$BOOTSTRAP_DENIED_LOG"\nexit 97\n');p.chmod(0o700)
  env=os.environ.copy();env['PATH']=str(b)+':'+env['PATH'];env['BOOTSTRAP_DENIED_LOG']=str(log)
  r=subprocess.run(['sh','seed/omega-from-seed.sh'],cwd=d,env=env,capture_output=True,text=True,timeout=60)
  denied=log.read_text().splitlines() if log.exists() else []
  passed=False;digest=None
  if case=='intact':
   out=d/'seed/out/omc';digest=hashlib.sha256(out.read_bytes()).hexdigest() if out.exists() else None
   receipt=(d/'seed/out/omc.sha256').read_text() if (d/'seed/out/omc.sha256').exists() else ''
   passed=r.returncode==0 and digest==pin and receipt==pin+'  seed/out/omc\n' and 'RC=0' in r.stdout and not denied
  else:
   reason={'stage3-extra-byte':'fixpoint: stage2 != stage3','missing-hasher':'cannot open seed/native/sha256.om','invalid-comparator':'error 16','empty-hash-output':'native compiler hash length'}[case]
   passed=r.returncode!=0 and 'RC=0' not in r.stdout and not denied and reason in r.stderr
  results.append({'case':case,'pass':passed,'exit':r.returncode,'denied_tools_called':denied,'compiler_sha256':digest,'stdout':r.stdout,'stderr':r.stderr})
print(json.dumps({'cases':len(results),'failures':sum(not r['pass'] for r in results),'results':results},indent=2));sys.exit(any(not r['pass'] for r in results))
