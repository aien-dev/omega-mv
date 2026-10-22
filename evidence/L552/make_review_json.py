#!/usr/bin/env python3
import json,pathlib,hashlib,subprocess
E=pathlib.Path(__file__).resolve().parent;R=E.parents[1]
p=json.loads((E/'packet-verification.json').read_text())
review={'schema':'omega.L552.corrective-review.v1','reviewer':'L552','role':'INDEPENDENT_REVIEW_ONLY','new_findings':[],'artifacts':[],'desk_disposition':'REVIEW_ONLY','source_changes_by_reviewer':False,'integration_or_issue_closure':False,'gaps':{'current35B_shared_train_hardware':'NOT_RUN','native_callback_OSC_ABI_integration':'NOT_RUN','real_training':'NOT_RUN','fresh_heldout_science':'NOT_RUN','whole_repository_acceptance':'NOT_RUN','physical_performance':'NOT_RUN','energy':'UNMEASURED'},'prior_evidence_inspected_not_rerun':['decoder_full_plain_ASan_59each_22refusals_5oracles_10compiledmutants','original600_2337checks_40diagnostics_6controls','native_mutant_compilation_success_provenance'],'preserved_review_helper_failure':{'log':'packet-verification-initial-failure.log','exit':1,'cause':'expected 10/10 instead of actual 10 of 10 caught','final_exit':0}}
for x in p['pins_and_deltas']:
 x=dict(x);x['verdict']='ACCEPT_HOST_SCOPE'
 if x['artifact']=='decoder':
  x.update(resolved=[{'id':'L541-B1','prior_severity':'P2','file':'tests/compiler/verify_decoder_corpus.py','lines':[12,13,17,35,43,44,47,52],'status':'RESOLVED','repro':'run_checks.py: decoder-red old exit1/7 failed; corrected exit0/13 passed'}],independently_run={'new_link_tests':13,'old_failing_cases':7,'original_wrong_byte_and_completeness':67,'extra_readonly_and_trust_boundary':6},scope='Static trusted caller-owned export root and descendants, historical PR499 only')
 elif x['artifact']=='protocol':
  x.update(resolved=[{'id':'L541-A1','prior_severity':'P3','file':'research/exploration-600/continuation/guard.om','lines':[53,54,55,56,57,58],'status':'RESOLVED','repro':'test_protocol.py on old native ELF exit1/3 failures; corrected exit0/16 passed'},{'id':'L541-A2','prior_severity':'P3','file':'research/exploration-600/continuation/CONTRACT.md','lines':[11],'status':'RESOLVED','repro':'protocol_extra.py proves 16383 accepted /16384 refused20 no-output for six whitespace padding variants'}],independently_run={'original_oracle':423,'protocol':16,'extra_protocol':342,'replay_rows_byte_identical':600,'reused_hash_verified_behavioral_mutants_caught':5},original_science='FAIL_UNCHANGED',default_guard='REJECTED',scope='Native bounded offline protocol compatibility; synthetic known-prefix replay only')
 else:
  x.update(resolved=[{'id':'parent-startup-reservation','prior_severity':None,'file':'tools/trace/archive.mjs','lines':[134,135,136,137,163,170],'status':'RESOLVED','repro':'startup_probe.mjs old fixed-expectation exit1 ERR_ASSERTION; corrected exit0: two fsync failure sites each acceptfalse/submitted0/pending0; baseline bytes unchanged'}],independently_run={'full_host_tests':51,'full_host_skips':0,'native_oracle_assertions':12,'offline_base_checks':8,'original_behavioral_mutants_caught':17,'followon_behavioral_mutants_caught':9,'denied_network_followon_checks':14,'network_trace_bytes':0,'startup_cases':5},scope='Synchronous startup refusal and synthetic host archive/recovery regression')
 review['artifacts'].append(x)
(E/'L552-review.json').write_text(json.dumps(review,indent=2)+'\n')
# Pin tested source/input files to exact author Git blobs and SHA256.
records=[]
for scope,head,paths in [('decoder','676a0b80',['tests/compiler/verify_decoder_corpus.py','tests/compiler/test_decoder_path_links.py','tests/compiler/test_decoder_corpus_closure.py','tests/compiler/reproduce_pr499_decoder.sh']),('protocol','9869cbcb',['research/exploration-600/continuation','research/exploration-600/policy.osc','research/exploration-600/FREEZE.md','evidence/EXPLORATION-600/campaign/run-002/heldout']),('trace','a00ef9b5',['tools/trace','tests/trace','argus.lock','evidence/L489/synthetic-searchtrace.txt'])]:
 for line in subprocess.check_output(['git','ls-tree','-r',head,'--',*paths],cwd=R).decode().splitlines():
  meta,name=line.split('\t');mode,kind,oid=meta.split()
  if kind!='blob':continue
  raw=subprocess.check_output(['git','show',head+':'+name],cwd=R)
  exported=pathlib.Path('/home/drakestapleton/handoffs/omega-issue-sweep-20261022/L552')/scope/name
  assert exported.read_bytes()==raw
  records.append({'scope':scope,'head':head,'path':name,'git_blob':oid,'sha256':hashlib.sha256(raw).hexdigest(),'bytes':len(raw),'export_matches':True})
(E/'tested-source-input-pins.json').write_text(json.dumps(records,indent=2)+'\n')
print(json.dumps({'review_artifacts':3,'all_verdicts':'ACCEPT_HOST_SCOPE','new_findings':0,'source_input_pins':len(records)}))
