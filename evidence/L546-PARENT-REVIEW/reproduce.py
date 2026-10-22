import pathlib,subprocess,datetime,json
h=pathlib.Path.home();r=h/'workspace/omega-build-lock-file-safety-20261022';sha='c100a6eb145e47dc669f4f0422c60b968496025e';assert subprocess.check_output(['git','-C',str(r),'rev-parse','HEAD'],text=True).strip()==sha
out=h/'workspace/targets'/('L546-parent-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ'));out.mkdir(parents=True,exist_ok=False)
rows=[]
for label,script in [('safety','tests/build_lock_file_safety_controls.mjs'),('concurrency','tests/physics_build_concurrency_controls.mjs'),('history','tests/physics_header_identity_controls.mjs')]:
 p=subprocess.run(['quiet-build','timeout','150s','node',script,str(out/label)],cwd=r,capture_output=True,text=True,timeout=170);(out/(label+'.log')).write_text(p.stdout+p.stderr);rows.append({'label':label,'exit':p.returncode,'command':['node',script,str(out/label)]}); print(label+' EXIT='+str(p.returncode)+'\n'+p.stdout[-7000:],flush=True)
 if p.returncode:break
(out/'results.json').write_text(json.dumps({'source':sha,'output':str(out),'results':rows},indent=2)+'\n');print('PARENT_OUTPUT='+str(out));assert len(rows)==3 and all(x['exit']==0 for x in rows)
