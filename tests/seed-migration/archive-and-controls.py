from pathlib import Path
import subprocess,json,hashlib
w=Path.home()/'workspace/omega-osc-manual-migration'
def git(*args):return subprocess.check_output(['git','-C',str(w),*args])
pin='57f5f4258cebf3d5b2c22853cd062e351265c525'
out=w/'evidence/OSC-MANUAL-MIGRATION'
refs=git('for-each-ref','--format=%(refname) %(objectname)','refs/remotes/origin').decode().splitlines()
(out/'remote-refs-at-archive.txt').write_text('\n'.join(refs)+'\n')
rows=[]
for line in git('ls-tree','-r',pin).decode().splitlines():
 meta,path=line.split('\t',1)
 if path.startswith(('src/compiler/','tests/compiler/','docs/osc/')) or 'osc' in path.lower() or path=='mk/compiler.mk':
  blob=meta.split()[2];data=git('cat-file','blob',blob)
  rows.append({'path':path,'blob':blob,'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data),'state':'historical evidence' if path.startswith('evidence/') else 'not retired'})
(out/'legacy-inventory.json').write_text(json.dumps({'source_pin':pin,'scope':'pinned lineage snapshot; branch-only variants retained by remote-ref inventory, not claimed migrated','files':rows},indent=2)+'\n')
print(json.dumps({'pinned_files':len(rows),'remote_refs_recorded':len(refs),'source_pin':pin}))
source=w/'seed/native/literal.om';original=source.read_bytes()
mutants=[('overflow','if over\nret 3','if false\nret 3'),('digit','ret sub ulong c 48','ret sub ulong c 47'),('capacity','if gt n long 4096','if gt n long 4097')]
results=[]
try:
 for name,old,new in mutants:
  text=original.decode();assert text.count(old)==1
  source.write_text(text.replace(old,new))
  p=subprocess.run(['python3',str(w/'tests/seed-migration/literal.py'),str(w)],capture_output=True,text=True)
  assert p.stdout, p.stderr
  report=json.loads(p.stdout)
  assert p.returncode==1 and report['failures']>0,(name,p.stdout,p.stderr)
  (out/('mutant-'+name+'.json')).write_text(p.stdout)
  results.append({'name':name,'compiled':True,'mismatches':report['failures'],'exit':p.returncode})
finally:source.write_bytes(original)
(out/'mutants.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(results))
