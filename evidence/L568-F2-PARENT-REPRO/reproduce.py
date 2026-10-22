import pathlib,json,subprocess,hashlib
h=pathlib.Path.home();r=h/'workspace/omega-osh-batch-corrective-review-20261022';binary=r/'build/L568/core/osh/osh';pins=json.loads((r/'evidence/L568-REVIEW/binary-pins.json').read_text());assert hashlib.sha256(binary.read_bytes()).hexdigest()==pins[str(binary)]['sha256'];out=h/'workspace/targets/L568-parent-F2-clean';out.mkdir(parents=True,mode=0o700,exist_ok=False);assert out.stat().st_mode&0o777==0o700
program='exit bad 9\nprintf "tail\\n"\n';script=out/'case.sh';script.write_text(program);env={'PATH':'/usr/bin:/bin','LC_ALL':'C','HOME':str(out),'XDG_STATE_HOME':str(out)};rows=[]
for mode in ('command','script','stdin'):
 args=['-c',program] if mode=='command' else [str(script)] if mode=='script' else [];stdin=program if mode=='stdin' else None
 b=subprocess.run(['/bin/bash','--noprofile','--norc',*args],input=stdin,env=env,capture_output=True,text=True,timeout=5);assert b.returncode==2 and b.stdout=='' and 'numeric argument required' in b.stderr
 for engine in ('native','interp'):
  p=subprocess.run([str(binary),'--journal',str(out/(mode+'-'+engine+'.journal')),*args],input=stdin,env={**env,'OSH_INTERP':'1' if engine=='interp' else '0'},capture_output=True,text=True,timeout=5);assert 'journal unavailable' not in p.stderr
  rows.append({'mode':mode,'engine':engine,'bash_exit':b.returncode,'bash_stdout':b.stdout,'candidate_exit':p.returncode,'candidate_stdout':p.stdout,'candidate_stderr':p.stderr,'matches_bash':p.returncode==b.returncode and p.stdout==b.stdout})
assert len(rows)==6 and all(not x['matches_bash'] for x in rows);report={'source':'7177ad74cfb5215fa478c270f198af4e07cb33c5','binary_sha256':pins[str(binary)]['sha256'],'rows':rows,'initial_probe':'L568-parent-F2 retained with journal-directory0775 warning; this fresh0700 probe has no journal-refusal warning'};(out/'results.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
