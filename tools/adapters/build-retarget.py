#!/usr/bin/env python3
"""Host build orchestration only; all retarget computation is seeded Omega (translation-compose-v2 section 2)."""
from pathlib import Path
import hashlib,json,os,struct,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
build=ROOT/'build/L833'
for p in [ROOT/'build',build]:
    if p.is_symlink():raise SystemExit('RETARGET_UNSAFE_BUILD_PATH')
build.mkdir(parents=True,exist_ok=True)
# The translation-v1 section-1 native realization compiler; it has argv (argcnt/argread) for `retarget M`.
expected='734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61'
if not os.environ.get('OMEGA_NATIVE_OMC'):raise SystemExit('RETARGET_COMPILER_UNSET: set OMEGA_NATIVE_OMC to the pinned omc')
compiler=Path(os.environ['OMEGA_NATIVE_OMC'])
assert compiler.is_file() and hashlib.sha256(compiler.read_bytes()).hexdigest()==expected,'RETARGET_COMPILER_PIN_MISMATCH'
# Unchanged vendor SHA256 and the unchanged #621 v2 codec (its `decode` is the full source-aware v1/v2 check).
parts=[ROOT/'src/source-aware/vendor/sha256.om',ROOT/'src/source-aware/core-v2.om',ROOT/'src/adapters/upper-retarget.om']
for p in parts:
    if not p.is_file():raise SystemExit('RETARGET_MISSING_SOURCE: '+str(p.relative_to(ROOT)))
subprocess.run(['sha256sum','-c','tools/source-aware/imports.sha256'],cwd=ROOT,check=True,stdout=subprocess.PIPE)
program=b'\n'.join(p.read_bytes() for p in parts)+b'\n'
result=subprocess.run([str(compiler)],input=program,capture_output=True,timeout=60)
if result.returncode:raise SystemExit('RETARGET_COMPILE_FAILED: '+result.stderr.decode(errors='replace'))
binary=result.stdout
assert len(binary)>64 and binary[:4]==b'\x7fELF' and struct.unpack_from('<HH',binary,16)==(2,183),'RETARGET_NOT_AARCH64_EXEC'
report={'schema':'omega.upper-retarget.build.v1','compiler_sha256':expected,'compiler_origin':'omega-mv seed/out/omc via seed/omega-from-seed.sh (translation-v1 section 1)','inputs':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in parts},'combined_source_sha256':hashlib.sha256(program).hexdigest(),'binary_sha256':hashlib.sha256(binary).hexdigest(),'host_orchestration':'Python process/filesystem only; no host parser or compute fallback in the native artifact'}
with tempfile.TemporaryDirectory(prefix='.retarget-build-',dir=build) as temp:
    t=Path(temp)
    (t/'program.om').write_bytes(program);(t/'retarget').write_bytes(binary);(t/'retarget').chmod(0o700)
    (t/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    for name in ['program.om','retarget','build.json']:os.replace(t/name,build/name)
print(build/'retarget')
