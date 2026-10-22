#!/usr/bin/env python3
import hashlib,json,pathlib,subprocess,sys,os
repo=pathlib.Path(sys.argv[1]);O=pathlib.Path(sys.argv[2]);root=O/'decoder-green/export';v=repo/'tests/compiler/verify_decoder_corpus.py';rows=[]
def snapshot(p):
 result=[]
 for f in sorted(p.rglob('*')):
  st=f.lstat(); result.append([str(f.relative_to(p)),st.st_mode,st.st_nlink,f.readlink().as_posix() if f.is_symlink() else hashlib.sha256(f.read_bytes()).hexdigest() if f.is_file() else None])
 return result
# Snapshot test side effects separately from verifier read-only behavior.
def check(name,arg,want,watch):
 before=[snapshot(p) for p in watch];p=subprocess.run([sys.executable,str(v),str(arg),str(repo)],capture_output=True,text=True,timeout=5)
 unchanged=before==[snapshot(p) for p in watch];good=p.returncode==want and unchanged
 rows.append({'case':name,'expected':want,'exit':p.returncode,'unchanged':unchanged,'pass':good,'stderr':p.stderr})
 if want==0:assert json.loads(p.stdout)['corpus_programs']==59
 assert good
check('ordinary',root,0,[root])
target=O/'extra-relocated-compiler';p=root/'tests/compiler';p.rename(target);p.symlink_to(target,target_is_directory=True)
try:
 check('static-compiler-ancestor',root,1,[root,target])
 # Wrong bytes in linked subtree remain refused. Mutate only our synthetic copy.
 f=next((target/'progs').glob('*.osc'));saved=f.read_bytes();f.write_bytes(saved+b'\n')
 try:check('wrong-byte-linked-ancestor',root,1,[root,target])
 finally:f.write_bytes(saved)
finally:p.unlink();target.rename(p)
f=root/'tests/compiler/test_osc_ir_decode.c';link=O/'extra-loader-hardlink';os.link(f,link)
try:check('hardlinked-loader',root,1,[root])
finally:link.unlink()
# Symlink belongs to an ancestor OUTSIDE the caller's chosen root: allowed by contract.
alias=O/'trusted-parent-alias';alias.symlink_to(root.parent,target_is_directory=True)
check('trusted-outside-ancestor',alias/root.name,0,[root])
check('restored',root,0,[root])
print(json.dumps({'checks':len(rows),'failed':sum(not r['pass'] for r in rows),'rows':rows}))
