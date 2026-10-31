#!/usr/bin/env python3
"""Read-only FR-9 qualification oracle, not a compiler/backend/build dependency.

Run on Linux AArch64, from any directory:
  python3 seed/omc/qualify-x86.py --compiler /absolute/path/to/omc
Requires Python 3.10+, qemu-x86_64 and GNU x86-64 objdump or llvm-objdump.
Use --inventory for corpus categorization without compiling/running anything.

All outputs go to stdout and a new TemporaryDirectory, removed on exit. Existing
receipts, compiler binaries, sources and gate manifests are never rewritten.
A fresh cwd and resource limits are NOT a security sandbox. Only trusted fixture
sources belong here. Reviewed, content-pinned host tests normalize their own
clock/random/process results and run in separate architecture-local directories.
Unreviewed host effects remain NOT_RUN and make the verdict nonzero.

The fixture inventory includes every .om/.omegese under tests/, every seed/*/tests,
seed/omc/{om2,contracts}, seed/qualification, unit .om files and wasm/SPIRV case tables.
Frozen .cases and C2 rows retain their pinned historical assertions and exact
inputs. Current C2 compatibility assertions remain mandatory and separate from
their frozen provenance replay. Mutant specifications link to actual compiler
mutation executions with build/control verdicts; healthy compiler output is not
treated as a mutant. Current cross-target comparisons remain additional coverage.
"""
from __future__ import annotations

import argparse
from collections import Counter, OrderedDict
from dataclasses import dataclass, field, replace
import hashlib
from functools import lru_cache
import json
import os
from pathlib import Path
import platform
import re
import resource
import shutil
import signal
import stat
import struct
import subprocess
import tempfile

from x86_corpus_drivers import EFFECT_CPU_SECONDS, bare_calls, bridge_plan, bridge_positive_plan, directive_plan, plan_case, prepare_case, select_a64_entry, wrap_bare

ROOT = Path(__file__).resolve().parents[2]
MAIN = re.compile(rb"(?m)^[ \t]*(?:export[ \t]+)?fn[ \t]+main(?:[ \t]|$)")
MODE = re.compile(rb"^(?:ir(?:[ \t\r\n]|$)|check(?:[ \t\r\n]|$)|next(?:[ \t\r\n]|$)|om2 fmt(?:[ \t\r\n]|$)|target(?:[ \t\r\n]|$))")
TEXT = re.compile(rb'(?m)^[ \t]*text[ \t]+\w+[ \t]+"((?:[^"\\]|\\.)*)"[^\n]*$')
UNSAFE = set("""
devctl devcall uvmctl mapnew mapdrop mapget mapset mapread mapwrite mapaddr maphold mapbind
efi socket socknew bind sockbind connect listen accept send recv sockname peername sockopt shutdown
fork exec execprog waitpid kill killpid clock realns monons sleep sleepns random cwd
chdir fchdir envcnt envlen envread open openat fopen fclose fseek fsync fdsync fchmod
chmod chown umask unlink rename renames link symlnk mkdir makedir rmdir stat lstat
fstat getdnts faccess readlnk memfd ftrunc truncate pread pwrite ioctl flock syncfs
setpgid setsid
""".split())
INSTRUCTION = re.compile(r"^\s*([0-9a-fA-F]+):\s+((?:[0-9a-fA-F]{2}[ \t]+)+)(.*?)\s*$")
DIAG = re.compile(rb"line ([0-9]+): error ([0-9]+)")
TARGET_PREFIX = b"target x86linux\n"
QEMU_IOCTL_URL = "https://github.com/qemu/qemu/blob/v10.0.14/linux-user/syscall.c#L5713-L5717"
QEMU_SYSCALL_SHA256 = "0d6212a547d456f47612a4c297e093dd60fe59ad021f44e3f21e5c6bb9be0cf7"
QEMU_ORACLE_SHA256 = "fd7ae7768c1d698d387893688d28abde90512f1f445d171a1389110eb7c237ee"
QEMU_SOURCE_ARCHIVE_SHA256 = "c7230968906fdb5540d803e2af1f515e77590a414f2a4dbd3ec9ada3c47083d0"
QEMU_IOCTL_EXCERPT = b"""        if (ie->target_cmd == 0) {
            qemu_log_mask(
                LOG_UNIMP, "Unsupported ioctl: cmd=0x%04lx\\n", (long)cmd);
            return -TARGET_ENOTTY;
        }
"""

# Inspected host fixtures at the source revision below. These are not authority
# to run a changed fixture: hashes pin exact reviewed effects. Drivers use local
# scratch paths, anonymous mappings, private children, invalid-fd ioctls and
# /dev/zero reads, never GPU nodes. Pinned sockets use relative Unix paths or
# OS-assigned loopback ports, not fixed ports/outbound endpoints/live services.
# Original gate.sh supplies the arguments/environment/expected statuses below.
HOST_PINS = """
args 0e6bdbfb63ef9e52028682ddbecb1e9b4d81a3326b2e63848be570a775c31e6e
chdir 33f64d79ae5655a020d795f8f31b7e4cff9f8c176534e4474216a2709b04f759
chmod 21ee5f82d67f2b286cbcd3c5e1242199f0b7b66f020caa98f5a53212d4245532
chown 9e5d8e7f2fd5678c63ed95bbde66f9d6f753b42791701d45311bfe20dd7f4d17
devcall ca76010ea6cf2ab7acd8b8fab36234dcaad8f2cede057e01a52e840842a2e8ed
directories c25fcaeb5e6fc0331b31d7f4af5aa5d00c87c5df6d99c8d2fa42456268e0124c
environment d798e64c66d39fa9cf6df29ed67026e747a8cff2e4c4f95f84ccc2ea8d01bff6
environment_trap 93043b312007ee2dedce62d8a7a5d4601c34c80a4f142ce471270b585ce7dc66
errors a6fcf6e1ede30da15f4ad75ea11173cd4d5001613ae424cd0b30eff62f96898a
faccess e4aec179deea0ab97c88ab0a274bba8fab2f1f0ca6d54b9829d8b724967e4087
fchdir d647e8917878fc9e9b42b2d65102d302710e0592055bb84fa9fc67ce4b1d1c26
fdsync 4a7bc7d82979b05511a5ea2cd8a7c51ebce6e36a52a3fc929b0e9db1f99e3733
files 7599eaaa3936a7342b4825965f6fd0a0a2517075f12168b5bc54d4d06ddc5a4b
flock cb1c63d6738e4923018d3014be4358ef1d80b60f7c406db677b36b8684066fd1
genlimit ea4f544b49878f5532f75b0ac7e8fa42e6926d5b57ea002e748679922859ad48
links 9fa1710075bf9ce64bf57d3f0c6ffe8deac39a294bb8242c64ba185e8cb7cd3a
links_short 5cf587ad20927ccf7c2873f58d332623f1a2e60adac8efa2e3877d74647cdc98
location 691bc9c2903c9c269ded9a55596ce1cb08adc693fe3e08ffcbf274b3e523a944
lstat 544cf17cf26ab54eb71e42030027a6a171faccc084294e235c76c8df419a8a6f
mapio f657ff3daa44b070608178834b46f1f83ade03e65b73c00e05a1c2e2a9356267
mapping b42c477a43862018dc0cb1f1c4f06374d1413130eeb112b6fd6b81132360c607
pgroup 97cee434be0cc7dc8c27b2786747e2ecdd9dc31a33c558e5c54c20f787a19ece
pio d36aec6170344d4c3126212a092bc021e671c24ab3bc31871562847429767c80
random cf1eff4a9e12ff42141d76dd0a2eb136ca345e9cbf9f4e3ee73d70abdd7e16fb
reserve 8498b995e3ee7a2d9ac86085af1674af7c14b57da5778051fc5e7a15406768dc
stat 6ff1b76d4fd35599e67684e706a5d66eec84584f51326c74a1fc35bec023ab60
stat_short 628c9696acb656943ad68a4137a81fbfafdc1a1aafb59e0fe4a09332957b2600
structfile 8a861930643b73590772a575f62985389eb6c1daf77820d37e88a8c648171505
trunc 6a48633d31aa718ca9bd543a3c0ec134318d74005b31e6424bd4b0f920963764
truncate 53dab870239429150e88616fe5173a0ad8637719cc15eac7bbf955f0e5662b3d
uvmctl 1e7b46b9d0fb44f5c97768f92daa7599e9b36e6d5cbc6553ac755a7c02047b5f
network a8b16320610b79bd0a3e96e303d631c1a9a00b6464d963f3d79c5b4e1ff2d514
sockopt 0ee354c31e3e33c63da7cd8ade18cdf3f98d6d742adeedbffa506d746b5ff0c1
netdata 6762de311b8b1cb4f1f9220bc3837b29733fcf382ac3c2cd19e8096c02789f88
netpeer ed93e15648c6fe304cc28590065fab6d36887db448c253813b8a762ca9c61193
peername 3052bbfc59647745e2576bb5ffb528ec10f23dc467b2c1b1d55d0a8980f8a728
process 0c1292a905cb1263a733e26f2e026a2915089d6d6959d16b617c05f446452964
memcheck 652a4bc9455251bb51d01a75fc5be0ad008ae5e022e4835e43959f073c82413d
device 375898c6ac071af5ab50bb41fce6aa93ee5796302916cac65df70831c52d3d67
psync 8355680fe35976dc5fd3621c3dc9b2ff8f6cc8b210ec90915d2044a4de133473
"""
HOST_DRIVERS = {sha: name for name, sha in (row.split() for row in HOST_PINS.splitlines() if row)}

# Explicit reviewed verdicts, not a name-prefix heuristic. The first comments
# of these manifests fix ctl PASS / mutant FAIL in advance; the lexical list's
# RESULTS.txt:38-43 describes exactly the listed refusals/format/diag failures.
CONDUCTOR_FAILURES = {
    "seed/omc/contracts/mutant.cases": "mutant-requires mutant-proof",
    "seed/omc/om2/s1/s1-mutants.cases": "m-rotl m-rchk m-hexd m-hexU m-optin m-paren m-unk m-tyunk m-kwlong",
    "seed/omc/om2/s2/s2-mutants.cases": "m-div0 m-sovf m-ucount m-brk m-idx m-depth m-cyc",
    "seed/omc/om2/s3/s3-mutants.cases": "m-l-ovl m-l-rev m-l-lix m-l-past m-fuel m-e16-d3 m-e16-inl m-fzu m-fzl m-fzc m-fzr m-fzn m-aer m-aex m-act m-aop m-araw",
    "seed/omc/om2/s4/s4-mutants.cases": "m-uce m-slot m-pwrap m-pfirst m-p19 m-tramp m-preadwr m-textlen",
    "seed/qualification/om2/candidates/s1/s1-lexmutants.cases":
        "hex-upper-s1-d-hexupper hex-upper-s1-d-hexupper-plain hex-upper-s2-r6-upper hex-upper-s2-r6-mixed hex-upper-s2-r6-upper-ulong "
        "hex-17-s2-r6-17small hex-17-s2-r6-17big diag-col0-s1-d-left diag-col0-s1-d-eof-left diag-col0-s1-d-hexrange diag-col0-s1-d-unknown-fn "
        "fmt-note-s2-fmt-x1 fmt-note-s2-fmt-x3 fmt-text-s2-fmt-x1 fmt-text-s2-fmt-x5",
    "seed/qualification/om2/candidates/s1/s1-mutants.cases": "",
    "seed/qualification/om2/candidates/s2/s2-mutants.cases": "",
    "seed/qualification/om2/candidates/s3/s3-mutants.cases": "",
    "seed/qualification/om2/candidates/s4/s4-mutants.cases": "",
    "seed/qualification/om2/candidates/s4/s4-part-mutants.cases": "",
    "seed/qualification/om2/baseline/guard.cases": "",
    "seed/qualification/om2/baseline/transform.cases": "",
    # Headers fix ctl PASS / kill FAIL in advance (s5: "ctl-* PASS, m-* FAIL";
    # bnd: "pass=2 fail=8"; hex: "pass=3 fail=5"); seed/host/check.sh runs each.
    "seed/omc/om2/s5/s5-mutants.cases": "m-msr m-stp m-live m-tst m-zpool m-zvec m-movz16 m-twinall m-twinnone",
    "seed/omc/om2/bnd/mutants.cases":
        "bnd-extra-om1-left-typed bnd-extra-om2-left-typed bnd-extra-om1-left-assign bnd-extra-om2-left-assign "
        "bnd-missing-om1-short-next bnd-missing-om2-short-next bnd-missing-om1-short-bare bnd-missing-om2-short-bare",
    "seed/omc/om2/hex/mutants.cases":
        "hex-tbig-pairs-run hex-tbig-places-run hex-nolimit-r6-17trunc hex-digit-pairs-run hex-digit-floatlike-run",
    # Header: "These cases must produce OM2GATE FAIL, not a qualification PASS." Each row
    # substitutes wrong bytes into a pinned emit source, so its oracle digest must differ.
    "seed/qualification/security673/relational/binding-negative.cases": "wrong-source-bytes wrong-checker-bytes",
    # Header: "The expectation stays the correct one (trap 3, exit 103), so every row must FAIL: the
    # mutant executes /bin/true (exit 0)." seed/host/check.sh requires exactly these FAIL verdicts.
    "seed/qualification/security673/packets-mutant.cases": "mutant-size mutant-count mutant-exact",
}
NEGATIVE_CONDUCTOR_MANIFESTS = {
    "seed/qualification/security673/relational/binding-negative.cases": "stdout sha256 differs",
}
HISTORICAL_COMPILERS = {
    "build/om2-qual/cand-s1/omc.om": ("a5927895c02a5abe6b5c3a455f5d54303962ebe8", "3d7bcf9afdaa36fc42b862e0af39c524a14e85204bd5f40ee74107db40ce6a1d"),
    "build/om2-qual/cand-s2/omc.om": ("7fb62f3a87104f48bb1317ab05333c5805ce5340", "1f41c7dc4f27324c2595c60f601e74548cd1bb4aeca8d4d367056ee803e7de72"),
    "build/om2-qual/cand-s3/omc.om": ("ad220c054b2e018a985384a33bf09b9f0d612787", "656d4e25f503d38ae7523c758b37734b191ecc064d370ac7928011f25c02a3d6"),
    "build/om2-qual/cand-s4/omc.om": ("f6a6492fd1a3b608f0413a75685ddbb712075a73", "c792b401193d8477d38965d6ddb0fd0cb8fd5ac24fcb92fae9af52037a765f1c"),
}
ORIGINAL_SOURCE = {
    "path": "seed/omc/omc.om",
    "git_revision": "acb3f15cdc691b1b046b48b0dad0f9255a12ba4b",
    "git_path": "seed/omc/omc.om",
    "sha256": "2e35db7d6c2066fb4a20d19b8a9579c44caca9c9937b0594a89422010b673637",
}
PINNED_COMPILER_SHA256 = "734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61"
MUTANT_SPECIFICATIONS = {
    "seed/qualification/om2/sec/s3s4/mutant-kills.cases",
    "seed/qualification/om2/sec/s5s7/mutant-side.cases",
}


def frozen_manifest(rel):
    return rel.startswith("seed/qualification/om2/sec/") and rel.endswith("/pinned.cases")


def conductor_identity(rel):
    if rel.startswith("seed/qualification/om2/candidates/"):
        stage = rel.split("/")[4]
        return "build/om2-qual/cand-" + stage + "/omc.om"
    if frozen_manifest(rel) or rel.startswith("seed/qualification/om2/baseline/") or rel.endswith("/manifest-c2.txt"):
        return "pinned"
    return "current"
SELFTEST_MANIFESTS = {
    "seed/qualification/om2/selftest/" + name + ".cases"
    for name in ("controls", "emit", "fail", "pass")
}
DELIBERATELY_ABSENT = {
    "seed/qualification/om2/selftest/does-not-exist",
    "seed/qualification/om2/selftest/does-not-exist.om",
}


@dataclass
class Case:
    name: str
    source: bytes
    stdin: bytes = b""
    args: tuple[str, ...] = ()
    expected_exit: int | None = None
    expected_compile: str | None = None
    operation: str = "differential"
    stdout_oracle: str | None = None
    stderr_oracle: str | None = None
    same_source: bytes | None = None
    driver: str | None = None
    blocker: str | None = None
    evidence: dict = field(default_factory=dict)
    deadline: int | None = None
    bridge: dict | None = None


class Failure(Exception):
    pass


def digest(data):
    return hashlib.sha256(data).hexdigest()


def fixture_files():
    roots = [ROOT / "tests", ROOT / "seed/qualification", ROOT / "seed/omc/om2",
             ROOT / "seed/omc/contracts", ROOT / "tools/exp445/arms/examples"]
    roots += sorted((ROOT / "seed").glob("*/tests"))
    files = {p for root in roots if root.is_dir() for p in root.rglob("*")
             if p.is_file() and (p.suffix in (".om", ".omegese", ".cases") or
                                 (p.name.startswith("manifest-c2") and p.suffix == ".txt"))}
    files.update((ROOT / "seed").glob("*/*.unit.om"))
    files.add(ROOT / "seed/wasm/cases.tsv")
    files.update((ROOT / "seed/spirv/demo.tsv", ROOT / "seed/spirv/regression.tsv"))
    return sorted(files)


def contained(path):
    p = (ROOT / path).resolve()
    if not p.is_relative_to(ROOT):
        raise Failure("fixture path escapes repository: " + path)
    return p


def source_bytes(spec):
    parts = []
    for part in spec.split("+"):
        if part.startswith("pack:"):
            path, off, length = part[5:].rsplit(":", 2)
            off, length = int(off), int(length)
            data = contained(path).read_bytes()
            if off < 0 or length < 0 or off + length > len(data):
                raise Failure("invalid pack range: " + part)
            parts.append(data[off:off + length])
        elif part.startswith("cyc:"):
            path, length = part[4:].rsplit(":", 1)
            length = int(length)
            data = contained(path).read_bytes()
            if not data or length < 0:
                raise Failure("invalid cyclic input: " + part)
            parts.append((data * (length // len(data) + 1))[:length])
        elif part == "om/sha256/sha256.om" and not contained(part).exists():
            # Removed by the OM-2 cutover. The old caller uses omsha, not the
            # current API: recover its EXACT frozen source, never alias APIs.
            frozen = source_bytes("pack:seed/qualification/om2/c2/corpus.pack:12540:35333")
            expected = "7f034814303a8a40b4bc56383ddd55ab2ae8460291f80793340bc3b0ac024e21"
            if digest(frozen) != expected:
                raise Failure("historical SHA source differs from independent c2/index.tsv pin")
            parts.append(frozen)
        else:
            parts.append(contained(part).read_bytes())
    return b"".join(parts)


def stdin_bytes(spec):
    if spec == "-":
        return b""
    if spec.startswith("file:"):
        return contained(spec[5:]).read_bytes()
    if spec.startswith("rep:"):
        _, byte, length = spec.split(":")
        return bytes([int(byte)]) * int(length)
    if spec.startswith("seq:"):
        return bytes(i % 251 for i in range(int(spec[4:])))
    if spec.startswith("rnd:"):
        _, seed, length = spec.split(":")
        state, length, out = int(seed), int(length), bytearray()
        mask = (1 << 64) - 1
        while len(out) < length:
            state = (state + 0x9e3779b97f4a7c15) & mask
            z = ((state ^ (state >> 30)) * 0xbf58476d1ce4e5b9) & mask
            z = ((z ^ (z >> 27)) * 0x94d049bb133111eb) & mask
            out.extend((z ^ (z >> 31)).to_bytes(8, "little"))
        return bytes(out[:length])
    raise Failure("unknown stdin grammar: " + spec)


def host_words(source):
    source = re.sub(rb'"(?:[^"\\]|\\.)*"', b'""', source)
    source = re.sub(rb"(?m)^[ \t]*note[^\n]*", b"", source)
    return sorted(set(re.findall(r"\b\w+\b", source.decode("utf-8", "replace"))) & UNSAFE)


def target_source(case, driver):
    if driver not in ("directories", "fchdir"):
        return case.source, None
    # These exact pinned fixtures use the Linux AArch64 O_DIRECTORY literal.
    # This is target-adapted ABI coverage, explicitly NOT identical-source parity.
    old = b"len dir 16384 0" if driver == "directories" else b"len subd 16384 0"
    if case.source.count(old) != 1:
        raise Failure("reviewed O_DIRECTORY adaptation anchor changed")
    return case.source.replace(old, old.replace(b"16384", b"65536")), "O_DIRECTORY: AArch64 16384 -> x86-64 65536"


def unit_cases(path):
    # Same full-bit dumper as seed/wasm/gate-irwasm.sh, not a status-byte proxy.
    dumper = b"""global dumpbuf uchar 8
fn ir_dump x ulong to void
for j ulong 8
put dumpbuf j uchar and shr x mul j 8 255
end
p = ulong 0
while lt p 8 bound 8
k = write 1 dumpbuf p sub ulong 8 p
if le k 0
exit 90
end
p = add p ulong k
end
end
fn main to int
"""
    for number, row in enumerate(path.read_text().splitlines(), 1):
        if not row.strip() or row.startswith("#"):
            continue
        unit, function, result, typed, status = row.split()
        if not re.fullmatch(r"\w+", unit) or not re.fullmatch(r"\w+", function):
            raise Failure("invalid unit/function name in cases.tsv")
        source_path = ROOT / ("seed/wasm/" + unit + ".unit.om")
        if not source_path.is_file():
            source_path = ROOT / ("seed/spirv/" + unit + ".unit.om")
        if not source_path.is_file():
            source_path = contained("tests/one_layer/omegese/dev/" + unit + ".omegese")
        actual = []
        for item in (() if typed == "-" else typed.split(",")):
            kind, value = item.split(":")
            if kind == "bool" and value in ("true", "false"):
                actual.append(value)
            elif kind in ("byte", "uchar", "short", "ushort", "int", "uint", "long", "ulong") and re.fullmatch(r"-?\d+", value):
                actual.append(kind + " " + value)
            else:
                raise Failure("unknown typed unit argument: " + item)
        call = function + " " + " ".join(actual)
        if result == "bool":
            body = "if " + call + "\nir_dump ulong 1\nelse\nir_dump ulong 0\nend\n"
        elif result == "void":
            body = call + "\nir_dump ulong 0\n"
        elif result in ("byte", "short", "int", "long"):
            body = "ir_dump bitcast ulong long " + call + "\n"
        elif result in ("uchar", "ushort", "uint", "ulong"):
            body = "ir_dump ulong " + call + "\n"
        else:
            raise Failure("unknown unit result type: " + result)
        source = source_path.read_bytes() + b"\n" + dumper + body.encode() + b"ret 0\nend\n"
        yield Case(f"{path.relative_to(ROOT).as_posix()}:{number}:{unit}.{function}", source,
                   expected_exit=int(status), expected_compile="accept")


def positional_view_cases():
    # Mutability controls for the pread/pwrite writable-destination distinction.
    # Invalid fd -1 makes accepted cases deterministic without filesystem effects.
    for word, qualifier, accepted in (("pread", "view", False),
                                      ("pread", "edit", True),
                                      ("pwrite", "view", True),
                                      ("pwrite", "edit", False)):
        source = (
            "text ro \"abcd\"\n"
            f"fn probe b {qualifier} uchar 4 to long\n"
            f"ret {word} neg int 1 b 0 4 0\nend\nfn main to int\nrw = new uchar 4 0\n"
            f"if ne probe {'rw' if word == 'pread' else 'ro'} neg long 9\n"
            "ret 1\nend\nret 0\nend\n").encode()
        yield Case(f"oracle-positional-{word}-{qualifier}", source,
                   expected_exit=0 if accepted else None,
                   expected_compile="accept" if accepted else "20")


def expanded_cases(files, record):
    for case in cases(files, record):
        plan = {} if case.operation in ("conductor", "bridge") else plan_case(ROOT, case.name.split(":", 1)[0], case.source)
        if plan:
            for key in ("source", "stdin", "args", "expected_exit", "driver", "blocker"):
                if key in plan and (case.operation == "differential" or key in ("driver", "blocker")):
                    setattr(case, key, plan[key])
            case.evidence.update(plan.get("evidence", {}))
            if "effects" in plan:
                case.evidence["reviewed_effects"] = plan["effects"]
        case.evidence.update(stdin_bytes=len(case.stdin), stdin_sha256=digest(case.stdin),
                             args=list(case.args), original_case=case.name)
        yield case
        if plan and case.operation == "differential":
            for variant in plan.get("variants", ()):
                yield Case(case.name + ":" + variant["name"], case.source, variant["stdin"], tuple(variant["args"]),
                           driver=case.driver, expected_exit=variant["expected_exit"],
                           evidence={**case.evidence, "stdin_bytes": len(variant["stdin"]),
                                     "stdin_sha256": digest(variant["stdin"]), "args": list(variant["args"]),
                                     "variant_of": case.name})
        driver = case.driver or HOST_DRIVERS.get(digest(case.source))
        if driver == "network" and not case.args:
            yield Case(case.name + ":gate-host-tcp-loopback", case.source, args=("tcp",), expected_exit=0)
        elif driver in ("netdata", "peername") and not case.args:
            for argument in ("1", "2", "3"):
                yield Case(case.name + ":gate-host-bounds-" + argument, case.source,
                           args=(argument,), expected_exit=103)
        elif driver == "memcheck":
            # Read the conductor's literal-name and want assignments together.
            # Literal ordering is not the case index (letters i/l/o/r are absent).
            texts = {m[1]: literal_bytes(m[0])[0] for m in re.finditer(
                rb'(?m)^[ \t]*text[ \t]+(\w+)[ \t]+"(?:[^"\\]|\\.)*"[^\n]*$', case.source)}
            default = re.search(rb"(?m)^want = int (\d+)$", case.source)
            arms = list(re.finditer(
                rb"(?m)^if eq num (\d+)\nr = write fd (\w+) 0 len \2\n(?:want = (\d+)\n)?end$", case.source))
            if default is None or len(arms) != 36:
                record("INVALID", case.name + ":embedded-refusals",
                       "memcheck conductor grammar changed; cannot infer expectations",
                       source_sha256=digest(case.source))
                continue
            for arm in arms:
                number, literal, code = arm.groups()
                yield Case(case.name + ":embedded-refusal-" + number.decode(), texts[literal],
                           expected_compile=(code or default[1]).decode())
    yield from positional_view_cases()
    current_sha = contained("om/sha256/sha256-om2.om").read_bytes()
    yield Case("oracle-current-sha256-private-api", current_sha, driver="bare-private-abi",
               expected_compile="accept", evidence={
                   "source_spec": "om/sha256/sha256-om2.om", "source_sha256": digest(current_sha),
                   "scope": "current API, separate from frozen historical omsha composition and leak scanner"})


@lru_cache(maxsize=None)
def conductor_resources(path):
    """Exact transitive file allowlist for an original-conductor replay."""
    resources = {}

    def add(name):
        if name in resources:
            return
        contained(name)
        if path.relative_to(ROOT).as_posix() in SELFTEST_MANIFESTS and name in DELIBERATELY_ABSENT:
            if contained(name).exists():
                raise Failure("original absent-input control unexpectedly exists: " + name)
            return
        if name in HISTORICAL_COMPILERS:
            revision, sha = HISTORICAL_COMPILERS[name]
            resources[name] = {"path": name, "sha256": sha,
                               "git_revision": revision, "git_path": "seed/omc/omc.om"}
        elif (name == "seed/omc/omc.om" and path.relative_to(ROOT).as_posix().startswith("seed/qualification/om2/baseline/")
              and path.relative_to(ROOT).as_posix() != "seed/qualification/om2/baseline/guard.cases"):
            # guard.cases mutates the CURRENT compiler (its header); f56a372ae (#971) moved
            # guard-wrapped.from to the current vs_chk text, which acb3f15's omc.om lacks.
            resources[name] = dict(ORIGINAL_SOURCE)
        elif name == "om/sha256/sha256.om":
            resources[name] = {"path": name, "sha256": "7f034814303a8a40b4bc56383ddd55ab2ae8460291f80793340bc3b0ac024e21",
                               "pack_path": "seed/qualification/om2/c2/corpus.pack", "offset": 12540, "length": 35333}
        else:
            resources[name] = {"path": name, "sha256": digest(contained(name).read_bytes())}

    def source(spec, review=False):
        for segment in spec.split("+"):
            if segment.startswith(("pack:", "cyc:")):
                add(segment.split(":")[1])
            else:
                add(segment)
        if review:
            if spec in DELIBERATELY_ABSENT and path.relative_to(ROOT).as_posix() in SELFTEST_MANIFESTS:
                return
            data = source_bytes(spec)
            words = host_words(data)
            if words and digest(data) not in HOST_DRIVERS:
                plan = plan_case(ROOT, spec, data)
                if not plan or "blocker" in plan:
                    raise Failure(f"original conductor source {spec} has unreviewed effects: {words}"
                                  + (f"; {plan['blocker']}" if plan else ""))

    add(path.relative_to(ROOT).as_posix())
    for constant in ("sha256-k", "sha256-iv"):
        add("seed/qualification/om2/fixtures/" + constant + ".dec")
    for line in path.read_text().splitlines():
        row = line.split()
        if not row or row[0].startswith("#"):
            continue
        if path.relative_to(ROOT).as_posix() in SELFTEST_MANIFESTS and row[0] == "malformed":
            continue  # This exact reviewed control has no syntactically valid source field.
        if row[0] == "subst":
            for name in row[1:]:
                add(name)
        elif row[0] == "cc":
            if row[1:] != ["pinned"]:
                source("+".join(row[1:]))
        elif len(row) > 2 and row[1] != "notrun":
            source(row[2], review=row[1] in ("run", "kill"))
            if row[1] == "same":
                source(row[3])
            for spec in row[3:]:
                if spec.startswith(("file:", "has:", "tok:")):
                    add(spec.split(":", 1)[1])
    return list(resources.values())


@lru_cache(maxsize=None)
def selftest_expectations(path):
    """Parse the original selftest's explicit comment groups, not case indices."""
    verdicts, group, overall = {}, None, None
    for line in path.read_text().splitlines():
        if line.startswith("#"):
            match = re.search(r"(?:expected|Expected)(?::\s+([\w-]+))?\s+(PASS|FAIL|INVALID|NOT_RUN)\b", line)
            if match:
                if match[1]:
                    verdicts[match[1]] = match[2]
                else:
                    group = match[2]
            elif "every case must PASS" in line:
                group = "PASS"
            exit_match = re.search(r"\bexit ([0-3])\b", line)
            if exit_match:
                overall = int(exit_match[1])
            continue
        fields = line.split()
        if not fields or fields[0] in ("subst", "cc", "deadline"):
            continue
        if fields[0] not in verdicts:
            if group is None:
                raise Failure("selftest case has no explicit comment verdict: " + line)
            verdicts[fields[0]] = group
    return verdicts, overall


def conductor_case(path, name, line, alternate, compiler_subst):
    rel = path.relative_to(ROOT).as_posix()
    selftest = rel in SELFTEST_MANIFESTS
    old_om2_refusal = (rel == "seed/qualification/om2/leak/leak.cases"
                      and line.split()[0] == "s1-rot-refused")
    historical = frozen_manifest(rel) or rel.endswith("/manifest-c2.txt") or old_om2_refusal
    adequacy = rel == "seed/qualification/om2/baseline/adequacy.cases"
    if not selftest and not historical and not adequacy and rel not in CONDUCTOR_FAILURES:
        raise Failure(f"alternate compiler manifest has no reviewed original-verdict authority: {rel}")
    resources = conductor_resources(path)
    original_name = line.split()[0]
    expected = (selftest_expectations(path)[0][original_name] if selftest else
                ("FAIL" if adequacy or original_name in CONDUCTOR_FAILURES.get(rel, "").split() else "PASS"))
    historical_failure = None
    if rel == "seed/qualification/om2/candidates/s2/s2-mutants.cases":
        failures = {"unroll-kplus1-kill-unroll-nest": "compile exit=21 expected=0"}
        for arm, code in (("B", 101), ("C", 105), ("D", 107), ("E", 101),
                          ("F", 101), ("H", 105), ("I", 102)):
            failures["fold-div0-kill-fold-trap-" + arm] = (
                f"run exit={code} expected=0 mutant survived: stdout equals the oracle "
                "stderr=match; not a meaningful kill")
        if original_name in failures:
            # f56a372ae (#971) moved only the four ctl-*-sha control sources from the deleted
            # om/sha256/sha256.om to seed/qualification/om2/baseline/sha256-om1.om, whose sha256
            # 7f034814...4e21 equals the acb3f15 source pinned below; kill and failure rows unchanged.
            # Previous manifest sha256 0ac554e4497232cc1ba99caca4e07cb12c62b5e3dd1c4af027864a31b29fbc64.
            if digest(path.read_bytes()) != "69eb3687c3f0a108c4aab3a7989ded6de0e46b118b4e2856ebfbacbaec3cc90c":
                raise Failure("historical S2 failure contract manifest changed")
            historical_failure = {
                "original_expected": expected, "original_observed": "FAIL",
                "exact_verdict": f"VERDICT {original_name} FAIL {failures[original_name]}",
                "scope": "historical failed experiment; reproduction is not a successful mutant kill"}
    authority = "\n".join(row for row in path.read_text().splitlines() if row.startswith("#"))
    authority_path = rel
    if rel.endswith("s1-lexmutants.cases"):
        authority_path = "seed/qualification/om2/candidates/s1/RESULTS.txt"
        authority = "\n".join(contained(authority_path).read_text().splitlines()[37:43])
    return Case(name, contained("seed/qualification/om2/gate.om").read_bytes(),
                driver="corpus-gate", operation="conductor",
                expected_compile="accept", evidence={
                    "manifest": rel, "resources": resources, "conductor_row": line,
                    "alternate_compiler": alternate, "compiler_substitution": compiler_subst,
                    "child_compiler_identity": "pinned" if old_om2_refusal else conductor_identity(rel),
                    "expected_failure_reason": "mutant survived" if adequacy else NEGATIVE_CONDUCTOR_MANIFESTS.get(rel),
                    "control_kind": "supervisor_verdict_control" if selftest else ("compiler_mutant" if expected == "FAIL" else "compiler_control"),
                    "expected_overall_exit": selftest_expectations(path)[1] if selftest else None,
                    "expected_original_conductor": expected, "original_case_name": original_name,
                    "historical_failure": historical_failure,
                    "authority": {"path": authority_path, "sha256": digest(contained(authority_path).read_bytes()),
                                  "quote": authority}})


def current_c2_contract(fields):
    """Use the already accepted deltas, retaining the frozen replay separately."""
    for relative in ("seed/qualification/om2/manifest-c2-capacity.txt",
                     "seed/qualification/om2/manifest-c2-batch2.txt",
                     "seed/qualification/om2/manifest-c2-fpvreg.txt",
                     "seed/qualification/om2/manifest-c2-closerange.txt"):
        path = ROOT / relative
        for line in path.read_text().splitlines():
            replacement = line.split()
            if not replacement or replacement[0].startswith("#") or replacement[0] != fields[0]:
                continue
            if replacement[1:3] != fields[1:3]:
                raise Failure("current C2 delta changed the frozen operation or input")
            return replacement, {"path": relative, "sha256": digest(path.read_bytes()),
                                 "frozen_row": " ".join(fields), "current_row": line}
    return fields, None


def cases(files, record):
    for path in files:
        rel = path.relative_to(ROOT).as_posix()
        if path.suffix == ".tsv":
            try:
                yield from unit_cases(path)
            except (OSError, ValueError, Failure) as error:
                record("MISSING_INPUT", rel, str(error))
            continue
        if path.suffix in (".om", ".omegese"):
            source = path.read_bytes()
            # Conductors are real compiler inputs too. Their effects are gated
            # by reviewed drivers, not dropped just because they test a checker.
            yield Case(rel, source)
            if rel in ("seed/rung4/tests/echo.om", "seed/rung4/tests/bytecopy.om"):
                yield Case(rel + ":gate4-nonempty", source, stdin_bytes("seq:50000"))
            continue
        alternate, compiler_subst, subst = None, None, None
        deadline = 30  # Original gate.om's documented default, seconds.
        for number, line in enumerate(path.read_text().splitlines(), 1):
            fields = line.split()
            if not fields or fields[0].startswith("#"):
                continue
            name = f"{rel}:{number}:{fields[0]}"
            if fields[0] == "cc":
                alternate = None if fields[1:] == ["pinned"] else "+".join(fields[1:])
                compiler_subst, subst = subst, None
                record("EXCLUDED", name,
                       f"compiler-selection directive {' '.join(fields[1:])}; subsequent rows retain this compiler identity",
                       conductor_row=line, compiler_substitution=compiler_subst)
                continue
            if fields[0] == "deadline":
                if len(fields) != 2 or not fields[1].isdigit() or not 1 <= int(fields[1]) <= 900:
                    record("INVALID", name, "deadline requires original conductor's integer seconds 1..900", conductor_row=line)
                    continue
                deadline = int(fields[1])
                record("EXCLUDED", name,
                       f"conductor-only metadata: exact {deadline}-second deadline attached to subsequent child arms",
                       conductor_row=line, manifest_sha256=digest(path.read_bytes()))
                continue
            if fields[0] == "subst":
                subst = fields[1:]
                record("EXCLUDED", name,
                       f"stateful substitution {' '.join(fields[1:])}; not a standalone source; interpreted by the next arm or complete original selftest conductor",
                       conductor_row=line, manifest_sha256=digest(path.read_bytes()), substitution_paths=fields[1:])
                continue
            if len(fields) < 2:
                record("INVALID", name, "malformed conductor row", conductor_row=line)
                continue
            mode = fields[1]
            if rel == "seed/qualification/om2/leak/leak.cases" and fields[0] == "s1-rot-refused":
                yield conductor_case(path, name + ":frozen-provenance", line, None, None)
                current_path = ROOT / "seed/qualification/om2/leak/candidate.cases"
                current = next(row for row in current_path.read_text().splitlines()
                               if row.startswith("s1-rot-build "))
                replacement = current.split()
                if replacement[1:] != ["emit", fields[2], "any", "-", "0"]:
                    raise Failure("current OM-2 acceptance contract changed")
                yield Case(name + ":current", source_bytes(fields[2]), expected_compile="accept",
                           operation="emit", stdout_oracle="any", stderr_oracle="-",
                           evidence={"historical_obligation": name, "accepted_current_contract": {
                               "path": current_path.relative_to(ROOT).as_posix(),
                               "sha256": digest(current_path.read_bytes()), "row": current}})
                continue
            if frozen_manifest(rel):
                yield conductor_case(path, name, line, None, None)
                # The old compiler's oracle remains a separate, mandatory replay.
                # Current code generation is additionally compared, not substituted.
                source = source_bytes(fields[2])
                yield Case(name + ":current-differential", source,
                           stdin=stdin_bytes(fields[3]) if fields[1] == "run" else b"",
                           args=tuple(fields[7:]) if fields[1] == "run" else (),
                           evidence={"historical_obligation": name, "conductor_row": line})
                continue
            if rel == "seed/qualification/om2/baseline/adequacy.cases":
                yield conductor_case(path, name, line, None, None)
                subst = None
                continue
            if rel in MUTANT_SPECIFICATIONS and mode != "notrun":
                matches = []
                for candidate in sorted((ROOT / "seed/qualification/om2/candidates").rglob("*mutants.cases")):
                    for candidate_line in candidate.read_text().splitlines():
                        candidate_fields = candidate_line.split()
                        if candidate_fields and candidate_fields[1:] == fields[1:]:
                            matches.append(conductor_case(candidate, name, candidate_line, None, None))
                if not matches:
                    record("FAIL", name, "mutant specification has no executable compiler mutation", conductor_row=line)
                else:
                    obligation = matches[0]
                    replays = [dict(item.evidence) for item in matches]
                    obligation.evidence["mutant_specification"] = {"path": rel, "line": number, "row": line}
                    obligation.evidence["required_mutant_replays"] = replays
                    yield obligation
                continue
            if rel in SELFTEST_MANIFESTS:
                try:
                    yield conductor_case(path, name, line, alternate, compiler_subst)
                except (OSError, ValueError, Failure) as error:
                    record("FAIL", name, str(error), conductor_row=line)
                subst = None
                continue
            if mode == "notrun":
                try:
                    binding = bridge_plan(ROOT, name)
                except (OSError, ValueError, Failure) as error:
                    record("FAIL", name, "reviewed bridge plan: " + str(error), conductor_row=line)
                    continue
                if binding:
                    if binding["operation"] == "units-check":
                        source = contained(binding["script"]).read_bytes()
                    elif "driver_source" in binding:
                        source = binding["driver_source"]
                    else:
                        source = contained(binding["driver"]).read_bytes()
                    yield Case(name, source, driver="external-c-boundary", operation="bridge", bridge=binding,
                               args=tuple(binding["args"]), expected_exit=binding["expected_exit"],
                               evidence={"conductor_row": line, "unit_sources": binding["units"],
                                         "driver_sha256": binding["driver_sha256"], **binding["evidence"]})
                    continue
                plan = directive_plan(ROOT, name)
                evidence = {"conductor_row": line, **(plan.get("evidence", {}) if plan else {})}
                if plan and "source" in plan:
                    yield Case(name, plan["source"], stdin=plan.get("stdin", b""),
                               args=tuple(plan.get("args", ())), driver=plan.get("driver"),
                               expected_exit=plan.get("expected_exit"),
                               expected_compile=plan.get("expected_compile", "accept"),
                               operation=plan.get("operation", "differential"), evidence=evidence, deadline=deadline,
                               stdout_oracle=plan.get("stdout_oracle"), stderr_oracle=plan.get("stderr_oracle"))
                else:
                    reason = plan.get("blocker", " ".join(fields[2:])) if plan else " ".join(fields[2:])
                    record("BLOCKED" if reason.startswith("BLOCKED_ON ") else "NOT_RUN",
                           name, reason, evidence=evidence, conductor_row=line)
                continue
            if rel in NEGATIVE_CONDUCTOR_MANIFESTS:
                yield conductor_case(path, name, line, None, None)
                subst = None
                continue
            if alternate:
                try:
                    yield conductor_case(path, name, line, alternate, compiler_subst)
                except (OSError, ValueError, Failure) as error:
                    record("BLOCKED", name, str(error), conductor_row=line,
                           alternate_compiler=alternate, compiler_substitution=compiler_subst)
                subst = None
                continue
            try:
                source = source_bytes(fields[2])
                if rel.endswith("/manifest-c2.txt"):
                    frozen = conductor_case(path, name + ":frozen-provenance", line, None, None)
                    yield frozen
                evidence = {"conductor_row": line, "source_spec": fields[2], "conductor_deadline_seconds": deadline}
                if rel.endswith("/manifest-c2.txt"):
                    fields, replacement = current_c2_contract(fields)
                    if replacement:
                        evidence["accepted_current_contract"] = replacement
                if ("om/sha256/sha256.om" in fields[2] or
                        "seed/qualification/om2/baseline/sha256-om1.om" in fields[2]):
                    evidence["historical_source"] = {
                        "path": "om/sha256/sha256.om", "revision": "acb3f15",
                        "packed_spec": "pack:seed/qualification/om2/c2/corpus.pack:12540:35333",
                        "sha256": "7f034814303a8a40b4bc56383ddd55ab2ae8460291f80793340bc3b0ac024e21",
                        "provenance": "seed/qualification/om2/c2/index.tsv:c2-00003"}
                if subst is not None:
                    if mode == "same":
                        raise Failure("original conductor forbids substitution before same")
                    if len(subst) != 2:
                        raise Failure("malformed subst directive")
                    old, new = (source_bytes(part) for part in subst)
                    if not old or source.count(old) != 1:
                        raise Failure("subst source must contain FROMFILE exactly once")
                    source = source.replace(old, new)
                    evidence["source_substitution"] = subst
                    subst = None
                if mode in ("run", "kill") and len(fields) >= 7:
                    yield Case(name, source, stdin_bytes(fields[3]), tuple(fields[7:]),
                               expected_exit=int(fields[6]), expected_compile="accept",
                               operation=mode, stdout_oracle=fields[4], stderr_oracle=fields[5], evidence=evidence, deadline=deadline)
                elif mode == "emit" and len(fields) == 6:
                    code = int(fields[5])
                    yield Case(name, source, expected_exit=code,
                               expected_compile="accept" if code == 0 else str(code),
                               operation=mode, stdout_oracle=fields[3], stderr_oracle=fields[4], evidence=evidence, deadline=deadline)
                elif mode == "refuse" and len(fields) == 4:
                    yield Case(name, source, expected_compile=fields[3], operation=mode, evidence=evidence, deadline=deadline)
                elif mode == "same" and len(fields) == 4:
                    yield Case(name, source, expected_compile="accept", operation=mode,
                               same_source=source_bytes(fields[3]), evidence=evidence, deadline=deadline)
                else:
                    record("INVALID", name, "unknown/malformed conductor operation " + mode, conductor_row=line)
            except (OSError, ValueError, Failure) as error:
                record("MISSING_INPUT", name, str(error), conductor_row=line)


def limits(cpu_seconds=None, nofile=256):
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_FSIZE, (64 * 1024 * 1024, 64 * 1024 * 1024))
    resource.setrlimit(resource.RLIMIT_NOFILE, (nofile, nofile))
    if cpu_seconds is not None:
        # Soft limit delivers SIGXCPU; the higher hard limit keeps SIGKILL out of the way.
        resource.setrlimit(resource.RLIMIT_CPU, (cpu_seconds, cpu_seconds + 2))


def run(command, data, cwd, timeout, *, extra_env=None, cpu_seconds=None, nofile=256):
    # Regular-file stdin matches seekable-input fixtures; no shell interpolation.
    with tempfile.TemporaryFile() as inp, tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err:
        inp.write(data)
        inp.seek(0)
        process = subprocess.Popen(command, stdin=inp, stdout=out, stderr=err, cwd=cwd,
                                   env={"LC_ALL": "C", "PATH": os.defpath, "HOME": ".", "OMTEST": "hello", **(extra_env or {})},
                                   start_new_session=True, preexec_fn=lambda: limits(cpu_seconds, nofile))
        try:
            process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            out.seek(0, os.SEEK_END)
            out.seek(max(0, out.tell() - 4096))
            err.seek(0, os.SEEK_END)
            err.seek(max(0, err.tell() - 4096))
            raise Failure(f"child timeout after {timeout}s: {command!r}; stdout_tail={out.read()!r}; stderr_tail={err.read()!r}")
        finally:
            # Also remove descendants of a child which exited before its children.
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        out.seek(0)
        err.seek(0)
        return process.returncode, out.read(), err.read()


def elf_range(image):
    if len(image) < 64 or image[:6] != b"\x7fELF\x02\x01":
        raise Failure("not ELF64 little endian")
    if struct.unpack_from("<H", image, 18)[0] != 62:
        raise Failure("ELF e_machine is not x86-64")
    phoff = struct.unpack_from("<Q", image, 32)[0]
    size, count = struct.unpack_from("<HH", image, 54)
    executable = []
    for i in range(count):
        p = phoff + i * size
        if p + 56 > len(image):
            raise Failure("truncated ELF program headers")
        kind, flags, offset, address, _, length, _, _ = struct.unpack_from("<IIQQQQQQ", image, p)
        if kind == 1 and flags & 1:
            executable.append((offset, address, length))
    if len(executable) != 1:
        raise Failure("expected one executable PT_LOAD")
    offset, address, length = executable[0]
    # FR-9 sectionless emitter reserves bytes [0,512) for ELF headers, then emits
    # code/literals in source order. This is a checked format contract, not guessed
    # by looking for byte sequences that resemble function prologues.
    if offset != 0 or length != len(image) or length <= 512:
        raise Failure("unsupported executable layout: expected whole-file PT_LOAD and 512-byte prelude")
    return 512, length, address


def literal_bytes(source):
    result = []
    for match in TEXT.finditer(source):
        literal = re.sub(rb'\\([nt\\"])', lambda m: {b'n': b'\n', b't': b'\t', b'\\': b'\\', b'"': b'"'}[m[1]], match[1])
        result.append(literal)
    return result


def code_spans(image, source):
    if not MAIN.search(source) and image[:4] != b"\x7fELF":
        # Bare x86 units omit the reserved 512 bytes. Their six-byte entry
        # branches and function bodies use the private frame ABI, not SysV.
        if not image:
            raise Failure("empty bare unit")
        start, end, address = 0, len(image), 0
    else:
        start, end, address = elf_range(image)
    cursor, spans, carveouts = start, [], []
    # Every literal must be unambiguously protected by a near JMP, exactly match
    # the source-decoded bytes, and have only zero alignment padding. Ambiguous
    # matches fail closed rather than silently carving possible instructions out.
    # Empty text emits no bytes, jump or padding in the integrated compiler.
    # It creates no carveout: every executable byte still goes to the decoder.
    literals = [literal for literal in literal_bytes(source) if literal]
    locations = {}
    for literal in set(literals):
        candidates = []
        for p in range(start, end - 4):
            if image[p] != 0xe9:
                continue
            after = p + 5 + struct.unpack_from("<i", image, p + 1)[0]
            # xbranch reserves six bytes: E9 rel32 followed by a NOP.
            # The NOP is an instruction, not a literal/data carveout.
            if p + 5 >= end or image[p + 5] != 0x90:
                continue
            data_start = p + 6
            data_end = data_start + len(literal)
            if (data_end <= after <= data_end + 3 and after <= end and
                    image[data_start:data_end] == literal and
                    image[data_end:after] == b"\0" * (after - data_end)):
                candidates.append((p, data_start, after))
        locations[literal] = candidates
    earliest, latest = [], []
    lower = start
    for literal in literals:
        candidate = next((c for c in locations[literal] if c[0] >= lower), None)
        if candidate is None:
            raise Failure("ORACLE_COVERAGE_MISSING no ordered branch-around location for source literal")
        earliest.append(candidate)
        lower = candidate[2]
    upper = end
    for literal in reversed(literals):
        candidate = next((c for c in reversed(locations[literal]) if c[2] <= upper), None)
        if candidate is None:
            raise Failure("ORACLE_COVERAGE_MISSING no reverse ordered literal mapping")
        latest.append(candidate)
        upper = candidate[0]
    if earliest != list(reversed(latest)):
        raise Failure("ORACLE_COVERAGE_MISSING ambiguous ordered literal mapping")
    for p, data_start, after in earliest:
        spans.append((cursor, data_start))
        carveouts.append((data_start, after))
        cursor = after
    spans.append((cursor, end))
    return [(a, b, address) for a, b in spans if b > a], carveouts


def oracle_elf(code, address):
    # An oracle-only ELF wrapper exposes a code span to either disassembler. It
    # does not transform or implement the candidate instructions.
    names = b"\0.text\0.shstrtab\0"
    names_at = 64 + len(code)
    sections_at = (names_at + len(names) + 7) & ~7
    header = struct.pack("<16sHHIQQQIHHHHHH", b"\x7fELF\x02\x01\x01" + bytes(9),
                         1, 62, 1, 0, 0, sections_at, 0, 64, 0, 0, 64, 3, 2)
    text = struct.pack("<IIQQQQIIQQ", 1, 1, 6, address, 64, len(code), 0, 0, 1, 0)
    strings = struct.pack("<IIQQQQIIQQ", 7, 3, 0, 0, names_at, len(names), 0, 0, 1, 0)
    body = header + code + names
    return body + bytes(sections_at - len(body)) + bytes(64) + text + strings


def disassemble(image, source, objdump, llvm, cwd, timeout):
    spans, carveouts = code_spans(image, source)
    mnemonics = Counter()
    instructions = []
    for n, (start, end, base) in enumerate(spans):
        cursor = base + start
        while cursor < base + end:
            offset = cursor - base
            chunk_end = min(offset + 65536, end)
            # Decode from an established instruction boundary, with enough
            # lookahead to finish a boundary-crossing x86 instruction. Consume
            # only through that instruction, then start the next bounded chunk
            # there. The child output cap never truncates a large whole image.
            decode_end = min(chunk_end + 15, end)
            wrapper = cwd / f"oracle-{n}.elf"
            wrapper.write_bytes(oracle_elf(image[offset:decode_end], cursor))
            options = ["-d", "-z", "--x86-asm-syntax=intel"] if llvm else ["-d", "-z", "-M", "intel", "--insn-width=16"]
            rc, output, error = run([objdump, *options, str(wrapper)], b"", cwd, timeout)
            if rc:
                raise Failure(f"disassembler refusal: exit={rc} stdout_bytes={len(output)} stderr={error!r}")
            for line in output.decode("utf-8", "replace").splitlines():
                match = INSTRUCTION.match(line)
                if not match:
                    continue
                address = int(match[1], 16)
                raw = bytes.fromhex(match[2])
                asm = match[3].strip()
                if address != cursor or not raw or not asm:
                    raise Failure("disassembly byte gap/overlap or undecoded instruction: " + line)
                offset = address - base
                if image[offset:offset + len(raw)] != raw:
                    raise Failure("disassembler bytes differ from emitted image")
                if re.search(r"\(bad\)|<unknown>|\.byte|\.word|\.short|\.long|\.quad", asm):
                    raise Failure("invalid instruction: " + line)
                mnemonic = asm.split()[0]
                if mnemonic in ("rex", "rex.W", "data16", "addr32", "rep", "repz", "repnz", "lock") and len(asm.split()) == 1:
                    raise Failure("orphan instruction prefix: " + line)
                mnemonics[mnemonic] += 1
                instructions.append((offset, raw, asm))
                cursor += len(raw)
                if cursor >= base + chunk_end:
                    break
            if cursor < base + chunk_end or cursor > base + end:
                raise Failure(f"disassembler did not cover every code byte: stopped {cursor:x}, expected chunk {base + chunk_end:x}")
    boundaries = {base + offset for offset, _, _ in instructions}
    for offset, raw, asm in instructions:
        branch = re.match(r"(?:j\w+|callq?)\s+(?:0x)?([0-9a-fA-F]+)(?:\s|$)", asm)
        if branch and int(branch[1], 16) not in boundaries:
            raise Failure("direct control-flow target is not a decoded instruction boundary: " + asm)
    return mnemonics, instructions, carveouts


def normalized_diagnostics(error, shift):
    """Undo ONLY the known selector's source-coordinate translation.

    xlead replaces the 14 selector characters by spaces but keeps its LF.
    Human and structured line fields move by one, absolute spans by 15 bytes.
    Columns, codes, token bytes, obligations and all other text stay exact.
    """
    if not shift:
        return error
    translated = []
    for line in error.splitlines(keepends=True):
        if line.startswith(b"omc: line "):
            line = re.sub(rb"(?<=line )\d+(?=: error )",
                          lambda m: str(int(m[0]) - shift if int(m[0]) else 0).encode(), line)
        elif re.match(rb"omc-(?:diag|site|check) 1 ", line):
            line = re.sub(rb"(?<=line=)\d+",
                          lambda m: str(int(m[0]) - shift if int(m[0]) else 0).encode(), line)
            line = re.sub(rb"(?<=span=)(\d+)-(\d+)",
                          lambda m: f"{int(m[1]) - shift * len(TARGET_PREFIX)}-{int(m[2]) - shift * len(TARGET_PREFIX)}".encode(),
                          line)
        translated.append(line)
    return b"".join(translated)


def source_site_diagnostics(native, target, source_lines):
    """Compare reviewed source sites, retaining architecture-specific stub tables."""
    allowed = set(source_lines)

    def parse(data):
        other, all_sites, source_sites, seen = [], [], [], set()
        for line in data.splitlines(keepends=True):
            if not line.startswith(b"omc-site "):
                other.append(line)
                continue
            match = re.fullmatch(rb"omc-site 1 line=(\d+) iter=(-|\d+(?:\.\d+)*)\n", line)
            if not match:
                raise Failure("malformed compiler site record: " + repr(line))
            site = int(match[1]), match[2].decode()
            all_sites.append(site)
            if site[0] in allowed and site not in seen:
                source_sites.append(site)
                seen.add(site)
        return b"".join(other), all_sites, source_sites

    left, right = parse(native), parse(target)
    observation = {"native_sites": left[1], "x86_normalized_sites": right[1],
                   "native_source_sites": left[2], "x86_source_sites": right[2],
                   "reviewed_source_lines": source_lines,
                   "comparison": "ordered first-occurrence (source line, unrolled iteration); all non-site diagnostic bytes exact"}
    if not left[2] or left[0] != right[0] or left[2] != right[2]:
        raise Failure("source-level site/non-site compiler diagnostic differential: " + str(observation))
    return observation


@lru_cache(maxsize=1)
def diagnostic_contracts():
    path = ROOT / "seed/omc/x86-diagnostic-contracts.json"
    document = json.loads(path.read_text())
    if document["schema"] != 1:
        raise Failure("unsupported target diagnostic contract schema")
    contracts = {}
    for entry in document["contracts"]:
        key = entry["source_sha256"]
        if key in contracts or digest(source_bytes(entry["source_spec"])) != key:
            raise Failure("duplicate or stale target diagnostic contract: " + entry["source_spec"])
        contracts[key] = entry
    return contracts


def target_diagnostic_contract(source, native, target):
    """Assert exact records, never discard target-specific safety checks."""
    contract = diagnostic_contracts().get(digest(source))
    if contract is None:
        return None
    for architecture, actual in (("aarch64", native), ("x86_64", target)):
        expected = contract[architecture]
        if actual[0] != expected["exit"] or actual[2] != expected["stderr"].encode():
            raise Failure("exact target diagnostic contract differs: " + architecture)
        if expected["exit"]:
            refusal(actual, 0)  # Refusals must still be clean, with no artifact.
    return {"path": "seed/omc/x86-diagnostic-contracts.json",
            "sha256": digest((ROOT / "seed/omc/x86-diagnostic-contracts.json").read_bytes()),
            "source_spec": contract["source_spec"], "reason": contract["reason"],
            "x86_execution": contract.get("x86_execution")}


def refusal(result, shift):
    rc, output, error = result
    match = DIAG.search(error)
    if rc <= 0 or output or not match or int(match[2]) != rc:
        raise Failure("not a clean compiler refusal")
    return rc, output, normalized_diagnostics(error, shift)


def matches_oracle(actual, spec):
    if spec is None or spec == "any":
        return True
    if spec == "-":
        return actual == b""
    if spec.startswith("sha:"):
        return digest(actual) == spec[4:]
    if spec.startswith("file:"):
        return actual == contained(spec[5:]).read_bytes()
    if spec.startswith("line:"):
        return actual == spec[5:].encode() + b"\n"
    if spec.startswith("err:"):
        _, line, code = spec.split(":")
        return actual == f"omc: line {line}: error {code}\n".encode()
    if spec.startswith(("has:", "tok:")):
        kind, path = spec.split(":", 1)
        expected = contained(path).read_bytes().splitlines()
        observed = actual.splitlines()
        if kind == "has":
            return all(line in observed for line in expected)
        return all(any(all(word in line.split() for word in wanted.split())
                       for line in observed) for wanted in expected)
    raise Failure("unknown output oracle grammar: " + spec)


def assert_oracle(actual, spec, label, different=False):
    matched = matches_oracle(actual, spec)
    if matched == different:
        raise Failure(f"{label} {'mutant survived (equals)' if different else 'differs from'} "
                      f"oracle {spec}: bytes={len(actual)} sha256={digest(actual)} "
                      f"prefix={actual[:160]!r}")


def result_details(result):
    return {"exit": result[0], "stdout_bytes": len(result[1]),
            "stdout_sha256": digest(result[1]), "stdout_prefix": repr(result[1][:160]),
            "stderr_bytes": len(result[2]), "stderr_sha256": digest(result[2]),
            "stderr": result[2][:8192].decode("utf-8", "backslashreplace")}


def ioctl_emulator_artifact(case, driver, arun, xrun, qemu, argv0, args, cwd, timeout):
    # Not a PASS or an errno waiver: challenge the exact failed syscall under
    # QEMU's own tracer, after the pinned native fixture has passed its EBADF
    # assertions. Unknown ioctls are rejected before host ioctl(fd,cmd,arg).
    request = {"devcall": 3223340587, "uvmctl": 805306369}.get(driver)
    if digest(Path(qemu).read_bytes()) != QEMU_ORACLE_SHA256:
        return None  # No source attribution for an unreviewed QEMU executable.
    if request is None or arun[0] != 0 or xrun != (80, b"", b""):
        return None
    trace = run([qemu, "-strace", "-0", argv0, argv0, *args], case.stdin, cwd, timeout)
    if trace[:2] != xrun[:2]:
        return None
    for line in trace[2].splitlines():
        match = re.search(rb"ioctl\(-1,([^,]+),.*\)\s*=\s*-1 errno=25\b", line)
        if match:
            try:
                observed = int(match[1], 0) & 0xffffffff
            except ValueError:
                continue
            if observed == request:
                return {"classification": "EMULATOR_ARTIFACT", "syscall": "ioctl", "fd": -1,
                        "request": request, "native_required_errno": "EBADF",
                        "qemu_observed_errno": "ENOTTY", "qemu_trace_line": line.decode(),
                        "qemu_trace_sha256": digest(trace[2]),
                        "upstream_source": QEMU_IOCTL_URL,
                        "upstream_source_sha256": QEMU_SYSCALL_SHA256,
                        "upstream_checked_source_path": "/home/drakestapleton/work/bridge-x86/build/qemu-oracle/qemu-10.0.14/linux-user/syscall.c",
                        "upstream_source_archive": "https://download.qemu.org/qemu-10.0.14.tar.xz",
                        "upstream_source_archive_sha256": QEMU_SOURCE_ARCHIVE_SHA256,
                        "qemu_binary_sha256": QEMU_ORACLE_SHA256,
                        "upstream_excerpt": QEMU_IOCTL_EXCERPT.decode(),
                        "upstream_excerpt_sha256": digest(QEMU_IOCTL_EXCERPT)}
    return None


def filesystem_snapshot(directory, excluded):
    """Compare persistent effects, not inode/timestamp/owner allocation noise."""
    snapshot = {}
    for path in sorted(directory.rglob("*")):
        rel = path.relative_to(directory).as_posix()
        if rel in excluded:
            continue
        info = path.lstat()
        entry = {"mode": stat.S_IMODE(info.st_mode)}
        if stat.S_ISLNK(info.st_mode):
            entry.update(type="symlink", target=os.readlink(path))
        elif stat.S_ISREG(info.st_mode):
            entry.update(type="file", bytes=info.st_size, sha256=digest(path.read_bytes()))
        elif stat.S_ISDIR(info.st_mode):
            entry.update(type="directory")
        else:
            entry.update(type=stat.S_IFMT(info.st_mode))
        snapshot[rel] = entry
    return snapshot


def exercise_bare(case, default, x86, qemu, cwd, timeout):
    specs = bare_calls(ROOT, case.name.split(":", 1)[0], case.source)
    outcomes = []
    for number, spec in enumerate(specs):
        runs, wrappers = [], []
        for arch, image in (("aarch64", default), ("x86_64", x86)):
            work = cwd / f"caller-{number}-{arch}"
            work.mkdir()
            wrapped = wrap_bare(image, case.source, arch, spec)
            binary = work / "caller"
            binary.write_bytes(wrapped)
            binary.chmod(0o700)
            wrappers.append(digest(wrapped))
            command = ["./caller"] if arch == "aarch64" else [qemu, "-0", "./caller", "./caller"]
            runs.append(run(command, case.stdin, work, timeout))
        if runs[0][0] < 0 or runs[1][0] < 0 or runs[0] != runs[1]:
            raise Failure(f"bare caller {spec['name']} differs: default={result_details(runs[0])}; x86={result_details(runs[1])}")
        expected_exit = spec.get("expected_exit", 0)
        if expected_exit is not None and runs[0][0] != expected_exit:
            raise Failure(f"bare caller {spec['name']} exit {runs[0][0]}, expected {expected_exit}")
        if "expected_stdout" in spec and runs[0][1] != spec["expected_stdout"]:
            raise Failure(f"bare caller {spec['name']} full-width return/array oracle differs: "
                          f"expected={spec['expected_stdout']!r}; actual={runs[0][1]!r}")
        outcomes.append({"name": spec["name"], "evidence": spec.get("evidence", {}),
                         "caller_inputs": {"slots": spec.get("slots", []),
                                           "calls": spec.get("calls", []),
                                           "arrays": [{key: value for key, value in array.items() if key != "data"} |
                                                      {"initial_bytes": len(array.get("data", b"")),
                                                       "initial_sha256": digest(array.get("data", b""))}
                                                      for array in spec["arrays"]]},
                         "expected_exit": expected_exit, "observed": result_details(runs[0]),
                         "oracle": "independent full-width/array oracle" if "expected_stdout" in spec else "native differential",
                         "wrapper_sha256": dict(zip(("aarch64", "x86_64"), wrappers))})
    return outcomes


def exercise_bare_target(case, x86, qemu, cwd, timeout):
    """x86-only private-ABI calls for an entryless unit that only x86 accepts (a reviewed per-target
    limit). There is no native arm to compare, so every call must meet a recorded independent result."""
    specs = bare_calls(ROOT, case.name.split(":", 1)[0], case.source)
    if not specs or any("expected_stdout" not in spec or spec.get("expected_exit") is None for spec in specs):
        raise Failure("target-limit bare caller requires recorded independent results for every call")
    outcomes = []
    for number, spec in enumerate(specs):
        work = cwd / f"target-limit-caller-{number}"
        work.mkdir()
        wrapped = wrap_bare(x86, case.source, "x86_64", spec)
        binary = work / "caller"
        binary.write_bytes(wrapped)
        binary.chmod(0o700)
        observed = run([qemu, "-0", "./caller", "./caller"], case.stdin, work, timeout)
        if observed[0] != spec["expected_exit"] or observed[1] != spec["expected_stdout"]:
            raise Failure(f"target-limit bare caller {spec['name']} differs from recorded result: "
                          f"expected exit {spec['expected_exit']} stdout {spec['expected_stdout']!r}; "
                          f"x86={result_details(observed)}")
        outcomes.append({"name": spec["name"], "evidence": spec.get("evidence", {}),
                         "slots": spec.get("slots", []), "expected_exit": spec["expected_exit"],
                         "observed": result_details(observed), "wrapper_sha256": digest(wrapped)})
    return outcomes


def exercise_conductor(case, default, x86, compiler, qemu, cwd, timeout):
    runs, effects = [], []
    # The original conductor itself enforces each directive's child deadline.
    # Its enclosing process must have time for all declared compile/run arms.
    lines = contained(case.evidence["manifest"]).read_text().splitlines()
    outer_timeout = max(timeout, 60) * max(1, len(lines))
    for arch, image in (("aarch64", default), ("x86_64", x86)):
        work = cwd / ("conductor-" + arch)
        work.mkdir()
        binary = work / "conductor"
        binary.write_bytes(image)
        binary.chmod(0o700)
        args = prepare_case(ROOT, work, "corpus-gate", compiler, case=case.evidence)
        command = ["./conductor", *args] if arch == "aarch64" else [qemu, "-0", "./conductor", "./conductor", *args]
        runs.append(run(command, b"", work, outer_timeout))
        effects.append(filesystem_snapshot(work, {"conductor"}))
    if runs[0] != runs[1] or effects[0] != effects[1]:
        raise Failure(f"original conductor architecture differential: native={result_details(runs[0])}; x86={result_details(runs[1])}; filesystem_equal={effects[0] == effects[1]}; native_log={runs[0][1]!r}; x86_log={runs[1][1]!r}")
    if runs[0][0] not in (0, 1, 2, 3) or b"OM2GATE " not in runs[0][1]:
        raise Failure(f"original conductor invalid/setup failure: {result_details(runs[0])}; original_log={runs[0][1]!r}")
    verdicts, directives = {}, []
    for match in re.finditer(rb"(?m)^VERDICT ([^ \n]+) (PASS|FAIL|INVALID|NOT_RUN)([^\n]*)$", runs[0][1]):
        name = match[1].decode()
        verdict = {"status": match[2].decode(), "line": match[0].decode()}
        if name in ("cc", "subst", "deadline"):
            directives.append(verdict)
            continue
        if name in verdicts:
            raise Failure("original conductor emitted duplicate case name: " + name)
        verdicts[name] = verdict
    return {"exit": runs[0][0], "stdout": runs[0][1].decode(), "stdout_sha256": digest(runs[0][1]), "stderr": runs[0][2].decode(),
            "verdicts": verdicts, "directive_verdicts": directives,
            "filesystem_sha256": digest(json.dumps(effects[0], sort_keys=True).encode())}

def exercise_bridge(plan, compiler, qemu, options, scratch, state):
    key = plan["cache_key"]
    if key in state["errors"]:
        raise Failure(state["errors"][key])
    try:
        return _exercise_bridge(plan, compiler, qemu, options, scratch, state)
    except (OSError, ValueError, Failure) as error:
        state["errors"][key] = str(error)
        raise Failure(str(error)) from error


def _exercise_bridge(plan, compiler, qemu, options, scratch, state):
    """Use original reviewed callers and generated native bindings, never raw-ABI substitutes."""
    if plan["cache_key"] in state["results"]:
        return state["results"][plan["cache_key"]]
    if not options.bridge or not options.bridge.is_file():
        raise Failure("BLOCKED_ON BridgeX86: --bridge must name its reviewed tools/native executable")
    bridge = options.bridge.resolve()
    bridge_root = bridge.parents[1]
    work = scratch / ("bridge-" + plan["cache_key"])
    work.mkdir()

    def copy_resources(items, directory):
        for item in items:
            data = (git_resource(ROOT, item) if "git_revision" in item
                    else contained(item["path"]).read_bytes())
            if digest(data) != item["sha256"]:
                raise Failure("reviewed bridge input changed: " + item["path"])
            dest = directory / item["path"]
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)

    def git_resource(repository, item):
        result = run(["git", "-C", str(repository), "show",
                      item["git_revision"] + ":" + item["git_path"]], b"", work, options.timeout)
        if result[0] or digest(result[1]) != item["sha256"]:
            raise Failure(f"pinned bridge resource unavailable/mismatched: {repository} {item}: {result_details(result)}")
        return result[1]

    def mutate(path, mutation):
        data = path.read_bytes()
        if "append" in mutation:
            data += mutation["append"]
        else:
            count = data.count(mutation["old"])
            if count != mutation["count"]:
                raise Failure(f"reviewed mutation {path} expected {mutation['count']} matches, found {count}; "
                              f"source_sha256={digest(data)} old_hex={mutation['old'].hex()} new_hex={mutation['new'].hex()}")
            data = data.replace(mutation["old"], mutation["new"])
        path.write_bytes(data)
        return {"path": str(path.relative_to(work)), "sha256": digest(data),
                "old_hex": mutation.get("old", b"").hex(), "new_hex": mutation.get("new", mutation.get("append", b"")).hex()}

    if plan["operation"] == "units-check":
        tree = work / "tree"
        tree.mkdir()
        copy_resources(plan["resources"], tree)
        old_compiler = work / "pinned-omc"
        old_compiler.write_bytes(git_resource(ROOT, plan["strict_compiler"]))
        old_compiler.chmod(0o700)
        old_bridge = work / "pinned-bridge"
        (old_bridge / "src").mkdir(parents=True)
        (old_bridge / "src/native.om").write_bytes(git_resource(bridge_root, plan["strict_generator"]))
        temp = work / "tmp"
        temp.mkdir()
        environment = {"OMEGA_OMC": str(old_compiler), "OMEGA_BRIDGE": str(old_bridge), "TMPDIR": str(temp)}
        command = ["sh", str(tree / plan["script"]), *plan["args"]]
        before = filesystem_snapshot(tree, set())
        positive = run(command, b"", tree, options.timeout, extra_env=environment)
        if positive[0] != plan["positive_control_expected_exit"] or filesystem_snapshot(tree, set()) != before:
            raise Failure(f"original strict gate positive control failed or wrote source: {result_details(positive)}")
        mutation = mutate(tree / plan["mutation"]["path"], plan["mutation"])
        before = filesystem_snapshot(tree, set())
        negative = run(command, b"", tree, options.timeout, extra_env=environment)
        if negative[0] != plan["expected_exit"] or filesystem_snapshot(tree, set()) != before:
            raise Failure(f"original strict gate did not reject exact stale metadata without writes: {result_details(negative)}")
        result = {"exit": negative[0], "stdout": negative[1].decode(), "stderr": negative[2].decode(),
                  "positive_control": result_details(positive), "negative_control": result_details(negative),
                  "mutation": mutation, "scope": "original host regeneration gate, not x86 device/runtime evidence",
                  "compiler": plan["strict_compiler"], "generator": plan["strict_generator"]}
        state["results"][plan["cache_key"]] = result
        return result

    if "positive_control" in plan:
        positive = exercise_bridge(bridge_positive_plan(ROOT, plan), compiler, qemu, options, scratch, state)
        if positive["exit"] != 0:
            raise Failure("original C-boundary positive control did not pass before negative control")
    baseline = options.bridge_baseline_compiler.resolve()
    selected = (options.bridge_compiler or Path(compiler)).resolve()
    if not baseline.is_file() or not selected.is_file():
        raise Failure("BLOCKED_ON BridgeX86: pinned baseline/candidate compiler artifact missing")
    selected_sha = digest(selected.read_bytes())
    if digest(baseline.read_bytes()) != "734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61":
        raise Failure("BLOCKED_ON BridgeX86: baseline compiler hash does not match its exact seeded generator pin")
    if selected_sha != digest(Path(compiler).read_bytes()):
        raise Failure("BLOCKED_ON BridgeX86: bridge compiler does not match selected qualification candidate")
    native_cc, x86_cc = shutil.which("cc"), shutil.which("x86_64-linux-gnu-gcc")
    nm = shutil.which("nm")
    if not native_cc or not x86_cc or not nm:
        raise Failure("BLOCKED_ON BridgeX86: native cc, x86_64-linux-gnu-gcc and nm are required")
    observations, runs, effects = [], [], []
    driver_timeout = options.timeout
    if plan["cache_key"] == "sha-original-driver-big" and plan["args"] == ("big",):
        driver_timeout = max(driver_timeout, options.bridge_big_timeout)
    for arch, cc in (("aarch64", native_cc), ("x86_64", x86_cc)):
        library_key = arch, tuple((unit["path"], unit["sha256"]) for unit in plan["units"]), selected_sha
        if library_key not in state["libraries"]:
            libwork = Path(tempfile.mkdtemp(prefix="bridge-library-", dir=scratch))
            copy_resources(plan["units"], libwork)
            output = libwork / "generated"
            command = [str(bridge)]
            if arch == "x86_64":
                command += ["--target", "x86linux"]
            command += [str(output), *(str(libwork / unit["path"]) for unit in plan["units"])]
            environment = {"OMEGA_NATIVE_OMC": str(baseline), "OMEGA_NATIVE_OMC2": str(selected),
                           "OMEGA_NATIVE_X86_OMC": str(selected), "CC": cc}
            built = run(command, b"", libwork, options.timeout, extra_env=environment)
            if built[0]:
                raise Failure(f"actual {arch} bridge generation failed: {result_details(built)}")
            state["libraries"][library_key] = output
        library = state["libraries"][library_key]
        local = work / arch
        local.mkdir()
        copy_resources(plan["resources"], local)
        generated = local / "generated"
        shutil.copytree(library, generated)
        mutation = None
        if plan["operation"] == "bridge-mutant":
            mutation = mutate(generated / "omega_native.c", plan["mutation"])
        symbols_result = run([nm, "-D", "--defined-only", str(generated / "libomega_native.so")],
                             b"", local, options.timeout)
        if symbols_result[0]:
            raise Failure("cannot inspect generated bridge symbols: " + str(result_details(symbols_result)))
        symbols = sorted({line.split()[-1].decode() for line in symbols_result[1].splitlines() if line.split()})
        for symbol in plan.get("required_symbols", ()):
            if symbol not in symbols:
                raise Failure("required native binding symbol absent: " + symbol)
        for symbol in plan.get("forbidden_symbols", ()):
            if symbol in symbols:
                raise Failure("forbidden binding symbol exported: " + symbol)
        linked, binary_sha = None, None
        args = ()
        if plan["operation"] == "symbols":
            observed = symbols_result
        else:
            if plan.get("driver") is None:
                driver = local / "original-driver.c"
                driver.write_bytes(plan["driver_source"])
            else:
                driver = local / plan["driver"]
            if digest(driver.read_bytes()) != plan["driver_sha256"]:
                raise Failure("original C driver pin mismatch")
            executable = local / "original-driver"
            command = [cc, *plan["cflags"], "-static", "-I", str(generated),
                       str(driver), str(generated / "omega_native.c"), "-o", str(executable)]
            linked = run(command, b"", local, options.timeout)
            if plan["operation"] == "negative-link":
                symbol = plan["undefined_symbol"].encode()
                if linked[0] != plan["expected_exit"] or b"undefined reference" not in linked[2] or symbol not in linked[2]:
                    raise Failure(f"negative-link failed for wrong reason or survived: {result_details(linked)}")
                observed = linked
            else:
                if linked[0]:
                    raise Failure("original C driver build failed: " + str(result_details(linked)))
                binary_sha = digest(executable.read_bytes())
                args = tuple(selected_sha if arg == "$COMPILER_SHA256" else arg for arg in plan["args"])
                before = filesystem_snapshot(local, set())
                command = ["./original-driver", *args] if arch == "aarch64" else [
                    qemu, "-0", "./original-driver", "./original-driver", *args]
                observed = run(command, b"", local, driver_timeout)
                after = filesystem_snapshot(local, set())
                changes = {name: after.get(name) for name in before.keys() | after.keys()
                           if before.get(name) != after.get(name)}
                effects.append(changes)
                if changes:
                    raise Failure("read-only C-boundary caller changed its scratch filesystem: " + str(changes))
        if observed[0] != plan["expected_exit"]:
            raise Failure(f"original {arch} C-boundary caller expected {plan['expected_exit']}: {result_details(observed)}; stdout={observed[1]!r}; stderr={observed[2]!r}")
        observations.append({"architecture": arch, "compiler_sha256": selected_sha,
                             "args": list(args), "stdin_hex": "", "deadline_seconds": driver_timeout,
                             "caller_resources": plan["resources"], "unit_sources": plan["units"],
                             "baseline_compiler_sha256": digest(baseline.read_bytes()),
                             "bridge_tool_sha256": digest(bridge.read_bytes()),
                             "bridge_generator_sha256": digest((bridge_root / "src/native.om").read_bytes()),
                             "cc": cc, "cc_version": run([cc, "--version"], b"", local, options.timeout)[1].decode(),
                             "generated_c_sha256": digest((generated / "omega_native.c").read_bytes()),
                             "generated_library_sha256": digest((generated / "libomega_native.so").read_bytes()),
                             "driver_sha256": plan.get("driver_sha256"), "driver_binary_sha256": binary_sha,
                             "mutation": mutation, "link": result_details(linked) if linked else None,
                             "symbol_names": symbols, "symbol_observation": result_details(symbols_result),
                             "observed": result_details(observed)})
        runs.append(observed)
    if plan["operation"] not in ("symbols", "negative-link") and runs[0] != runs[1]:
        raise Failure(f"original C-boundary architecture differential: native={result_details(runs[0])}; x86={result_details(runs[1])}")
    result = {"exit": runs[0][0], "stdout": runs[0][1].decode(), "stderr": runs[0][2].decode(),
              "operation": plan["operation"], "observations": observations, "runtime_filesystem_changes": effects,
              "comparison": ("independent original required/forbidden symbol obligations, not runtime"
                             if plan["operation"] == "symbols" else
                             "independent original absent-symbol link obligation; complete linker diagnostics retained, not runtime"
                             if plan["operation"] == "negative-link" else
                             "actual C/SysV native-AArch64 versus EMULATED x86_64")}
    state["results"][plan["cache_key"]] = result
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--compiler", type=Path)
    parser.add_argument("--x86-compiler", type=Path, help="optional x86-hosted paired compiler; otherwise built once from exact current seeded source")
    parser.add_argument("--qemu", default="qemu-x86_64")
    parser.add_argument("--bridge", type=Path, help="reviewed x86-capable omega-bridge executable for external ABI drivers")
    parser.add_argument("--bridge-baseline-compiler", type=Path, default=Path("/tmp/bridge-x86-baseline-omc"),
                        help="exact 734f0a39 seeded compiler required by the bridge generator")
    parser.add_argument("--bridge-compiler", type=Path,
                        help="bridge candidate artifact, required to hash-match --compiler (default: --compiler)")
    parser.add_argument("--bridge-big-timeout", type=float, default=3600,
                        help="per-architecture deadline for unchanged original SHA big mode (all original 2^32+3-byte cases)")
    parser.add_argument("--objdump", help="GNU x86-64 objdump or llvm-objdump executable")
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--inventory", action="store_true")
    options = parser.parse_args()
    totals, reasons, templates = Counter(), Counter(), Counter()

    if options.timeout <= 0:
        parser.error("--timeout must be positive")
    def record(status, name, reason, **details):
        totals[status] += 1
        if status != "PASS":
            reasons[(status, reason)] += 1
        if details.get("evidence", {}).get("evidence_kind") == "differential_only":
            details.update(scope="differential_only", conformance_status="UNASSERTED",
                           original_not_run_reason=details["evidence"].get("original_not_run_reason", details["evidence"].get("reason")))
        print(json.dumps(dict(status=status, case=name, reason=reason, **details)), flush=True)

    files = fixture_files()
    revision = run(["git", "rev-parse", "HEAD"], b"", ROOT, options.timeout)
    if revision[0] or not re.fullmatch(rb"[0-9a-f]{40}\n", revision[1]):
        parser.error("cannot record actual repository HEAD: " + str(result_details(revision)))
    compiler_source = contained("seed/omc/omc.om").read_bytes()
    print(json.dumps({"inventory_files": len(files),
                      "roots": "tests; seed/*/tests; seed/omc/{om2,contracts}; seed/qualification; tools/exp445/arms/examples",
                      "source_revision_contract": revision[1].decode().strip(),
                      "baseline_revision": "becf5c817bcb7e0846d12ebfc2be91042d8ad89a",
                      "compiler_source_sha256": digest(compiler_source),
                      "checker_sha256": digest(Path(__file__).read_bytes()),
                      "driver_sha256": digest(Path(__file__).with_name("x86_corpus_drivers.py").read_bytes()),
                      "bridge": str(options.bridge) if options.bridge else None}))
    if not options.inventory:
        if not options.compiler or not options.compiler.is_file():
            parser.error("--compiler must name an existing compiler")
        if platform.system() != "Linux" or platform.machine() not in ("aarch64", "arm64"):
            parser.error("native default baseline requires Linux AArch64")
        compiler = str(options.compiler.resolve())
        qemu = shutil.which(options.qemu)
        objdump = shutil.which(options.objdump) if options.objdump else next(
            (p for name in ("x86_64-linux-gnu-objdump", "llvm-objdump", "objdump") if (p := shutil.which(name))), None)
        if not qemu or not objdump:
            parser.error("qemu-x86_64 and an x86-64-capable GNU/LLVM objdump are required")
        llvm = "llvm" in Path(objdump).name
        print(json.dumps({"compiler": compiler, "compiler_sha256": digest(Path(compiler).read_bytes()),
                          "qemu": qemu, "qemu_sha256": digest(Path(qemu).read_bytes()),
                          "qemu_version": run([qemu, "--version"], b"", ROOT, options.timeout)[1].decode().strip(),
                          "objdump": objdump, "platform": platform.platform(), "timeout_seconds": options.timeout,
                          "bridge_big_timeout_seconds": options.bridge_big_timeout}))
    cache = OrderedDict()
    conductor_cache = {}
    conductor_decode_cache = OrderedDict()
    bridge_state = {"results": {}, "errors": {}, "libraries": {}}
    controls = None
    helper_image = None
    frontend_compiler = None
    frontend_evidence = {}
    generation_variant = None
    with tempfile.TemporaryDirectory(prefix="omega-fr9-oracle-") as scratch:
        scratch = Path(scratch)
        original_compilers = {}

        def original_compiler(identity):
            if identity == "current":
                return compiler
            baseline = options.bridge_baseline_compiler.resolve()
            if not baseline.is_file() or digest(baseline.read_bytes()) != PINNED_COMPILER_SHA256:
                raise Failure("original conductor requires exact independently pinned OM1 compiler " + PINNED_COMPILER_SHA256)
            if identity == "pinned":
                return str(baseline)
            if identity not in original_compilers:
                revision, sha = HISTORICAL_COMPILERS[identity]
                fetched = run(["git", "show", revision + ":seed/omc/omc.om"], b"", ROOT, options.timeout)
                if fetched[0] or digest(fetched[1]) != sha:
                    raise Failure("historical compiler source identity mismatch: " + identity)
                built = run([str(baseline)], fetched[1], scratch, options.timeout)
                if built[0] or built[2] or not built[1]:
                    raise Failure("historical compiler bootstrap failed: " + identity + ": " + str(result_details(built)))
                binary = scratch / ("original-" + sha)
                binary.write_bytes(built[1])
                binary.chmod(0o700)
                original_compilers[identity] = str(binary)
            return original_compilers[identity]

        def generation_compiler():
            nonlocal generation_variant
            if generation_variant is None:
                source = compiler_source
                substitutions = []
                for old, new in (
                    (b"matconst 15 36028797018963967\n", b"matconst 15 3\n"),
                    (b"xhcmp 10 36028797018963967\n", b"xhcmp 10 3\n"),
                ):
                    if source.count(old) != 1:
                        raise Failure("generation-ceiling whitebox setup no longer matches exact current emitter: " + repr(old))
                    source = source.replace(old, new)
                    substitutions.append({"old_hex": old.hex(), "new_hex": new.hex(), "count": 1})
                built = run([compiler], source, scratch, options.timeout)
                if built[0] or built[2] or not built[1]:
                    raise Failure("generation-ceiling compiler build failed: " + str(result_details(built)))
                binary = scratch / "generation-limit-compiler"
                binary.write_bytes(built[1])
                binary.chmod(0o700)
                generation_variant = str(binary), {
                    "authority_path": "seed/host/tests/genlimit.om",
                    "authority_sha256": "ea4f544b49878f5532f75b0ac7e8fa42e6926d5b57ea002e748679922859ad48",
                    "authority_quote": "White-box boundary fixture: compiler's generation ceiling replaced with 3 for this test only.",
                    "original_compiler_source_sha256": digest(compiler_source),
                    "variant_source_sha256": digest(source), "variant_compiler_sha256": digest(built[1]),
                    "substitutions": substitutions, "scope": "fixture-only, both target generation ceilings; production source unchanged"}
            return generation_variant

        def paired_frontend_compiler():
            nonlocal frontend_compiler, frontend_evidence
            if frontend_compiler is None:
                binary = scratch / "x86-hosted-compiler"
                if options.x86_compiler:
                    image = options.x86_compiler.read_bytes()
                    frontend_evidence = {"provenance": "explicit external --x86-compiler",
                                         "path": str(options.x86_compiler.resolve())}
                else:
                    source = compiler_source
                    built = run([compiler], TARGET_PREFIX + source, scratch, options.timeout)
                    if built[0] or built[2] or not built[1]:
                        raise Failure(f"x86-hosted compiler bootstrap failed: {result_details(built)}")
                    disassemble(built[1], source, objdump, llvm, scratch, options.timeout)
                    image = built[1]
                    frontend_evidence = {"provenance": "exact current seeded source compiled by selected native compiler",
                                         "source": "seed/omc/omc.om", "source_sha256": digest(source)}
                binary.write_bytes(image)
                binary.chmod(0o700)
                frontend_compiler = str(binary)
                frontend_evidence.update(x86_compiler_sha256=digest(image),
                                         native_compiler_sha256=digest(Path(compiler).read_bytes()))
            return frontend_compiler

        for index, case in enumerate(expanded_cases(files, record)):
            frontend_mode = bool(MODE.match(case.source))
            key = digest(case.source)
            driver = case.driver or HOST_DRIVERS.get(key)
            if case.name.startswith("oracle-positional-"):
                driver = "positional-view"
            target, adaptation = (case.source, None) if case.bridge else target_source(case, driver)
            if options.inventory:
                words = host_words(case.source)
                kind = "INVENTORY_HOST" if case.bridge else ("INVENTORY_UNIT" if not MAIN.search(case.source) else (
                    "INVENTORY_HOST" if words else "INVENTORY_PROGRAM"))
                record(kind, case.name, "reviewed host driver" if driver else (
                    "unreviewed host driver missing" if words else "compile/runtime differential input"),
                    host_words=words, host_driver=driver, source_sha256=key,
                    target_adaptation=adaptation, expected_compile=case.expected_compile,
                    operation=case.operation, evidence=case.evidence, blocker=case.blocker)
                continue
            cwd = scratch / str(index)
            cwd.mkdir()
            diagnostics = {}
            child_timeout = case.deadline if case.deadline is not None else options.timeout
            try:
                if case.bridge:
                    replay = exercise_bridge(case.bridge, compiler, qemu, options, scratch, bridge_state)
                    prefix = case.bridge.get("required_line_prefix")
                    if prefix and not any(line.startswith(prefix) for line in replay["stdout"].splitlines()):
                        raise Failure("original C driver did not report required exact case: " + prefix)
                    record("PASS", case.name, "original C-boundary/gate obligation, actual reviewed driver",
                           source_sha256=key, evidence=case.evidence, bridge_result=replay,
                           control_kind=case.bridge["operation"],
                           evidence_kind=case.evidence.get("evidence_kind", "original explicit oracle"),
                           contract_status=case.evidence.get("contract_status", "original fixed obligation"))
                    continue
                case_compiler = compiler
                if driver == "genlimit" and case.operation != "emit":
                    case_compiler, case.evidence["generation_ceiling_whitebox"] = generation_compiler()
                    case.stdout_oracle = "sha:" + digest(literal_bytes(case.source)[0])
                compilekey = key, digest(target), frontend_mode, child_timeout, case_compiler
                if compilekey not in cache:
                    default = run([case_compiler], case.source, cwd, child_timeout)
                    if frontend_mode:
                        x86 = run([qemu, "-0", "./omc", paired_frontend_compiler()],
                                  case.source, cwd, child_timeout)
                    else:
                        x86 = run([case_compiler], TARGET_PREFIX + target, cwd, child_timeout)
                    cache[compilekey] = default, x86
                    if len(cache) > 32:
                        cache.popitem(last=False)
                default, x86 = cache[compilekey]
                diagnostics = {"default_compile": result_details(default), "x86_compile": result_details(x86)}
                diagnostic_contract = None
                if not frontend_mode and target == case.source:
                    diagnostic_contract = target_diagnostic_contract(case.source, default, x86)
                    if diagnostic_contract:
                        case.evidence["target_diagnostic_contract"] = diagnostic_contract
                if frontend_mode:
                    case.evidence.update(frontend_compiler=frontend_evidence,
                                         comparison="crossHOST native-AArch64 versus EMULATED x86 compiler, exact original mode bytes")
                if case.operation == "emit":
                    assert_oracle(default[1], case.stdout_oracle, "compiler stdout")
                    assert_oracle(default[2], case.stderr_oracle, "compiler stderr")
                if default[0]:
                    if case.expected_compile == "accept":
                        raise Failure(f"fixture required acceptance but default compiler refused with {default[0]}")
                    if case.expected_compile and default[0] != int(case.expected_compile):
                        raise Failure(f"fixture default refusal code {default[0]} differs from explicit oracle {case.expected_compile}")
                    if diagnostic_contract and diagnostic_contract["x86_execution"]:
                        expected = diagnostic_contract["x86_execution"]
                        mnemonics, instructions, carveouts = disassemble(
                            x86[1], target, objdump, llvm, cwd, child_timeout)
                        if expected == "recorded-bare-caller":
                            observed = exercise_bare_target(case, x86[1], qemu, cwd, child_timeout)
                        else:
                            binary = cwd / "target-limit-control"
                            binary.write_bytes(x86[1])
                            binary.chmod(0o700)
                            observed = run([qemu, str(binary)], b"", cwd, child_timeout)
                            if observed != (expected["exit"], expected["stdout"].encode(), expected["stderr"].encode()):
                                raise Failure("target-specific limit execution oracle differs: " + str(result_details(observed)))
                            observed = result_details(observed)
                        templates.update(mnemonics)
                        record("PASS", case.name, "exact per-target limit contract and independent x86 execution oracle",
                               source_sha256=key, evidence=case.evidence,
                               default_compile=result_details(default), x86_compile=result_details(x86),
                               x86_execution=observed, instructions=len(instructions))
                        continue
                    if not diagnostic_contract and refusal(default, 0) != refusal(x86, 0 if frontend_mode else 1):
                        raise Failure("compile refusal differs after exact source-coordinate translation")
                    record("PASS", case.name, "compile-refusal differential + explicit conductor oracle",
                           compile_exit=default[0], source_sha256=key,
                           target_adaptation=adaptation, evidence=case.evidence,
                           comparison="crossHOST exact frontend mode" if frontend_mode else "crossTARGET")
                    continue
                if case.expected_compile not in (None, "accept"):
                    raise Failure("refusal control was incorrectly accepted by default compiler")
                if x86[0]:
                    record("MISSING_IMPLEMENTATION", case.name, "default accepts; x86 compiler refuses",
                           compile_exit=x86[0], diagnostic=x86[2].decode("utf-8", "replace"),
                           source_sha256=key, evidence=case.evidence)
                    continue
                translated_stderr = normalized_diagnostics(x86[2], 0 if frontend_mode else 1)
                if case.evidence.get("compiler_stderr_comparison") == "source-site-records":
                    case.evidence["raw_compiler_stderr"] = {"native_hex": default[2].hex(), "x86_hex": x86[2].hex()}
                    case.evidence["site_comparison"] = source_site_diagnostics(
                        default[2], translated_stderr, case.evidence["site_source_lines"])
                elif not diagnostic_contract and default[2] != translated_stderr:
                    raise Failure("successful compiler diagnostics differ after exact source-coordinate translation")
                if frontend_mode:
                    if default[1] != x86[1]:
                        raise Failure("exact-mode output differs between native and x86-hosted compiler")
                    if case.operation == "same":
                        left = run([compiler], case.same_source, cwd, options.timeout)
                        right = run([qemu, "-0", "./omc", paired_frontend_compiler()],
                                    case.same_source, cwd, options.timeout)
                        if left[0] or right[0] or default[1] != left[1] or x86[1] != right[1]:
                            raise Failure("exact frontend same-artifact oracle differs")
                    record("PASS", case.name, "exact-mode frontend crossHOST differential, original conductor oracles preserved",
                           source_sha256=key, evidence=case.evidence, compile_exit=default[0],
                           stdout_sha256=digest(default[1]), comparison="crossHOST EMULATED, not backend crossTARGET")
                    continue
                if MAIN.search(case.source) and (
                        default[1][:6] != b"\x7fELF\x02\x01" or
                        struct.unpack_from("<H", default[1], 18)[0] != 183):
                    raise Failure("default no longer emits AArch64 ELF program")
                decode_key = digest(x86[1]), digest(target)
                decoded = conductor_decode_cache.get(decode_key) if case.operation == "conductor" else None
                if decoded is None:
                    decoded = disassemble(x86[1], target, objdump, llvm, cwd, child_timeout)
                    if case.operation == "conductor":
                        # Frozen manifests repeatedly execute the same conductor.
                        # Cache only an exact image AND source, never a verdict.
                        conductor_decode_cache[decode_key] = decoded
                        if len(conductor_decode_cache) > 4:
                            conductor_decode_cache.popitem(last=False)
                mnemonics, instructions, carveouts = decoded
                templates.update(mnemonics)
                if case.operation == "conductor":
                    observations = []
                    required = case.evidence.get("required_mutant_replays", [case.evidence])
                    for replay_number, requirement in enumerate(required):
                        manifest = requirement["manifest"]
                        identity = requirement["child_compiler_identity"]
                        cachekey = manifest, identity, digest(json.dumps(requirement["resources"], sort_keys=True).encode())
                        if cachekey not in conductor_cache:
                            try:
                                replaydir = cwd / ("replay-" + str(replay_number))
                                replaydir.mkdir()
                                replay_case = replace(case, evidence=requirement)
                                child_compiler = original_compiler(identity)
                                conductor_cache[cachekey] = exercise_conductor(
                                    replay_case, default[1], x86[1], child_compiler, qemu, replaydir, options.timeout)
                                # File the actual build/control/kill transcript
                                # once, independently of per-row verdicts. A
                                # successful mutation assertion is not merely
                                # an expected output attached to healthy code.
                                print(json.dumps({"kind": "original-conductor-evidence",
                                                  "manifest": manifest, "child_compiler_identity": identity,
                                                  "child_compiler_sha256": digest(Path(child_compiler).read_bytes()),
                                                  "resources": requirement["resources"],
                                                  "replay": conductor_cache[cachekey]}, sort_keys=True))
                            except (OSError, ValueError, Failure) as error:
                                conductor_cache[cachekey] = {"error": str(error)}
                        replay = conductor_cache[cachekey]
                        if "error" in replay:
                            raise Failure(replay["error"])
                        overall = requirement.get("expected_overall_exit")
                        if overall is not None and replay["exit"] != overall:
                            raise Failure(f"original conductor expected whole-manifest exit {overall}, observed {replay['exit']}; original_log={replay['stdout']}")
                        observed = replay["verdicts"].get(requirement["original_case_name"])
                        expected = requirement["expected_original_conductor"]
                        historical_failure = requirement.get("historical_failure")
                        if historical_failure:
                            if observed is None or observed["status"] != "FAIL" or observed["line"] != historical_failure["exact_verdict"]:
                                raise Failure("historical failure replay changed: " + str(observed))
                        elif observed is None or observed["status"] != expected:
                            raise Failure(f"original conductor oracle: expected {expected}, observed {observed}; log_sha256={replay['stdout_sha256']}; original_log={replay['stdout']}")
                        reason = requirement.get("expected_failure_reason")
                        if reason and reason not in observed["line"]:
                            raise Failure("original negative control failed for the wrong reason: " + observed["line"])
                        if requirement.get("required_mutant_replays") is not None:
                            raise Failure("recursive mutant replay plan")
                        if "mutant_specification" in case.evidence:
                            bad_controls = {name: verdict for name, verdict in replay["verdicts"].items()
                                            if ("ctl" in name) and verdict["status"] != "PASS"}
                            if bad_controls or replay["directive_verdicts"]:
                                raise Failure(f"mutant replay build/control failed: {bad_controls}; {replay['directive_verdicts']}")
                        observations.append({"manifest": manifest, "child_compiler_identity": identity,
                                             "expected": expected, "observed": observed,
                                             "historical_failure": historical_failure,
                                             "conductor_exit": replay["exit"],
                                             "conductor_stdout_sha256": replay["stdout_sha256"],
                                             "filesystem_sha256": replay["filesystem_sha256"],
                                             "directive_verdicts": replay["directive_verdicts"]})
                    historical_failures = [item for item in observations if item["historical_failure"]]
                    record("PASS", case.name, ("historical FAIL reproduced exactly; not current acceptance or a successful mutant kill"
                           if historical_failures else "original conductor oracle proved on native/x86 hosts with explicit child compiler identity"),
                           source_sha256=key, evidence=case.evidence, replays=observations,
                           control_kind=case.evidence["control_kind"], oracle_instructions=len(instructions),
                           child_compiler_architecture="native AArch64 in both conductor runs")
                    continue
                if case.operation == "structural":
                    minimum = case.evidence["structural"]["minimum_calls"]
                    calls = sum(count for mnemonic, count in mnemonics.items() if mnemonic in ("call", "callq"))
                    if calls < minimum:
                        raise Failure(f"explicit structural call obligation: decoded {calls}, requires at least {minimum}")
                if case.operation == "same":
                    left = run([compiler], case.same_source, cwd, options.timeout)
                    right = run([compiler], TARGET_PREFIX + case.same_source, cwd, options.timeout)
                    if left[0] or right[0] or default[1] != left[1] or x86[1] != right[1]:
                        diagnostics.update(default_same=result_details(left), x86_same=result_details(right))
                        raise Failure("same oracle failed: each architecture must compile both sources to byte-identical artifacts")
                    record("PASS", case.name, "same-artifact oracle independently satisfied on both architectures + decoded x86",
                           source_sha256=key, evidence=case.evidence, oracle_instructions=len(instructions))
                    continue
                if case.operation == "emit":
                    record("PASS", case.name, "explicit compiler exit/stdout/stderr oracle + x86 differential/decode",
                           source_sha256=key, evidence=case.evidence, oracle_instructions=len(instructions))
                    continue
                if case.blocker:
                    record("BLOCKED" if case.blocker.startswith("BLOCKED_ON ") else "NOT_RUN",
                           case.name, case.blocker, source_sha256=key,
                           evidence=case.evidence, oracle_instructions=len(instructions),
                           bridge=str(options.bridge) if options.bridge else None)
                    continue
                if not MAIN.search(case.source):
                    callers = exercise_bare(case, default[1], x86[1], qemu, cwd, options.timeout)
                    if callers:
                        record("PASS", case.name, "original bare artifact decoded + reviewed full-width private-ABI callers exercised",
                               source_sha256=key, evidence=case.evidence, callers=callers,
                               oracle_instructions=len(instructions), data_carveouts=carveouts)
                        continue
                    record("NOT_RUN", case.name, f"entryless source {key} has no reviewed private-ABI caller for its exported/last entry",
                           source_sha256=key, evidence=case.evidence,
                           oracle_instructions=len(instructions), data_carveouts=carveouts)
                    continue
                words = host_words(case.source)
                if words and not driver:
                    record("NOT_RUN", case.name, f"source {key} requires unreviewed effects: {', '.join(words)}",
                           source_sha256=key, evidence=case.evidence, host_words=words,
                           oracle_instructions=len(instructions), data_carveouts=carveouts)
                    continue
                native, emulated = cwd / "native", cwd / "emulated"
                native.mkdir(); emulated.mkdir()
                basename = driver if driver and driver != "positional-view" else "program"
                for directory, image in ((native, default[1]), (emulated, x86[1])):
                    binary = directory / basename
                    binary.write_bytes(image); binary.chmod(0o700)
                args = case.args
                if driver == "args" and not args:
                    args = ("", "two words", "π")
                elif driver == "memcheck":
                    args = (compiler,)
                elif driver == "process":
                    if helper_image is None:
                        helper = run([compiler], (ROOT / "seed/host/tests/args.om").read_bytes(), cwd, options.timeout)
                        if helper[0]:
                            raise Failure("native exec-child helper compilation refused")
                        helper_image = helper[1]
                    # execve intentionally enters a native host ELF in both runs.
                    # qemu-user is not assumed registered as a system binfmt.
                    for directory in (native, emulated):
                        child = directory / "exec-child"
                        child.write_bytes(helper_image); child.chmod(0o700)
                elif driver == "effect-scratch" and "native_child" in case.evidence:
                    spec = case.evidence["native_child"]
                    child_source = contained(spec["source"]).read_bytes()
                    if digest(child_source) != spec["source_sha256"]:
                        raise Failure("reviewed native child source changed: " + spec["source"])
                    built = run([compiler], child_source, cwd, options.timeout)
                    if built[0] or built[2]:
                        raise Failure(f"native child compilation failed: {result_details(built)}")
                    case.evidence["native_child_sha256"] = digest(built[1])
                    # Both arms exec the same native AArch64 child; only the parent changes architecture.
                    for directory in (native, emulated):
                        child = directory / "exec-child"
                        child.write_bytes(built[1]); child.chmod(0o700)
                prepared = [prepare_case(ROOT, directory, driver, compiler, case=case.evidence)
                            for directory in (native, emulated)]
                if prepared[0] != prepared[1]:
                    raise Failure("reviewed prepare_case produced architecture-dependent argv")
                if prepared[0]:
                    args = tuple(prepared[0])
                if "stdin_compiled_source" in case.evidence:
                    source_path = case.evidence["stdin_compiled_source"]
                    stdin_source = contained(source_path).read_bytes()
                    if digest(stdin_source) != case.evidence["stdin_source_sha256"]:
                        raise Failure("reviewed scanner input source changed: " + source_path)
                    compiled_input = run([compiler], stdin_source, cwd, options.timeout)
                    if compiled_input[0] or compiled_input[2]:
                        raise Failure(f"scanner input compilation failed: {result_details(compiled_input)}")
                    case.evidence["stdin_original_artifact_sha256"] = digest(compiled_input[1])
                    case.stdin = select_a64_entry(compiled_input[1], case.evidence["stdin_export_index"])
                    case.evidence["stdin_selected_entry_artifact_sha256"] = digest(case.stdin)
                case.evidence["runtime_args"] = list(args)
                case.evidence["runtime_stdin_sha256"] = digest(case.stdin)
                expected = case.expected_exit
                if expected is None and driver:
                    expected = 103 if driver in ("environment_trap", "stat_short", "links_short") else 0
                # Both argv[0] strings and initial environments are byte-identical;
                # filesystem mutations cannot leak between architectures.
                argv0 = "./" + basename
                cpu_seconds = 1 if driver == "cpu-limit" else EFFECT_CPU_SECONDS if driver == "effect-scratch" else None
                nofile = case.evidence.get("nofile", 256) if driver == "effect-scratch" else 256
                arun = run([argv0, *args], case.stdin, native, child_timeout, cpu_seconds=cpu_seconds, nofile=nofile)
                xrun = run([qemu, "-0", argv0, argv0, *args], case.stdin, emulated, child_timeout, cpu_seconds=cpu_seconds, nofile=nofile)
                diagnostics.update(default_run=result_details(arun), x86_run=result_details(xrun))
                if "required_verdict" in case.evidence:
                    required = ("VERDICT " + case.evidence["required_verdict"]).encode()
                    if required not in arun[1].splitlines() or required not in xrun[1].splitlines():
                        raise Failure(f"original scanner did not prove required verdict {required!r}")
                native_files = filesystem_snapshot(native, {basename, "exec-child"})
                x86_files = filesystem_snapshot(emulated, {basename, "exec-child"})
                diagnostics.update(default_filesystem=native_files, x86_filesystem=x86_files)
                if native_files != x86_files:
                    raise Failure("persistent scratch filesystem effects differ (type/mode/contents/symlink target)")
                if arun[0] < 0 or xrun[0] < 0:
                    if not ((driver == "supervisor-self-signal" and case.operation == "differential" and
                             case.evidence.get("required_conductor_verdict") == "INVALID signal" and
                             arun[0] == xrun[0] == case.expected_exit == -9) or
                            (driver == "cpu-limit" and arun[0] == xrun[0] == case.expected_exit == -signal.SIGXCPU)):
                        raise Failure(f"runtime signal: default={arun[0]} x86={xrun[0]}")
                if arun != xrun:
                    artifact = ioctl_emulator_artifact(case, driver, arun, xrun, qemu, argv0,
                                                      args, emulated, options.timeout)
                    if artifact:
                        record("NOT_RUN", case.name, "EMULATOR_ARTIFACT: exact invalid-fd ioctl returned QEMU ENOTTY instead of native EBADF",
                               source_sha256=key, evidence=case.evidence, emulator_artifact=artifact,
                               diagnostics=diagnostics, oracle_instructions=len(instructions))
                        continue
                    raise Failure(f"runtime differential: default={arun[0]} x86={xrun[0]}, stdout={arun[1] == xrun[1]}, stderr={arun[2] == xrun[2]}")
                if expected is not None and arun[0] != expected:
                    raise Failure(f"fixture oracle exit {expected}, observed {arun[0]}")
                assert_oracle(arun[1], case.stdout_oracle, "runtime stdout", different=case.operation == "kill")
                assert_oracle(arun[2], case.stderr_oracle, "runtime stderr")
                reason = "target-adapted ABI differential" if adaptation else "identical-source native-AArch64/qemu-x86 differential"
                if driver == "supervisor-self-signal":
                    reason = "supervisor negative-input control: both children observed SIGKILL, never an ordinary mutant kill"
                record("PASS", case.name, reason + " + every code byte decoded", run_exit=arun[0],
                       stdout_sha256=digest(arun[1]), source_sha256=key, x86_source_sha256=digest(target),
                       x86_sha256=digest(x86[1]), oracle_instructions=len(instructions),
                       data_carveouts=carveouts, host_driver=driver, target_adaptation=adaptation,
                       exec_child_architecture="native AArch64" if driver == "process" else None,
                       generation_ceiling_whitebox=driver == "genlimit", evidence=case.evidence,
                       conductor_operation=case.operation,
                       control_kind=("generation_ceiling_whitebox" if driver == "genlimit" else
                                     "supervisor_negative_input" if driver in ("supervisor-self-signal", "supervisor-self-sleep") else None),
                       filesystem_sha256=digest(json.dumps(native_files, sort_keys=True).encode()),
                       filesystem_entries=len(native_files))
                if case.name == "seed/rung4/tests/t_exit.om":
                    expected = int((ROOT / "seed/rung4/tests/t_exit.want").read_text())
                    if arun != (expected, b"", b""):
                        raise Failure("exit control disagrees with existing .want oracle")
                    controls = (case.source, x86[1], arun)
            except (OSError, ValueError, struct.error, Failure) as error:
                record("BLOCKED" if str(error).startswith("BLOCKED_ON ") else "FAIL", case.name, str(error), source_sha256=key,
                       evidence=case.evidence, diagnostics=diagnostics)
        if not options.inventory:
            if controls is None:
                record("FAIL", "negative-controls", "required t_exit.om did not qualify")
            else:
                source, image, baseline = controls
                cwd = scratch / "negative-controls"
                cwd.mkdir()
                try:
                    _, instructions, _ = disassemble(image, source, objdump, llvm, cwd, options.timeout)
                    bad = bytearray(image)
                    first = instructions[0][0]
                    bad[first:first + 2] = b"\x0f\x04"  # unassigned x86 opcode, not UD2 (which is valid).
                    try:
                        disassemble(bytes(bad), source, objdump, llvm, cwd, options.timeout)
                    except Failure:
                        record("PASS", "negative-invalid-encoding", "independent disassembler rejected deliberately invalid emitted opcode")
                    else:
                        raise Failure("invalid-opcode mutant survived disassembler")
                    candidates = [(offset, raw) for offset, raw, _ in instructions
                                  if len(raw) == 10 and raw[:2] == b"\x48\xb8" and raw[2:] == (42).to_bytes(8, "little")]
                    if len(candidates) != 1:
                        raise Failure("expected unique MOVABS RAX,42 in exit fixture for wrong-immediate control")
                    offset, raw = candidates[0]
                    wrong = bytearray(image)
                    wrong[offset + 2] = 43
                    disassemble(bytes(wrong), source, objdump, llvm, cwd, options.timeout)
                    mutant = cwd / "wrong-immediate"
                    mutant.write_bytes(wrong); mutant.chmod(0o700)
                    observed = run([qemu, str(mutant)], b"", cwd, options.timeout)
                    if observed[0] < 0 or observed == baseline:
                        raise Failure("valid-but-wrong encoding mutant not caught by behavior differential")
                    record("PASS", "negative-valid-wrong-encoding", "disassembler accepts valid changed immediate; behavior oracle rejects it", expected_exit=baseline[0], observed_exit=observed[0])
                except (OSError, Failure) as error:
                    record("FAIL", "negative-controls", str(error))
    missing = sum(count for status, count in totals.items() if status not in ("PASS", "EXCLUDED", "INVENTORY_PROGRAM", "INVENTORY_HOST", "INVENTORY_UNIT"))
    print(json.dumps({
        "summary": dict(totals),
        "reasons": [{"status": status, "reason": reason, "count": count}
                    for (status, reason), count in sorted(reasons.items())],
        "oracle_mnemonic_counts": dict(sorted(templates.items())),
        "coverage_limits": [
            "Every emitted code byte is linearly decoded, including unreachable functions; direct branches must target decoded instruction boundaries",
            "Only source-matched JMP-protected literal/padding spans are carved out; ambiguous/missing data maps fail closed",
            "Mnemonic counts are not exhaustive encoding-template, register, width, displacement or trap coverage",
            "The default baseline may share frontend defects; invalid and valid-but-wrong encoding controls challenge separate oracles",
            "Frozen C2 retains exact historical inputs; original frontend modes compare unchanged bytes through native/x86-hosted compilers, separately from cross-target code generation",
            "Reviewed content-pinned host drivers run in separate architecture-local directories and compare persistent filesystem effects; other host effects remain NOT_RUN",
            "Bare callers execute original unit bytes through architecture-specific ABI wrappers and observe full-width returns/arrays; missing callers remain individually NOT_RUN",
            "process exec enters a native AArch64 helper in both runs, not an x86 child; generation retirement uses the original fixture's exact ceiling3 compiler variant on both targets",
            "psync invokes syncfs on its scratch-file descriptor and therefore flushes the scratch filesystem; no service process is controlled",
            "No hardware qualification"],
        "verdict": "INVENTORY_ONLY" if options.inventory else ("FAIL" if missing else "PASS")}))
    return 0 if options.inventory else int(bool(missing))


if __name__ == "__main__":
    raise SystemExit(main())
