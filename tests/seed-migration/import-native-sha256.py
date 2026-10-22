from pathlib import Path
import subprocess,json,hashlib
w=Path.home()/'workspace/omega-osc-manual-migration';repo=Path.home()/'workspace/omega-retirement-integration';pin='12251e3403d42931ada27dc1c54b3b610954be5d'
def blob(path):return subprocess.check_output(['git','-C',str(repo),'show',pin+':'+path])
raw=blob('seed/retirement/trn1/trn1.om');source=raw.decode()
core=source[source.index('fn rotate '):source.index('fn hash ')]
constants=source[source.index('fn constants '):source.index('fn sayOk ')]
header='''note SPDX-License-Identifier: AGPL-3.0-or-later
note SHA256 core reused byte-for-byte from native Omega TRN1 at 12251e3403d42931ada27dc1c54b3b610954be5d.
note Reads stdin up to 8 MiB and emits lowercase hex plus LF only after successful EOF.
global Buf uchar 8192
global H uint 8
global Vv uint 8
global W uint 64
global K uint 64
global Hb uchar 64
global Hs ulong 2
global Hash uchar 32
global Out uchar 65
text Hex "0123456789abcdef"

'''
main=(w/'seed/native/sha256-main.om').read_text()
out=header+core+constants+main
(w/'seed/native/sha256.om').write_text(out)
for name in ['LICENSE','NOTICE']:(w/('seed/native/SHA256-'+name)).write_bytes(blob('seed/retirement/trn1/'+name))
r={'source_commit':pin,'source_path':'seed/retirement/trn1/trn1.om','source_sha256':hashlib.sha256(raw).hexdigest(),'reused_sections':[{'start':'fn rotate','end_exclusive':'fn hash','sha256':hashlib.sha256(core.encode()).hexdigest()},{'start':'fn constants','end_exclusive':'fn sayOk','sha256':hashlib.sha256(constants.encode()).hexdigest()}],'adapter':'seed/native/sha256-main.om','method':'exact native-Omega source reuse plus handwritten adapter; no C/Rust translation'}
assert out[out.index('fn rotate '):out.index('fn constants ')]==core
assert out[out.index('fn constants '):out.index('note Handwritten stream interface')]==constants
(w/'evidence/OSC-MANUAL-MIGRATION/sha256-provenance.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
