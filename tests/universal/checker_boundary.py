#!/usr/bin/env python3
"""Observe retained checker2's IR-proof/native-byte trust boundary, not a checker."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

ap=argparse.ArgumentParser()
ap.add_argument('--tool-dir',required=True)
ap.add_argument('--out',required=True)
a=ap.parse_args()
root=Path(__file__).resolve().parents[2]
tools=Path(a.tool_dir).resolve();out=Path(a.out).resolve()
# Every campaign is fresh: never rewrite an earlier receipt or source artifact.
out.mkdir(parents=True,exist_ok=False)
commands=[]
def run(argv,want=0):
    p=subprocess.run(list(map(str,argv)),capture_output=True,text=True,timeout=60)
    commands.append({'argv':list(map(str,argv)),'exit':p.returncode,'stdout':p.stdout,'stderr':p.stderr})
    assert p.returncode==want, commands[-1]
    return p.stdout
source=root/'tests/checker2/lrat/p01_abs8.osc'
ir=tools/'checker2/lrat/p01_abs8.ir'
proof=tools/'checker2/lrat/p01_abs8.lrat'
c2=tools/'checker2/checker2'
oscc=tools/'compiler/oscc'
run([oscc,source,out/'original'])
assert (out/'original.ir').read_bytes()==ir.read_bytes(), 'independent emitted IR differs from proved IR'
code=(out/'original.code').read_bytes()
assert len(code)>=4 and len(code)%4==0, 'expected complete AArch64 image'
# Replace entry with AArch64 RET, then do NOT execute this untrusted image.
mutated=bytes.fromhex('c0035fd6')+code[4:]
assert mutated!=code, 'instruction mutation must change actual code'
(out/'mutated.code').write_bytes(mutated)
for name in ['original','mutated']:
    receipts=out/(name+'-receipts');receipts.mkdir()
    run([c2,'receipt','issue',ir,out/(name+'.code'),'--target','universal-623-boundary','--epoch','5','--expiry','9','--out-dir',receipts,'--lrat',str('abs8='+str(proof))])
    files=list(receipts.glob('*.rcpt'));assert len(files)==1
    text=run([c2,'receipt','verify',files[0],ir,out/(name+'.code'),'--target','universal-623-boundary','--epoch','6','--allow-test-key','--lrat','abs8='+str(proof)])
    assert text.strip()=='receipt=VALID',text
original=list((out/'original-receipts').glob('*.rcpt'))[0]
text=run([c2,'receipt','verify',original,ir,out/'mutated.code','--target','universal-623-boundary','--epoch','6','--allow-test-key','--lrat','abs8='+str(proof)],want=1)
assert text.strip()=='receipt=EXECUTABLE_MISMATCH',text
text=run([c2,'receipt','verify',original,ir,out/'original.code','--target','universal-623-boundary','--epoch','6','--lrat','abs8='+str(proof)],want=1)
assert text.strip()=='receipt=TEST_SIGNER_IN_PRODUCTION',text
report={'schema':'omega.universal.checker-boundary.experiment.v1','result':'OBSERVED_IR_PROPERTY_PROOF_ONLY','source_commit':run(['git','-C',root,'rev-parse','HEAD']).strip(),'existing_receipt_class':2,'proved_property':'checker2 bounded IR NO-TRAP','native_translation_relation':'UNKNOWN','executable_identity_binding':'TESTED_REFUSAL_ON_CHANGED_BYTES','original_and_newly_issued_mutated_code_receipts':'VALID_WITH_PUBLIC_TEST_KEY_AND_MATCHING_BYTES','production_test_key':'REFUSED','mutated_code_executed':False,'scope':'This is an existing-system limitation demonstration, not a new checker, lowering validator, production receipt or vulnerability claim. Newly issued TEST receipts do not prove native code behavior.','commands':commands,'inputs':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [source,ir,proof,c2,oscc,Path(__file__).resolve()]},'artifacts':{str(p.relative_to(out)):hashlib.sha256(p.read_bytes()).hexdigest() for p in out.rglob('*') if p.is_file()}}
(out/'experiment.json').write_text(json.dumps(report,indent=2)+'\n')
print('OBSERVED: IR NO-TRAP proofs and executable byte identity; native translation relation UNKNOWN; mutation not executed')
