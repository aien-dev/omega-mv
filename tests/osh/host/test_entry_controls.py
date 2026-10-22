#!/usr/bin/env python3
"""Compile entry-precondition faults; require exact old behavioral failures."""
import pathlib,subprocess,signal,json,sys,re
repo=pathlib.Path(sys.argv[1]).resolve();out=pathlib.Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=False)
source=(repo/'tests/osh/host/test_osh_host.c').read_text();oldlog=pathlib.Path(sys.argv[3]).read_text()
def failures(text):return [x.split(' -- ',1)[1].split(':',1)[0] for x in text.splitlines() if x.startswith('FAIL ') and ' -- ' in x]
original=failures(oldlog);assert len(original)==5
old=subprocess.check_output(['git','-C',str(repo),'show','3f9eee7178e0ba05ad847f81f4dd4d9e9f519c40:tests/osh/host/test_osh_host.c'],text=True)
assert source.split('/* The suite owns its entry preconditions')[0]==old.split('int main(int argc, char **argv)')[0]
entries=[('no-quit-default','if (sigaction(SIGINT, &sa, NULL) || sigaction(SIGQUIT, &sa, NULL)) return -1;','if (sigaction(SIGINT, &sa, NULL)) return -1;',original[:1]),('no-cloexec','fcntl(fd, F_SETFD, flags | FD_CLOEXEC)','fcntl(fd, F_SETFD, flags)',original[1:])]
rest=['src/osh/host/osh_req.c','src/osh/host/osh_vars.c','src/osh/host/osh_builtin.c','src/osh/host/osh_exec.c','src/osh/host/osh_journal.c','src/sha256.c'];reports=[]
def setup():signal.signal(signal.SIGINT,signal.SIG_DFL);signal.signal(signal.SIGQUIT,signal.SIG_IGN)
for name,before,after,expected in entries:
 assert source.count(before)==1
 folder=out/name;folder.mkdir();f=folder/'test.c';f.write_text(source.replace(before,after));build=folder/'build'
 args=['quiet-build','make','--no-print-directory','OUT_DIR='+str(build),'OSH_HOST_SRCS='+' '.join([str(f),*rest])]
 targets=[build/'osh-host/test_osh_host',build/'osh-host/test_osh_host_asan']
 p=subprocess.run(args+list(map(str,targets)),cwd=repo,capture_output=True,text=True,timeout=120);(folder/'compile.log').write_text(p.stdout+p.stderr);assert p.returncode==0
 for b in targets:assert b.read_bytes()[:4]==b'\x7fELF'
 p=subprocess.run(args+['test-osh-host'],cwd=repo,preexec_fn=setup,capture_output=True,text=True,timeout=150);(folder/'run.log').write_text(p.stdout+p.stderr);text=(build/'osh-host/host.out').read_text();actual=failures(text);ok=p.returncode==2 and actual==expected and 'OSH_HOST_FAIL' in text
 reports.append({'fault':name,'compile_exit':0,'make_exit':p.returncode,'expected_assertions':expected,'actual_assertions':actual,'pass':ok});print(json.dumps(reports[-1]),flush=True)
(out/'results.json').write_text(json.dumps({'original_test_assertion_prefix':'BYTE_IDENTICAL','results':reports},indent=2)+'\n');assert all(r['pass'] for r in reports)
