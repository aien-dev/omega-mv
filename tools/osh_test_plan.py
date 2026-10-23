#!/usr/bin/env python3
"""Audit the module's actual default test plan without materializing Omega."""
import json,pathlib,subprocess,sys,tempfile
repo=pathlib.Path(__file__).resolve().parents[1]
legacy='osh-gen-check test-osh-lex test-osh-parse test-osh-expand test-osh-e2e osh-trace test-osh-host test-osh-caps test-osh-journal'.split()
added='test-osh-diff test-osh-prompt-signals test-osh-prompt-interrupt test-osh-prompt-pty test-osh-exit-numeric-priority test-osh-exit-arguments test-osh-reader-boundaries test-osh-exit-abort-status'.split()
rule='l570-print-plan:;@printf "tests=%s\\nqualify=%s\\ndefault=%s\\n" "$(TESTS)" "$(QUALIFY_TESTS)" "$(.DEFAULT_GOAL)"'
(repo/'build').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='osh-plan-query.',dir=repo/'build') as folder:
 probe=pathlib.Path(folder)/'query.mk';probe.write_text(rule+'\n')
 p=subprocess.run(['make','--no-print-directory','-s','-f','Makefile','-f',str(probe),'l570-print-plan'],cwd=repo,capture_output=True,text=True,timeout=10)
if p.returncode:print(p.stderr,file=sys.stderr);sys.exit(p.returncode)
fields=dict(line.split('=',1) for line in p.stdout.splitlines() if '=' in line)
tests=fields.get('tests','').split();qualify=fields.get('qualify','').split()
missing=[x for x in legacy+added if x not in tests]
if 'test-osh-followon-mutants' not in qualify:missing.append('qualify:test-osh-followon-mutants')
defined=set()
for f in (repo/'mk').glob('*.mk'):
 for line in f.read_text().splitlines():
  if line and not line[0].isspace() and ':' in line:defined.update(line.split(':',1)[0].split())
undefined=[x for x in tests+qualify if x not in defined]
report={'tests':tests,'qualify':qualify,'missing':missing,'undefined':undefined,'duplicates':len(tests)!=len(set(tests)),'default_goal':fields.get('default'),'scope':'test-plan wiring only, no executable qualification'}
print(json.dumps(report,sort_keys=True));sys.exit(bool(missing or undefined or report['duplicates'] or report['default_goal']!='tree'))
