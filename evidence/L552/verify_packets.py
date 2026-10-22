#!/usr/bin/env python3
import hashlib,json,pathlib,subprocess,sys
R=pathlib.Path(__file__).resolve().parents[2];S=pathlib.Path('/home/drakestapleton/handoffs/omega-issue-sweep-20261022/L552');E=R/'evidence/L552';rows=[]
def git(*args):return subprocess.check_output(['git',*args],cwd=R)
pins={'decoder':('9d8807ddc306e204faa20ce2faeb48e215cdb5af','676a0b80ef71fba1cf5360da2e6cb9186789dfb5'),'protocol':('dc24fcf27bbc6c36ca6dfcac020452bb06004bae','9869cbcb9eb7e2b39673a4208785606826385cd9'),'trace':('2dee6673dc6df329ee4b4913069affd1ea307e29','a00ef9b5089fd37876caece268e2be9601d20158')}
for k,(base,head) in pins.items():
 paths=git('diff','--name-only',base,head).decode().splitlines();production=[p for p in paths if not p.startswith('evidence/') and not p.endswith('.crumb')]
 (E/(k+'-delta.patch')).write_bytes(git('diff',base,head,'--',*production))
 # Every preexisting evidence file is immutable, not merely the named rows.
 changed_old=[p for p in git('diff','--name-only','--diff-filter=MD',base,head,'--','evidence').decode().splitlines()]
 assert not changed_old
 rows.append({'artifact':k,'base':base,'head':head,'source_test_contract_changes':production,'old_evidence_modified_or_deleted':changed_old})
assert git('diff',pins['decoder'][0],pins['decoder'][1],'--','src/compiler','tests/compiler/reproduce_pr499_decoder.sh')==b''
# Full decoder source gate is inspected retained evidence, not rerun by L552.
d=S/'decoder/evidence/L547-DECODER-LINKS'
assert (d/'corpus-manifest.json').read_bytes()==(d/'corpus-manifest-post.json').read_bytes()
for f in ('ir_decode.out','ir_decode_asan.out'):
 t=(d/f).read_text();assert 'units=59 round_trip_ok=59 compile_refused=0' in t and '22/22' in t and '5/5' in t
log=(d/'full-gate.log').read_text();assert 'test-osc-ir-decode-mutants: PASS (10 of 10 caught)' in log
# Published five mutants: reuse exact binaries after their independent SHA checks.
mutants=[]
for row in json.loads((S/'protocol/evidence/L547-PROTOCOL/mutants-summary.json').read_text())['results']:
 name=row['mutant'];p=pathlib.Path('/home/drakestapleton/workspace/targets/L547-native-protocol-final/mutants')/name
 assert hashlib.sha256(p.read_bytes()).hexdigest()==row['binary_sha256'];assert row['compile_exit']==0
 r=subprocess.run([sys.executable,str(S/'protocol/research/exploration-600/continuation/test_guard.py'),str(p)],capture_output=True,timeout=20)
 (E/('native-mutant-'+name+'.log')).write_bytes(r.stdout+r.stderr)
 assert r.returncode==1 and json.loads(r.stdout)['failed']>0
 mutants.append({'name':name,'sha256':row['binary_sha256'],'prior_compile_exit':0,'independent_test_exit':r.returncode,'behavior_failures':json.loads(r.stdout)['failed']})
(E/'packet-verification.json').write_text(json.dumps({'pins_and_deltas':rows,'decoder_retained_gate_checked_not_rerun':True,'native_mutants_reused_hash_verified':mutants},indent=2)+'\n')
print(json.dumps({'artifacts':len(rows),'old_evidence_unchanged':True,'decoder_retained_gate_inspected':True,'native_mutants_behaviorally_caught':len(mutants)}))
