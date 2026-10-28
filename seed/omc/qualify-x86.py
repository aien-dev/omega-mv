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
Historical evidence is inventoried but never replayed. .cases and frozen C2
manifest rows add composed, packed and cyclic sources and their stdin/argv. Raw
files are also checked, so a manifest omission cannot quietly shrink the corpus.
Pinned/mutant manifests are differential inputs, not old semantic authority. cc/subst
conductor arms are counted separately, never confused with candidate execution.
"""
from __future__ import annotations

import argparse
from collections import Counter, OrderedDict
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import resource
import shutil
import signal
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MAIN = re.compile(rb"(?m)^[ \t]*(?:export[ \t]+)?fn[ \t]+main(?:[ \t]|$)")
MODE = re.compile(rb"^(?:ir(?:[ \t\r\n]|$)|check(?:[ \t\r\n]|$)|next(?:[ \t\r\n]|$)|om2 fmt(?:[ \t\r\n]|$)|target(?:[ \t\r\n]|$))")
TEXT = re.compile(rb'(?m)^[ \t]*text[ \t]+\w+[ \t]+"((?:[^"\\]|\\.)*)"[^\n]*$')
UNSAFE = set("""
devctl devcall uvmctl mapnew mapdrop mapget mapset mapread mapwrite mapaddr maphold mapbind
efi socket socknew bind sockbind connect listen accept send recv sockname sockopt shutdown
fork exec execprog waitpid kill killpid clock realns monons sleep sleepns random cwd
chdir fchdir envcnt envlen envread open openat fopen fclose fseek fsync fdsync fchmod
chmod chown umask unlink rename renames link symlnk mkdir makedir rmdir stat lstat
fstat getdnts faccess readlnk memfd ftrunc truncate pread pwrite ioctl flock syncfs
setpgid setsid
""".split())
INSTRUCTION = re.compile(r"^\s*([0-9a-fA-F]+):\s+((?:[0-9a-fA-F]{2}[ \t]+)+)(.*?)\s*$")
DIAG = re.compile(rb"line ([0-9]+): error ([0-9]+)")

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
process 0c1292a905cb1263a733e26f2e026a2915089d6d6959d16b617c05f446452964
memcheck 652a4bc9455251bb51d01a75fc5be0ad008ae5e022e4835e43959f073c82413d
device 375898c6ac071af5ab50bb41fce6aa93ee5796302916cac65df70831c52d3d67
psync 8355680fe35976dc5fd3621c3dc9b2ff8f6cc8b210ec90915d2044a4de133473
"""
HOST_DRIVERS = {sha: name for name, sha in (row.split() for row in HOST_PINS.splitlines() if row)}


@dataclass
class Case:
    name: str
    source: bytes
    stdin: bytes = b""
    args: tuple[str, ...] = ()
    expected_exit: int | None = None
    expected_compile: str | None = None


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


def exclusion(source):
    if MODE.match(source):
        return "mode-specific IR/check/next/formatter/explicit-target input"
    # Entryless sources still undergo compile/refusal differential. Only their
    # undefined runtime entry is excluded later; known unit conductors add mains.
    return None


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
            "text ro \"abcd\"\nglobal rw uchar 4\n"
            f"fn probe b {qualifier} uchar 4 to long\n"
            f"ret {word} neg int 1 b 0 4 0\nend\nfn main to int\n"
            f"if ne probe {'rw' if word == 'pread' else 'ro'} neg long 9\n"
            "ret 1\nend\nret 0\nend\n").encode()
        yield Case(f"oracle-positional-{word}-{qualifier}", source,
                   expected_exit=0 if accepted else None,
                   expected_compile="accept" if accepted else "20")


def expanded_cases(files, record):
    for case in cases(files, record):
        yield case
        driver = HOST_DRIVERS.get(digest(case.source))
        if driver == "network" and not case.args:
            yield Case(case.name + ":gate-host-tcp-loopback", case.source, args=("tcp",), expected_exit=0)
        elif driver == "netdata" and not case.args:
            for argument in ("1", "2", "3"):
                yield Case(case.name + ":gate-host-bounds-" + argument, case.source,
                           args=(argument,), expected_exit=103)
        elif driver == "memcheck":
            # The runtime conductor invokes the native compiler; replay its 36
            # literal source inputs directly as well, including x86 refusals.
            for n, source in enumerate(s for s in literal_bytes(case.source) if b"fn " in s):
                code = "9"
                if n in (0, 4, 5, 10, 12, 15, 17, 21, 28, 33):
                    code = "7"
                elif n in (3, 11, 18, 19):
                    code = "20"
                yield Case(case.name + f":embedded-refusal-{n}", source, expected_compile=code)
    yield from positional_view_cases()


def cases(files, record):
    for path in files:
        rel = path.relative_to(ROOT).as_posix()
        if "evidence" in path.relative_to(ROOT).parts:
            record("EXCLUDED", rel, "historical evidence, never replayed")
            continue
        if "selftest" in path.relative_to(ROOT).parts:
            record("EXCLUDED", rel, "qualification conductor selftest, not language fixture")
            continue
        if path.suffix == ".tsv":
            try:
                yield from unit_cases(path)
            except (OSError, ValueError, Failure) as error:
                record("MISSING_INPUT", rel, str(error))
            continue
        if path.suffix in (".om", ".omegese"):
            if path.parent == ROOT / "seed/qualification/om2" or any(
                    part in ("selftest", "oracle") for part in path.relative_to(ROOT).parts):
                record("EXCLUDED", rel, "qualification conductor/oracle selftest, not language fixture")
                continue
            source = path.read_bytes()
            yield Case(rel, source)
            if rel in ("seed/rung4/tests/echo.om", "seed/rung4/tests/bytecopy.om"):
                # gate4 section 6 uses a nonempty binary input; retain its length
                # with deterministic varying binary input, not host randomness.
                yield Case(rel + ":gate4-nonempty", source, stdin_bytes("seq:50000"))
            continue
        alternate = False
        subst = None
        for number, line in enumerate(path.read_text().splitlines(), 1):
            fields = line.split()
            if not fields or fields[0].startswith("#"):
                continue
            name = f"{rel}:{number}:{fields[0]}"
            if fields[0] == "cc":
                alternate = fields[1:] != ["pinned"]
                record("EXCLUDED", name, "conductor compiler-selection directive")
                continue
            if fields[0] == "deadline":
                record("EXCLUDED", name, "conductor deadline; checker uses explicit --timeout")
                continue
            if fields[0] == "subst":
                subst = fields[1:]
                record("EXCLUDED", name, "conductor source substitution directive")
                continue
            if len(fields) < 2:
                record("INVALID", name, "malformed conductor row")
                continue
            mode = fields[1]
            if mode == "notrun":
                record("NOT_RUN", name, " ".join(fields[2:]))
                continue
            if alternate or subst is not None:
                record("EXCLUDED", name, "compiler-mutant/substitution conductor arm; raw source remains inventoried")
                subst = None
                continue
            try:
                if mode in ("run", "kill") and len(fields) >= 7:
                    yield Case(name, source_bytes(fields[2]), stdin_bytes(fields[3]), tuple(fields[7:]))
                elif mode in ("emit", "refuse"):
                    yield Case(name, source_bytes(fields[2]))
                elif mode == "same" and len(fields) == 4:
                    yield Case(name + ":left", source_bytes(fields[2]))
                    yield Case(name + ":right", source_bytes(fields[3]))
                else:
                    record("INVALID", name, "unknown/malformed conductor operation " + mode)
            except (OSError, ValueError, Failure) as error:
                record("MISSING_INPUT", name, str(error))


def limits():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_FSIZE, (64 * 1024 * 1024, 64 * 1024 * 1024))
    resource.setrlimit(resource.RLIMIT_NOFILE, (256, 256))


def run(command, data, cwd, timeout):
    # Regular-file stdin matches seekable-input fixtures; no shell interpolation.
    with tempfile.TemporaryFile() as inp, tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err:
        inp.write(data)
        inp.seek(0)
        process = subprocess.Popen(command, stdin=inp, stdout=out, stderr=err, cwd=cwd,
                                   env={"LC_ALL": "C", "PATH": os.defpath, "HOME": ".", "OMTEST": "hello"},
                                   start_new_session=True, preexec_fn=limits)
        try:
            process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            raise Failure("child timeout: " + str(command[0]))
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
    literals = literal_bytes(source)
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
        wrapper = cwd / f"oracle-{n}.elf"
        wrapper.write_bytes(oracle_elf(image[start:end], base + start))
        options = ["-d", "-z", "--x86-asm-syntax=intel"] if llvm else ["-d", "-z", "-M", "intel", "--insn-width=16"]
        rc, output, error = run([objdump, *options, str(wrapper)], b"", cwd, timeout)
        if rc:
            raise Failure("disassembler refusal: " + error.decode("utf-8", "replace"))
        cursor = base + start
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
        if cursor != base + end:
            raise Failure(f"disassembler did not cover every code byte: stopped {cursor:x}, expected {base + end:x}")
    boundaries = {base + offset for offset, _, _ in instructions}
    for offset, raw, asm in instructions:
        branch = re.match(r"(?:j\w+|callq?)\s+(?:0x)?([0-9a-fA-F]+)(?:\s|$)", asm)
        if branch and int(branch[1], 16) not in boundaries:
            raise Failure("direct control-flow target is not a decoded instruction boundary: " + asm)
    return mnemonics, instructions, carveouts


def refusal(result, shift):
    rc, output, error = result
    match = DIAG.search(error)
    if rc <= 0 or output or not match or int(match[2]) != rc:
        raise Failure("not a clean compiler refusal")
    return rc, int(match[1]) - shift, DIAG.sub(b"line LINE: error CODE", error)

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--compiler", type=Path)
    parser.add_argument("--qemu", default="qemu-x86_64")
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
        print(json.dumps(dict(status=status, case=name, reason=reason, **details)), flush=True)

    files = fixture_files()
    print(json.dumps({"inventory_files": len(files), "roots": "tests; seed/*/tests; seed/omc/{om2,contracts}; seed/qualification; tools/exp445/arms/examples", "source_revision_contract": "6999fa9e6dae4847c155ef4eacaece74f491d3ca", "checker_sha256": digest(Path(__file__).read_bytes())}))
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
        print(json.dumps({"compiler": compiler, "compiler_sha256": digest(Path(compiler).read_bytes()), "qemu": qemu, "objdump": objdump, "platform": platform.platform(), "timeout_seconds": options.timeout}))
    cache = OrderedDict()
    controls = None
    helper_image = None
    with tempfile.TemporaryDirectory(prefix="omega-fr9-oracle-") as scratch:
        scratch = Path(scratch)
        for index, case in enumerate(expanded_cases(files, record)):
            excluded = exclusion(case.source)
            if excluded:
                record("EXCLUDED", case.name, excluded)
                continue
            key = digest(case.source)
            driver = HOST_DRIVERS.get(key)
            if case.name.startswith("oracle-positional-"):
                driver = "positional-view"
            target, adaptation = target_source(case, driver)
            if options.inventory:
                words = host_words(case.source)
                kind = "INVENTORY_UNIT" if not MAIN.search(case.source) else (
                    "INVENTORY_HOST" if words else "INVENTORY_PROGRAM")
                record(kind, case.name, "reviewed host driver" if driver else (
                    "unreviewed host driver missing" if words else "compile/runtime differential input"),
                    host_words=words, host_driver=driver, source_sha256=key,
                    target_adaptation=adaptation, expected_compile=case.expected_compile)
                continue
            cwd = scratch / str(index)
            cwd.mkdir()
            try:
                compilekey = key, digest(target)
                if compilekey not in cache:
                    default = run([compiler], case.source, cwd, options.timeout)
                    x86 = run([compiler], b"target x86linux\n" + target, cwd, options.timeout)
                    cache[compilekey] = default, x86
                    if len(cache) > 32:
                        cache.popitem(last=False)
                default, x86 = cache[compilekey]
                if default[0]:
                    if case.expected_compile == "accept":
                        raise Failure("fixture required acceptance but default compiler refused")
                    if case.expected_compile and default[0] != int(case.expected_compile):
                        raise Failure("fixture default refusal code differs from explicit oracle")
                    if refusal(default, 0) != refusal(x86, 1):
                        raise Failure("compile refusal differs (target directive line offset normalized)")
                    record("PASS", case.name, "compile-refusal differential", compile_exit=default[0],
                           source_sha256=key, target_adaptation=adaptation)
                    continue
                if case.expected_compile not in (None, "accept"):
                    raise Failure("refusal control was incorrectly accepted by default compiler")
                if x86[0]:
                    record("MISSING_IMPLEMENTATION", case.name, "default accepts; x86 compiler refuses", compile_exit=x86[0], diagnostic=x86[2].decode("utf-8", "replace"), source_sha256=key)
                    continue
                if MAIN.search(case.source) and (
                        default[1][:6] != b"\x7fELF\x02\x01" or
                        struct.unpack_from("<H", default[1], 18)[0] != 183):
                    raise Failure("default no longer emits AArch64 ELF program")
                if default[2] != x86[2]:
                    # Diagnostic line numbers can move by the selector line.
                    normalized = re.sub(rb"line ([0-9]+)", lambda m: b"line " + str(int(m[1]) - 1).encode(), x86[2])
                    if default[2] != normalized:
                        raise Failure("successful compiler diagnostics differ")
                mnemonics, instructions, carveouts = disassemble(x86[1], target, objdump, llvm, cwd, options.timeout)
                templates.update(mnemonics)
                if not MAIN.search(case.source):
                    record("NOT_RUN", case.name, "entryless unit compiled and fully decoded; runtime requires explicit composed main/private-ABI caller",
                           source_sha256=key, oracle_instructions=len(instructions), data_carveouts=carveouts)
                    continue
                words = host_words(case.source)
                if words and not driver:
                    record("NOT_RUN", case.name, "unreviewed host effects require dedicated driver",
                           host_words=words, oracle_instructions=len(instructions), data_carveouts=carveouts)
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
                expected = case.expected_exit
                if expected is None and driver and driver != "genlimit":
                    expected = 103 if driver in ("environment_trap", "stat_short", "links_short") else 0
                # Both argv[0] strings and initial environments are byte-identical;
                # filesystem mutations cannot leak between architectures.
                argv0 = "./" + basename
                arun = run([argv0, *args], case.stdin, native, options.timeout)
                xrun = run([qemu, "-0", argv0, argv0, *args], case.stdin, emulated, options.timeout)
                if arun[0] < 0 or xrun[0] < 0:
                    raise Failure(f"runtime signal: default={arun[0]} x86={xrun[0]}")
                if arun != xrun:
                    raise Failure(f"runtime differential: default={arun[0]} x86={xrun[0]}, stdout={arun[1] == xrun[1]}, stderr={arun[2] == xrun[2]}")
                if expected is not None and arun[0] != expected:
                    raise Failure(f"fixture oracle exit {expected}, observed {arun[0]}")
                reason = "target-adapted ABI differential" if adaptation else "identical-source native-AArch64/qemu-x86 differential"
                record("PASS", case.name, reason + " + every code byte decoded", run_exit=arun[0],
                       stdout_sha256=digest(arun[1]), source_sha256=key, x86_source_sha256=digest(target),
                       x86_sha256=digest(x86[1]), oracle_instructions=len(instructions),
                       data_carveouts=carveouts, host_driver=driver, target_adaptation=adaptation,
                       exec_child_architecture="native AArch64" if driver == "process" else None,
                       ordinary_not_generation_mutant=driver == "genlimit")
                if case.name == "seed/rung4/tests/t_exit.om":
                    expected = int((ROOT / "seed/rung4/tests/t_exit.want").read_text())
                    if arun != (expected, b"", b""):
                        raise Failure("exit control disagrees with existing .want oracle")
                    controls = (case.source, x86[1], arun)
            except (OSError, ValueError, struct.error, Failure) as error:
                record("FAIL", case.name, str(error))
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
            "Frozen C2 replays captured inline/fuzz inputs; current shell-only generators and alternate-compiler/subst conductor arms are not executed",
            "Reviewed content-pinned host drivers run in separate architecture-local directories; other host effects remain NOT_RUN",
            "Bare units are fully decoded but are runtime NOT_RUN without explicit main composition; wasm and SPIRV case tables supply composed full-width callers",
            "process exec enters a native AArch64 helper in both runs, not an x86 child; genlimit runs ordinary-compiler differential, not a mutated generation ceiling",
            "psync invokes syncfs on its scratch-file descriptor and therefore flushes the scratch filesystem; no service process is controlled",
            "No hardware qualification"],
        "verdict": "INVENTORY_ONLY" if options.inventory else ("FAIL" if missing else "PASS")}))
    return 0 if options.inventory else int(bool(missing))


if __name__ == "__main__":
    raise SystemExit(main())
