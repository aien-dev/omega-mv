#!/usr/bin/env python3
"""Host build orchestration only; all C++ importer/exporter computation is seeded Omega (#739)."""
from pathlib import Path
import hashlib,json,os,struct,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
build=ROOT/'build/L739'
for p in [ROOT/'build',build]:
    if p.is_symlink():raise SystemExit('CPP_ADAPTER_UNSAFE_BUILD_PATH')
build.mkdir(parents=True,exist_ok=True)
# Same source-only #621 construction path and pinned bridge-seed compiler as the C, Rust and Python adapters.
subprocess.run(['bash','tools/source-aware/build'],cwd=ROOT,check=True,timeout=600,stdout=subprocess.PIPE)
compiler=ROOT/'build/L574/toolchain/build/bootstrap/omc'
verifier=ROOT/'build/L574/toolchain/tools/verify-elf.sh'
expected='191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9'
assert compiler.is_file() and hashlib.sha256(compiler.read_bytes()).hexdigest()==expected,'CPP_ADAPTER_COMPILER_PIN_MISMATCH'
subprocess.run(['sha256sum','-c','tools/source-aware/imports.sha256'],cwd=ROOT,check=True,stdout=subprocess.PIPE)
VENDOR='src/source-aware/vendor/sha256.om'
CODEC='src/source-aware/core-v2.om'
if not (ROOT/CODEC).is_file():raise SystemExit('CPP_ADAPTER_CODEC_V2_MISSING: '+CODEC+' (#621 v2 codec lane) is required by cpp-export')
ARTIFACTS={'cpp-import':[VENDOR,'src/adapters/cpp-u64-import.om'],
           'cpp-export':[VENDOR,CODEC,'src/adapters/cpp-u64-export.om']}
report={'schema':'omega.cpp-u64.build.v1','profile':'spec/universal/cpp-u64-v1.md','compiler_sha256':expected,
        'seed_source_archive':'0fc7d8662cb799e7c206b17c18ea4c3ec2b8fea9','birth_seed':'29b12760',
        'compiler_choice':'bridge-seed omc 191b776e: the pinned compiler of the reviewed C importer, the #621 codecs and the C/Rust/Python adapters; vendor SHA256 and core-v2.om decode compile unchanged under it; neither adapter needs argv',
        'artifacts':{},'host_orchestration':'Python process/filesystem only; no C++ parser, generator or compute fallback in native artifacts'}
with tempfile.TemporaryDirectory(prefix='.cpp-build-',dir=build) as temp:
    t=Path(temp)
    for name,parts in ARTIFACTS.items():
        program=b'\n'.join((ROOT/p).read_bytes() for p in parts)+b'\n'
        result=subprocess.run([str(compiler)],input=program,capture_output=True,timeout=120)
        if result.returncode:raise SystemExit(f'CPP_ADAPTER_COMPILE_FAILED {name} rc={result.returncode}: '+result.stderr.decode(errors='replace'))
        binary=result.stdout
        assert len(binary)>64 and binary[:4]==b'\x7fELF' and struct.unpack_from('<HH',binary,16)==(2,183),'CPP_ADAPTER_NOT_AARCH64_EXEC'
        (t/(name+'.om')).write_bytes(program);(t/name).write_bytes(binary);(t/name).chmod(0o700)
        subprocess.run(['sh',str(verifier),str(t/name)],check=True)
        report['artifacts'][name]={'inputs':{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in parts},'combined_source_sha256':hashlib.sha256(program).hexdigest(),'binary_sha256':hashlib.sha256(binary).hexdigest()}
    (t/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    for name in [*ARTIFACTS,*(n+'.om' for n in ARTIFACTS),'build.json']:os.replace(t/name,build/name)
for name in ARTIFACTS:print(build/name)
