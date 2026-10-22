#!/usr/bin/env python3
"""L552 host review only. Own outputs, stable synthetic inputs, no author edits."""
import hashlib,json,pathlib,subprocess,sys,os,gzip
ROOT=pathlib.Path(__file__).resolve().parents[2]
S=pathlib.Path('/home/drakestapleton/handoffs/omega-issue-sweep-20261022/L552')
O=pathlib.Path('/home/drakestapleton/workspace/targets/L552-review')
E=ROOT/'evidence/L552'; records=[]
def run(name,args,cwd=ROOT,env=None):
 p=subprocess.run(list(map(str,args)),cwd=cwd,env=env,capture_output=True,timeout=180)
 (E/(name+'.log')).write_bytes(p.stdout+p.stderr)
 row={'name':name,'argv':list(map(str,args)),'cwd':str(cwd),'exit':p.returncode};records.append(row)
 (E/'commands.json').write_text(json.dumps(records,indent=2)+'\n')
 print(name,p.returncode,flush=True);return p
repo=ROOT
# New regression against exact old source, then fixed source. Inject only the verifier
# into an OWNED harness directory; original exports remain unchanged.
h=O/'decoder-old-harness';(h/'tests/compiler').mkdir(parents=True)
(h/'tests/compiler/verify_decoder_corpus.py').write_bytes((S/'decoder-old/tests/compiler/verify_decoder_corpus.py').read_bytes())
# Regression harness needs Git access; use an explicit symlink to repository object metadata.
(h/'.git').write_text((ROOT/'.git').read_text())
p=run('decoder-red',[sys.executable,S/'decoder/tests/compiler/test_decoder_path_links.py',h,O/'decoder-red'])
assert p.returncode==1 and json.loads(p.stdout)['failed']==7
p=run('decoder-green',[sys.executable,S/'decoder/tests/compiler/test_decoder_path_links.py',ROOT,O/'decoder-green'])
# ROOT starts at corrected decoder pin; verify exact source equality.
assert (ROOT/'tests/compiler/verify_decoder_corpus.py').read_bytes()==(S/'decoder/tests/compiler/verify_decoder_corpus.py').read_bytes()
assert p.returncode==0
p=run('decoder-original67',[sys.executable,S/'decoder/tests/compiler/test_decoder_corpus_closure.py',ROOT,S/'decoder/tests/compiler/verify_decoder_corpus.py',O/'decoder-original67'])
assert p.returncode==0
for name in ['decoder-red','decoder-green']:
 (E/(name+'.json')).write_bytes((O/name/'results.json').read_bytes())
# Published ELF copied only after hash verification and copied hash rechecked.
source=pathlib.Path('/home/drakestapleton/workspace/targets/L547-native-protocol-final/guard')
b=source.read_bytes();assert hashlib.sha256(b).hexdigest()=='b5afc755b60012ed907567c8670739bebdeb68727478d4e8c0602174a279bc43'
binary=O/'guard';binary.write_bytes(b);binary.chmod(0o755)
old=pathlib.Path('/home/drakestapleton/handoffs/omega-issue-sweep-20261022/L541-guard')
p=run('protocol-red',[sys.executable,S/'protocol/research/exploration-600/continuation/test_protocol.py',old]);assert p.returncode==1 and json.loads(p.stdout)['failed']==3
p=run('protocol-green',[sys.executable,S/'protocol/research/exploration-600/continuation/test_protocol.py',binary]);assert p.returncode==0
p=run('protocol-original423',[sys.executable,S/'protocol/research/exploration-600/continuation/test_guard.py',binary]);assert p.returncode==0
p=run('protocol-extra',[sys.executable,E/'protocol_extra.py',binary]);assert p.returncode==0
p=run('replay',[sys.executable,S/'protocol/research/exploration-600/continuation/replay.py',binary,O/'replay']);assert p.returncode==0
csv=(O/'replay/rows.csv').read_bytes();published=gzip.decompress((S/'protocol/evidence/L547-PROTOCOL/counterfactual-rows.csv.gz').read_bytes());assert csv==published
original=subprocess.check_output(['git','show','dc24fcf2:evidence/EX600-CONTINUATION-L523/counterfactual-rows.csv'],cwd=ROOT)
assert csv==original
(E/'replay-summary.json').write_bytes((O/'replay/summary.json').read_bytes())
(E/'replay-identity.json').write_text(json.dumps({'bytes':len(csv),'sha256':hashlib.sha256(csv).hexdigest(),'rows':600,'corrected_and_original_byte_identical':True})+'\n')
