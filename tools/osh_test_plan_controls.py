#!/usr/bin/env python3
"""Mutate only disposable Make plan fixtures; no compiler/runtime tests here."""
import pathlib,tempfile,subprocess,json,shutil,sys
repo=pathlib.Path(__file__).resolve().parents[1];(repo/'build').mkdir(exist_ok=True)
out=pathlib.Path(tempfile.mkdtemp(prefix='osh-plan-controls.',dir=repo/'build'))
source=(repo/'Makefile').read_text();legacy='osh-gen-check test-osh-lex test-osh-parse test-osh-expand test-osh-e2e osh-trace test-osh-host test-osh-caps test-osh-journal'
line=next(x for x in source.splitlines() if x.startswith('TESTS = '));qline=next(x for x in source.splitlines() if x.startswith('QUALIFY_TESTS = '))
variants=[('production',source,False),('old-plan',source.replace(line,'TESTS = '+legacy).replace(qline,'QUALIFY_TESTS ='),True),('duplicate',source.replace(line,line+' test-osh-host'),True),('undefined',source.replace(line,line+' test-osh-undefined'),True),('no-mutants',source.replace(qline,'QUALIFY_TESTS ='),True),('default-only',source.replace('.DEFAULT_GOAL := tree','.DEFAULT_GOAL := test-plan'),True)]
results=[]
for label,text,bad in variants:
 d=out/label;d.mkdir();(d/'Makefile').write_text(text);(d/'tools').mkdir();shutil.copy2(repo/'tools/osh_test_plan.py',d/'tools/osh_test_plan.py');shutil.copytree(repo/'mk',d/'mk')
 p=subprocess.run([sys.executable,str(d/'tools/osh_test_plan.py')],capture_output=True,text=True,timeout=15)
 report=json.loads(p.stdout);ok=p.returncode==int(bad)
 if label=='old-plan':ok=ok and len(report['missing'])==9
 if label=='duplicate':ok=ok and report['duplicates']
 if label=='undefined':ok=ok and report['undefined']==['test-osh-undefined']
 if label=='default-only':ok=ok and report['default_goal']=='test-plan'
 if label=='no-mutants':ok=ok and report['missing']==['qualify:test-osh-followon-mutants']
 results.append({'case':label,'exit':p.returncode,'pass':ok,'report':report})
(out/'results.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps({'cases':results,'output':str(out),'scope':'test-plan wiring only'}));sys.exit(not all(x['pass'] for x in results))
