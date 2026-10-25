#!/usr/bin/env python3
"""Host build orchestration only; all lowering computation is seeded Omega (translation-v1 section 5)."""
from pathlib import Path
import hashlib,json,os,struct,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
build=ROOT/'build/L831'
for p in [ROOT/'build',build]:
    if p.is_symlink():raise SystemExit('LOWER_UNSAFE_BUILD_PATH')
build.mkdir(parents=True,exist_ok=True)
# The section-1 native realization compiler, born from the seed by seed/omega-from-seed.sh.
# It is the only pinned compiler with argv (argcnt/argread), which `lower NAME` needs.
expected='734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61'
if not os.environ.get('OMEGA_NATIVE_OMC'):raise SystemExit('LOWER_COMPILER_UNSET: set OMEGA_NATIVE_OMC to the pinned omc')
compiler=Path(os.environ['OMEGA_NATIVE_OMC'])
assert compiler.is_file() and hashlib.sha256(compiler.read_bytes()).hexdigest()==expected,'LOWER_COMPILER_PIN_MISMATCH'
# Unchanged vendor SHA256 and the unchanged #621 core (its `decode` is the full source-aware v1 check).
parts=[ROOT/'src/source-aware/vendor/sha256.om',ROOT/'src/source-aware/core.om',ROOT/'src/adapters/upper-lower.om']
subprocess.run(['sha256sum','-c','tools/source-aware/imports.sha256'],cwd=ROOT,check=True,stdout=subprocess.PIPE)
program=b'\n'.join(p.read_bytes() for p in parts)+b'\n'
result=subprocess.run([str(compiler)],input=program,capture_output=True,timeout=60)
if result.returncode:raise SystemExit('LOWER_COMPILE_FAILED: '+result.stderr.decode(errors='replace'))
binary=result.stdout
assert len(binary)>64 and binary[:4]==b'\x7fELF' and struct.unpack_from('<HH',binary,16)==(2,183),'LOWER_NOT_AARCH64_EXEC'
report={'schema':'omega.upper-lower.build.v1','compiler_sha256':expected,'compiler_origin':'omega-mv seed/out/omc via seed/omega-from-seed.sh (translation-v1 section 1)','inputs':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in parts},'combined_source_sha256':hashlib.sha256(program).hexdigest(),'binary_sha256':hashlib.sha256(binary).hexdigest(),'host_orchestration':'Python process/filesystem only; no host parser or compute fallback in the native artifact'}
with tempfile.TemporaryDirectory(prefix='.lower-build-',dir=build) as temp:
    t=Path(temp)
    (t/'program.om').write_bytes(program);(t/'lower').write_bytes(binary);(t/'lower').chmod(0o700)
    (t/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    for name in ['program.om','lower','build.json']:os.replace(t/name,build/name)
print(build/'lower')
