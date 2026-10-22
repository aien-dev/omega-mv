import json,os,pathlib,shutil,subprocess,sys
repo=pathlib.Path(__file__).resolve().parents[2];s=pathlib.Path('/home/drakestapleton/handoffs/omega-issue-sweep-20261022');f=s/'L541-extra-boundaries';shutil.copytree(s/'L541-corpus-tests/export',f);cmd=[sys.executable,str(repo/'tests/compiler/verify_decoder_corpus.py'),str(f),str(repo)];results=[]
def check(name,want):
 p=subprocess.run(cmd,capture_output=True,text=True,timeout=5);results.append({'case':name,'expected':want,'actual':p.returncode,'pass':p.returncode==want,'stderr':p.stderr})
loader=f/'tests/compiler/test_osc_ir_decode.c';data=loader.read_bytes();loader.unlink();check('missing_loader',1);loader.write_bytes(data)
loader.unlink();loader.symlink_to(s/'L541-corpus-tests/export/tests/compiler/test_osc_ir_decode.c');check('linked_loader',1);loader.unlink();loader.write_bytes(data)
p=f/'src/osh';outside=s/'L541-extra-osh';p.rename(outside);check('missing_directory',1);p.symlink_to(outside);check('linked_directory',1);p.unlink();outside.rename(p)
p=next((f/'tests/compiler/progs').glob('*.osc'));data=p.read_bytes();p.unlink();os.mkfifo(p);check('FIFO_program',1);p.unlink();p.mkdir();check('directory_program',1);p.rmdir();p.write_bytes(data)
p=f/'src/osh/descendants';p.mkdir();(p/'ignored.osc').write_text('ignored nested file');check('nested_program_not_loaded',0)
(repo/'evidence/L541/corpus-extra-boundaries.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps({'checks':len(results),'failed':sum(not r['pass'] for r in results)}));sys.exit(any(not r['pass'] for r in results))
