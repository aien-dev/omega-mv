#!/usr/bin/env python3
"""Host build orchestration only; all S2 codec, import, export and lowering computation is seeded Omega
(spec/universal/translation-s2-v1.md). Writes build/S2; no S1 build directory is touched."""
from pathlib import Path
import hashlib,json,os,struct,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
build=ROOT/'build/S2'
for p in [ROOT/'build',build]:
    if p.is_symlink():raise SystemExit('S2_UNSAFE_BUILD_PATH')
build.mkdir(parents=True,exist_ok=True)
# Same source-only #621 construction path and pinned compiler as the S1 codec, C importer and Rust exporter.
subprocess.run(['bash','tools/source-aware/build'],cwd=ROOT,check=True,timeout=600,stdout=subprocess.PIPE)
seedc=ROOT/'build/L574/toolchain/build/bootstrap/omc'
verifier=ROOT/'build/L574/toolchain/tools/verify-elf.sh'
SEED='191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9'
NATIVE='734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61'
assert seedc.is_file() and hashlib.sha256(seedc.read_bytes()).hexdigest()==SEED,'S2_COMPILER_PIN_MISMATCH'
# The lowering takes an optional NAME argument, so like build-lower.py it needs the argv-capable native omc.
if not os.environ.get('OMEGA_NATIVE_OMC'):raise SystemExit('S2_COMPILER_UNSET: set OMEGA_NATIVE_OMC to the pinned omc')
nativec=Path(os.environ['OMEGA_NATIVE_OMC'])
assert nativec.is_file() and hashlib.sha256(nativec.read_bytes()).hexdigest()==NATIVE,'S2_COMPILER_PIN_MISMATCH'
subprocess.run(['sha256sum','-c','tools/source-aware/imports.sha256'],cwd=ROOT,check=True,stdout=subprocess.PIPE)
VENDOR='src/source-aware/vendor/sha256.om'
CORE='src/source-aware/core-v3.om'
ARTIFACTS={'sa':(SEED,[VENDOR,CORE,'src/source-aware/main.om']),
           'c-i64-import':(SEED,[VENDOR,'src/adapters/c-i64-import.om']),
           'rust-i64-export':(SEED,[VENDOR,CORE,'src/adapters/rust-i64-export.om']),
           'lower-s2':(NATIVE,[VENDOR,CORE,'src/adapters/upper-lower-s2.om'])}
report={'schema':'omega.translation-s2.build.v1','compilers':{SEED:'bridge-seed omc 191b776e (build/L574, seed 0fc7d866)',NATIVE:'omega-mv seed/out/omc via seed/omega-from-seed.sh (translation-v1 section 1); argv for lower NAME'},
        'artifacts':{},'host_orchestration':'Python process/filesystem only; no C/Rust parser or compute fallback in native artifacts'}
with tempfile.TemporaryDirectory(prefix='.s2-build-',dir=build) as temp:
    t=Path(temp)
    for name,(pin,parts) in ARTIFACTS.items():
        compiler=seedc if pin==SEED else nativec
        program=b'\n'.join((ROOT/p).read_bytes() for p in parts)+b'\n'
        result=subprocess.run([str(compiler)],input=program,capture_output=True,timeout=120)
        if result.returncode:raise SystemExit(f'S2_COMPILE_FAILED {name} rc={result.returncode}: '+result.stderr.decode(errors='replace'))
        binary=result.stdout
        assert len(binary)>64 and binary[:4]==b'\x7fELF' and struct.unpack_from('<HH',binary,16)==(2,183),'S2_NOT_AARCH64_EXEC'
        (t/(name+'.om')).write_bytes(program);(t/name).write_bytes(binary);(t/name).chmod(0o700)
        subprocess.run(['sh',str(verifier),str(t/name)],check=True)
        report['artifacts'][name]={'compiler_sha256':pin,'inputs':{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in parts},'combined_source_sha256':hashlib.sha256(program).hexdigest(),'binary_sha256':hashlib.sha256(binary).hexdigest()}
    (t/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    for name in [*ARTIFACTS,*(n+'.om' for n in ARTIFACTS),'build.json']:os.replace(t/name,build/name)
for name in ARTIFACTS:print(build/name)
