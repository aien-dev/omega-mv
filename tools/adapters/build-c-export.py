#!/usr/bin/env python3
"""Host build orchestration only; all exporter computation is seeded Omega."""
from pathlib import Path
import hashlib,json,os,struct,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
build=ROOT/'build/L622'
for p in [ROOT/'build',build]:
    if p.is_symlink():raise SystemExit('C_EXPORT_UNSAFE_BUILD_PATH')
build.mkdir(parents=True,exist_ok=True)
# Same source-only #621 construction path and pinned compiler as the C importer.
subprocess.run(['bash','tools/source-aware/build'],cwd=ROOT,check=True,timeout=120,stdout=subprocess.PIPE)
compiler=ROOT/'build/L574/toolchain/build/bootstrap/omc'
verifier=ROOT/'build/L574/toolchain/tools/verify-elf.sh'
expected='191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9'
assert compiler.is_file() and hashlib.sha256(compiler.read_bytes()).hexdigest()==expected,'C_EXPORT_COMPILER_PIN_MISMATCH'
# Unchanged reviewed #621 SHA256 vendor and decoder core, then the exporter.
parts=[ROOT/'src/source-aware/vendor/sha256.om',ROOT/'src/source-aware/core.om',ROOT/'src/adapters/c-u64-export.om']
program=b'\n'.join(p.read_bytes() for p in parts)+b'\n'
subprocess.run(['sha256sum','-c','tools/source-aware/imports.sha256'],cwd=ROOT,check=True,stdout=subprocess.PIPE)
result=subprocess.run([str(compiler)],input=program,capture_output=True,timeout=60)
if result.returncode:raise SystemExit('C_EXPORT_COMPILE_FAILED: '+result.stderr.decode(errors='replace'))
binary=result.stdout
assert len(binary)>64 and binary[:4]==b'\x7fELF' and struct.unpack_from('<HH',binary,16)==(2,183),'C_EXPORT_NOT_AARCH64_EXEC'
report={'schema':'omega.c-u64-export.build.v1','contract':'spec/universal/translation-v1.md','compiler_sha256':expected,'seed_source_archive':'0fc7d8662cb799e7c206b17c18ea4c3ec2b8fea9','birth_seed':'29b12760','inputs':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in parts},'combined_source_sha256':hashlib.sha256(program).hexdigest(),'binary_sha256':hashlib.sha256(binary).hexdigest(),'host_orchestration':'Python process/filesystem only; no C/Rust/Python generator or compute fallback in native artifact'}
with tempfile.TemporaryDirectory(prefix='.c-export-build-',dir=build) as temp:
    t=Path(temp)
    (t/'program.om').write_bytes(program);(t/'c-export').write_bytes(binary);(t/'c-export').chmod(0o700)
    subprocess.run(['sh',str(verifier),str(t/'c-export')],check=True)
    (t/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    for name in ['program.om','c-export','build.json']:os.replace(t/name,build/name)
print(build/'c-export')
