#!/usr/bin/env python3
"""Exercise the real canonical source linker against isolated git fixtures."""
from pathlib import Path
import os, shutil, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
HOME=Path.home()/'handoffs/L457'
HOME.mkdir(parents=True,exist_ok=True)

def command(*args,cwd=None,env=None):
    return subprocess.run([str(x) for x in args],cwd=cwd,env=env,capture_output=True,text=True)

def fixture(base):
    omega=base/'omega'; cudapp=base/'cudapp'
    (omega/'tools').mkdir(parents=True); (omega/'src').mkdir()
    (cudapp/'src').mkdir(parents=True)
    shutil.copy2(ROOT/'tools/cudapp-link.sh',omega/'tools/cudapp-link.sh')
    (cudapp/'src/omega_gpu_fixture.c').write_text('int canonical_fixture(void) { return 7; }\n')
    (cudapp/'Makefile').write_text('all:\n\ttrue\n')
    for args in [('init','-q'),('add','.'),('-c','user.name=Test','-c','user.email=test@invalid','commit','-qm','fixture')]:
        result=command('git',*args,cwd=cudapp); assert result.returncode==0,result.stderr
    pin=command('git','rev-parse','HEAD',cwd=cudapp).stdout.strip()
    (omega/'cudapp.lock').write_text(pin+'\n')
    (omega/'cudapp.files').write_text('src/omega_gpu_fixture.c\n')
    (omega/'cudapp.stay').write_text('src/omega_blackwell_gates.c\nsrc/omega_blackwell_gates.h\n')
    return omega,cudapp

def main():
    cases=['valid','missing-checkout','wrong-pin','disabled-lock','dirty-source','dirty-buildfile','ignored-source','malformed-pin','shadow-copy','unmapped-gpu','unsafe-map','source-symlink']
    checks=0
    for case in cases:
        with tempfile.TemporaryDirectory(prefix='canonical-',dir=HOME) as temp:
            base=Path(temp); omega,cudapp=fixture(base)
            env=dict(os.environ); env.pop('CUDAPP_LOCK_CHECK',None)
            target=cudapp
            if case=='missing-checkout': target=base/'absent'
            elif case=='wrong-pin': (omega/'cudapp.lock').write_text('0'*40+'\n')
            elif case=='disabled-lock': env['CUDAPP_LOCK_CHECK']='0'
            elif case=='dirty-source': (cudapp/'src/omega_gpu_fixture.c').write_text('int divergent;\n')
            elif case=='dirty-buildfile': (cudapp/'Makefile').write_text('all:\n\tfalse\n')
            elif case=='malformed-pin':
                pin=(omega/'cudapp.lock').read_text().strip()
                (omega/'cudapp.lock').write_text(pin[:20]+' '+pin[20:]+'\n')
            elif case=='ignored-source':
                result=command('git','rm','--cached','src/omega_gpu_fixture.c',cwd=cudapp)
                assert result.returncode==0,result.stderr
                (cudapp/'.gitignore').write_text('/src/omega_gpu_fixture.c\n')
                assert command('git','add','.gitignore',cwd=cudapp).returncode==0
                result=command('git','-c','user.name=Test','-c','user.email=test@invalid','commit','-qm','ignored source',cwd=cudapp)
                assert result.returncode==0,result.stderr
                (omega/'cudapp.lock').write_text(command('git','rev-parse','HEAD',cwd=cudapp).stdout.strip()+'\n')
            elif case=='shadow-copy': (omega/'src/omega_gpu_fixture.c').write_text('int shadow;\n')
            elif case=='unmapped-gpu': (omega/'src/omega_gpu_hidden.c').write_text('int hidden;\n')
            elif case=='unsafe-map': (omega/'cudapp.files').write_text('../escape\n')
            elif case=='source-symlink':
                (cudapp/'src/omega_gpu_fixture.c').unlink()
                (base/'outside').write_text('int outside;\n')
                (cudapp/'src/omega_gpu_fixture.c').symlink_to(base/'outside')
                command('git','add','.',cwd=cudapp)
                result=command('git','-c','user.name=Test','-c','user.email=test@invalid','commit','-qm','symlink',cwd=cudapp)
                assert result.returncode==0,result.stderr
                (omega/'cudapp.lock').write_text(command('git','rev-parse','HEAD',cwd=cudapp).stdout.strip()+'\n')
            result=command('sh',omega/'tools/cudapp-link.sh',target,env=env)
            if case=='valid':
                assert result.returncode==0,(case,result.stdout,result.stderr)
                link=omega/'src/omega_gpu_fixture.c'
                assert link.is_symlink() and link.resolve()==cudapp/'src/omega_gpu_fixture.c'
            else:
                assert result.returncode!=0,(case,'must refuse canonical binding',result.stdout,result.stderr)
            checks+=1
            print('PASS',case,flush=True)
    print('CANONICAL LINK PASS',checks,'behavioral checks')
if __name__=='__main__': main()
