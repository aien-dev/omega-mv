#!/usr/bin/env python3
"""Owned immutable-byte fixtures; static links only, no hostile concurrent writer."""
import hashlib,io,json,os,pathlib,subprocess,sys,tarfile
repo=pathlib.Path(sys.argv[1]).resolve(); out=pathlib.Path(sys.argv[2]).resolve(); out.mkdir(parents=True,exist_ok=False)
root=out/'export';root.mkdir();pin='1e4cdbe91b444efe51af4e745f366e07d54de030'
dirs=['tests/compiler/progs','tests/compiler/slices','src/osh','tests/physics0/osc'];loader='tests/compiler/test_osc_ir_decode.c'
raw=subprocess.check_output(['git','-C',str(repo),'archive',pin,*dirs,loader])
with tarfile.open(fileobj=io.BytesIO(raw)) as a:a.extractall(root,filter='data')
verifier=repo/'tests/compiler/verify_decoder_corpus.py';rows=[]
def check(name,expected):
 p=subprocess.run([sys.executable,str(verifier),str(root),str(repo)],capture_output=True,text=True,timeout=5)
 correct=p.returncode==expected
 if expected:correct=correct and 'link' in p.stderr.lower()
 else:correct=correct and json.loads(p.stdout)['corpus_programs']==59
 rows.append({'case':name,'expected':expected,'actual':p.returncode,'pass':correct,'stderr':p.stderr})
check('ordinary',0)
paths=sorted({str(p) for d in dirs+[loader] for p in pathlib.PurePosixPath(d).parents if str(p)!='.'}|set(dirs))
# Each relocation preserves the exact original bytes, not a changed-byte refusal.
for n,name in enumerate(['.']+paths):
 p=root if name=='.' else root/name; target=out/('outside-'+str(n));p.rename(target);p.symlink_to(target,target_is_directory=True)
 try:check('symlink:'+name,1)
 finally:p.unlink();target.rename(p)
for n,name in enumerate([next((root/dirs[0]).glob('*.osc')).relative_to(root).as_posix(),loader]):
 p=root/name;before=hashlib.sha256(p.read_bytes()).hexdigest();link=out/('hardlink-'+str(n));os.link(p,link)
 try:
  check('hardlink:'+name,1)
  assert hashlib.sha256(p.read_bytes()).hexdigest()==before
 finally:link.unlink()
check('restored_ordinary',0)
(out/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
failed=[r for r in rows if not r['pass']];print(json.dumps({'checks':len(rows),'failed':len(failed),'passed':len(rows)-len(failed),'failures':failed[:12]}));sys.exit(bool(failed))
