#!/usr/bin/env python3
"""Host build orchestration only; all Rust importer/exporter computation is seeded Omega (#626)."""
from pathlib import Path
import hashlib,json,os,struct,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
build=ROOT/'build/L626'
for p in [ROOT/'build',build]:
    if p.is_symlink():raise SystemExit('RUST_ADAPTER_UNSAFE_BUILD_PATH')
build.mkdir(parents=True,exist_ok=True)
# Same source-only #621 construction path and pinned compiler as the C importer and C exporter.
subprocess.run(['bash','tools/source-aware/build'],cwd=ROOT,check=True,timeout=600,stdout=subprocess.PIPE)
compiler=ROOT/'build/L574/toolchain/build/bootstrap/omc'
verifier=ROOT/'build/L574/toolchain/tools/verify-elf.sh'
expected='191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9'
assert compiler.is_file() and hashlib.sha256(compiler.read_bytes()).hexdigest()==expected,'RUST_ADAPTER_COMPILER_PIN_MISMATCH'
subprocess.run(['sha256sum','-c','tools/source-aware/imports.sha256'],cwd=ROOT,check=True,stdout=subprocess.PIPE)
VENDOR='src/source-aware/vendor/sha256.om'
ARTIFACTS={'rust-import':[VENDOR,'src/adapters/rust-u64-import.om'],
           'rust-export':[VENDOR,'src/source-aware/core.om','src/adapters/rust-u64-export.om']}
report={'schema':'omega.rust-u64.build.v1','compiler_sha256':expected,'seed_source_archive':'0fc7d8662cb799e7c206b17c18ea4c3ec2b8fea9','birth_seed':'29b12760',
        'compiler_choice':'bridge-seed omc 191b776e: the pinned compiler of the reviewed C importer and #621 codec; vendor SHA256 and core.om decode compile unchanged under it; neither adapter needs argv',
        'artifacts':{},'host_orchestration':'Python process/filesystem only; no Rust/C/Python parser or compute fallback in native artifacts'}
with tempfile.TemporaryDirectory(prefix='.rust-build-',dir=build) as temp:
    t=Path(temp)
    for name,parts in ARTIFACTS.items():
        program=b'\n'.join((ROOT/p).read_bytes() for p in parts)+b'\n'
        result=subprocess.run([str(compiler)],input=program,capture_output=True,timeout=120)
        if result.returncode:raise SystemExit(f'RUST_ADAPTER_COMPILE_FAILED {name} rc={result.returncode}: '+result.stderr.decode(errors='replace'))
        binary=result.stdout
        assert len(binary)>64 and binary[:4]==b'\x7fELF' and struct.unpack_from('<HH',binary,16)==(2,183),'RUST_ADAPTER_NOT_AARCH64_EXEC'
        (t/(name+'.om')).write_bytes(program);(t/name).write_bytes(binary);(t/name).chmod(0o700)
        subprocess.run(['sh',str(verifier),str(t/name)],check=True)
        report['artifacts'][name]={'inputs':{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in parts},'combined_source_sha256':hashlib.sha256(program).hexdigest(),'binary_sha256':hashlib.sha256(binary).hexdigest()}
    (t/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    for name in [*ARTIFACTS,*(n+'.om' for n in ARTIFACTS),'build.json']:os.replace(t/name,build/name)
for name in ARTIFACTS:print(build/name)
