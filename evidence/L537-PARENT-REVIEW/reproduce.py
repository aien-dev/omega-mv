import pathlib,subprocess,json,datetime,hashlib,os
home=pathlib.Path.home(); repo=home/'workspace/omega-physics-build-concurrency-fix-20261022'; stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ'); out=home/'workspace/targets'/('L537-parent-'+stamp);out.mkdir(parents=True,exist_ok=False)
head=subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip();assert head=='d84142f73f0fd0e44d22893232fdf0ee2a46a4d4'
results=[]
for kind in ('symlink','hardlink'):
 d=out/kind;d.mkdir(); build=d/'build';build.mkdir();victim=d/'synthetic-victim.txt';victim.write_bytes(b'PRIVATE SYNTHETIC VICTIM MUST REMAIN INTACT\n'); lock=pathlib.Path(str(build)+'.omega-build.lock')
 if kind=='symlink':lock.symlink_to(victim)
 else:os.link(victim,lock)
 before=hashlib.sha256(victim.read_bytes()).hexdigest(); p=subprocess.run(['sh',str(repo/'tools/with_build_lock.sh'),str(build),'true'],capture_output=True,text=True,timeout=10);after=hashlib.sha256(victim.read_bytes()).hexdigest()
 results.append({'case':kind,'exit':p.returncode,'victim_preserved':before==after,'before_sha256':before,'after_sha256':after,'stdout':p.stdout,'stderr':p.stderr})
(out/'linked-lock-probes.json').write_text(json.dumps({'source':head,'probes':results},indent=2)+'\n');print(json.dumps({'source':head,'output':str(out),'probes':results}),flush=True)
p=subprocess.run(['quiet-build','timeout','180s','node','tests/physics_build_concurrency_controls.mjs',str(out/'acceptance')],cwd=repo,capture_output=True,text=True,timeout=200); (out/'acceptance.log').write_text(p.stdout+p.stderr); (out/'acceptance.exit').write_text(str(p.returncode)+'\n'); print('ACCEPTANCE_EXIT='+str(p.returncode));print(p.stdout[-12000:]);print(p.stderr[-2000:]);print('PARENT_OUTPUT='+str(out))
