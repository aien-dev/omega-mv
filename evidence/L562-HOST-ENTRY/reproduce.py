import pathlib,subprocess,signal,json,sys
h=pathlib.Path.home();repo=h/'workspace/omega-osh-host-test-entry';phase=sys.argv[1];out=h/'workspace/targets'/('L562-'+phase);assert not out.exists();log=h/'handoffs/omega-issue-sweep-20261022'/('L562-'+phase+'.log')
def inherited_environment():
 signal.signal(signal.SIGINT,signal.SIG_DFL);signal.signal(signal.SIGQUIT,signal.SIG_IGN)
p=subprocess.run(['quiet-build','make','--no-print-directory','OUT_DIR='+str(out),'test-osh-host'],cwd=repo,preexec_fn=inherited_environment,capture_output=True,text=True,timeout=200);log.write_text(p.stdout+p.stderr)
rows=[line for line in (p.stdout+p.stderr).splitlines() if any(s in line for s in ('FAIL','checks passed','test-osh-host: PASS'))]
report={'phase':phase,'command':['quiet-build','make','--no-print-directory','OUT_DIR='+str(out),'test-osh-host'],'entry_SIGINT':'DFL','entry_SIGQUIT':'IGN','exit':p.returncode,'log':str(log),'summary':rows};print(json.dumps(report));(log.with_suffix('.json')).write_text(json.dumps(report,indent=2)+'\n')
if phase=='red':assert p.returncode!=0 and any('failed 5' in s for s in rows)
else:assert p.returncode==0 and 'test-osh-host: PASS (execution service; plain and ASan/UBSan)' in p.stdout
