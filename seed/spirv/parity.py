#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verification oracle only: reads artifacts and runs the OpenCL CPU host.

It never generates IR, SPIR-V, or native code. Exit codes are fail-closed:
0 every row matches, 1 at least one executed row mismatches, 2 anything else
(missing artifact, malformed module, host/API failure, timeout, bad table).
A host or API failure is therefore never counted as a mismatch.

usage: parity.py DIR TABLE HOST PREFIX [--mutant-baseline]
"""
import os
import re
import struct
import subprocess
import sys

MASK = (1 << 64) - 1
HOST_TIMEOUT = 120
OUTPUT = re.compile(rb'\A([0-9]+) ([0-9]+)\n\Z')


class Refuse(Exception):
    """A setup, artifact, or API failure: exit 2, never a parity verdict."""


def words_of(blob, label):
    if len(blob) < 20 or len(blob) % 4:
        raise Refuse(f'{label}: SPIR-V length {len(blob)} is not a whole header plus words')
    words = struct.unpack(f'<{len(blob) // 4}I', blob)
    if words[0] != 0x07230203:
        raise Refuse(f'{label}: bad SPIR-V magic {words[0]:#010x}')
    if words[1] != 0x00010000:
        raise Refuse(f'{label}: SPIR-V version {words[1]:#010x}, expected 1.0')
    if words[4] != 0:
        raise Refuse(f'{label}: nonzero reserved schema word')
    return words


def literal_string(operands, label):
    raw = struct.pack(f'<{len(operands)}I', *operands)
    end = raw.find(b'\0')
    if end < 0:
        raise Refuse(f'{label}: unterminated literal string')
    return raw[:end].decode('ascii'), (end + 4) // 4


def inspect_module(path):
    """Check the module contract and return its kernel entry-point names."""
    label = os.path.basename(path)
    with open(path, 'rb') as handle:
        words = words_of(handle.read(), label)
    caps = set()
    memory = None
    entries = {}
    ints = {}
    voids = set()
    pointers = {}
    fntypes = {}
    functions = {}
    params = {}
    current = None
    at = 5
    while at < len(words):
        count, op = words[at] >> 16, words[at] & 0xffff
        if count == 0 or at + count > len(words):
            raise Refuse(f'{label}: malformed instruction at word {at}')
        ops = words[at + 1:at + count]
        if op == 17:
            caps.add(ops[0])
        elif op == 14:
            memory = tuple(ops)
        elif op == 15:
            name, used = literal_string(ops[2:], label)
            if name in entries:
                raise Refuse(f'{label}: duplicate entry point {name}')
            entries[name] = (ops[0], ops[1])
        elif op == 19:
            voids.add(ops[0])
        elif op == 21:
            ints[ops[0]] = ops[1]
        elif op == 32:
            pointers[ops[0]] = (ops[1], ops[2])
        elif op == 33:
            fntypes[ops[0]] = (ops[1], tuple(ops[2:]))
        elif op == 54:
            current = ops[1]
            functions[current] = ops[3]
            params[current] = []
        elif op == 55:
            if current is None:
                raise Refuse(f'{label}: parameter outside function')
            params[current].append(ops[0])
        elif op == 56:
            current = None
        at += count
    missing = {4: 'Addresses', 6: 'Kernel', 11: 'Int64'}.keys() - caps
    if missing:
        raise Refuse(f'{label}: missing capabilities {sorted(missing)}')
    if memory != (2, 2):
        raise Refuse(f'{label}: memory model {memory}, expected Physical64 OpenCL (2, 2)')
    if not entries:
        raise Refuse(f'{label}: no entry points')
    for name, (model, fn) in entries.items():
        if model != 6:
            raise Refuse(f'{label}: {name} execution model {model}, expected Kernel 6')
        if fn not in functions or functions[fn] not in fntypes:
            raise Refuse(f'{label}: {name} has no function definition')
        ret, kinds = fntypes[functions[fn]]
        if ret not in voids or len(kinds) != 2 or len(params[fn]) != 2:
            raise Refuse(f'{label}: {name} is not void(inputs, outputs)')
        for kind in kinds:
            storage, pointee = pointers.get(kind, (None, None))
            if storage != 5 or ints.get(pointee) != 64:
                raise Refuse(f'{label}: {name} parameter is not a CrossWorkgroup 64-bit pointer')
    return set(entries)


def canonical(typed):
    if typed == '-':
        return []
    out = []
    for arg in typed.split(','):
        kind, value = arg.split(':')
        if kind == 'bool':
            if value not in ('true', 'false'):
                raise Refuse(f'bad bool literal {value}')
            out.append(1 if value == 'true' else 0)
        else:
            out.append(int(value, 10) & MASK)
    if len(out) > 6:
        raise Refuse('more than six arguments')
    return out


def run_host(host, module, kernel, args, log):
    command = [host, module, kernel] + [str(arg) for arg in args]
    try:
        done = subprocess.run(command, capture_output=True, timeout=HOST_TIMEOUT)
    except subprocess.TimeoutExpired:
        raise Refuse(f'API failure: host timed out on {kernel} ({os.path.basename(module)})')
    except OSError as error:
        raise Refuse(f'API failure: cannot execute host: {error}')
    with open(log + '.out', 'wb') as handle:
        handle.write(done.stdout)
    with open(log + '.err', 'wb') as handle:
        handle.write(done.stderr)
    if done.returncode != 0:
        raise Refuse(f'API failure: host exited {done.returncode} on {kernel} '
                     f'({os.path.basename(module)}); see {log}.err')
    match = OUTPUT.match(done.stdout)
    if not match:
        raise Refuse(f'API failure: malformed host output for {kernel}; see {log}.out')
    value, status = int(match[1]), int(match[2])
    if value > MASK:
        raise Refuse(f'API failure: host value out of 64-bit range for {kernel}')
    return value, status


def main(argv):
    if len(argv) not in (5, 6) or (len(argv) == 6 and argv[5] != '--mutant-baseline'):
        raise Refuse(__doc__.strip().splitlines()[-1])
    directory, table, host, prefix = argv[1:5]
    mutant_mode = len(argv) == 6
    tag = 'mutant' if mutant_mode else 'spirv'
    rows = []
    with open(table, encoding='ascii') as handle:
        for line in handle.read().split('\n'):
            if not line or line.startswith('#'):
                continue
            fields = line.split('\t')
            if len(fields) != 5:
                raise Refuse(f'{table}: row has {len(fields)} fields')
            rows.append(fields)
    if not rows:
        raise Refuse(f'{table}: empty denominator')
    modules = {}
    covered = {}
    mismatches = []
    traps = 0
    lines = ['case\tunit\tfunction\tcanonical_args\tnative_status\tnative_bits\t'
             'spirv_status\tspirv_bits\tverdict']
    for index, (unit, name, ret, typed, status) in enumerate(rows, 1):
        if unit not in modules:
            file = 'mutant-flipped.spv' if mutant_mode and unit == 'mutant' else f'{unit}.spv'
            path = os.path.join(directory, file)
            modules[unit] = (path, inspect_module(path))
            covered[unit] = set()
        path, exports = modules[unit]
        if name not in exports:
            raise Refuse(f'{unit}: missing kernel {name}')
        covered[unit].add(name)
        args = canonical(typed)
        base = os.path.join(directory, f'{prefix}-{index}')
        with open(base + '.status', encoding='ascii') as handle:
            native_status = int(handle.read().strip())
        with open(base + '.out', 'rb') as handle:
            native = handle.read()
        if native_status != int(status):
            raise Refuse(f'{unit}.{name}: native status {native_status} expected {status}')
        if native_status:
            if not 101 <= native_status <= 114:
                raise Refuse(f'{unit}.{name}: native status {native_status} is not an Omega trap')
            if native:
                raise Refuse(f'{unit}.{name}: native trap wrote a value')
            native_bits = 0
            traps += 1
        else:
            if len(native) != 8:
                raise Refuse(f'{unit}.{name}: incomplete native result')
            native_bits = struct.unpack('<Q', native)[0]
        value, spirv_status = run_host(host, path, name, args, f'{base}.{tag}')
        same = value == native_bits and spirv_status == native_status
        lines.append(f'{index}\t{unit}\t{name}\t{",".join(map(str, args)) or "-"}\t'
                     f'{native_status}\t{native_bits}\t{spirv_status}\t{value}\t'
                     f'{"match" if same else "MISMATCH"}')
        if not same:
            mismatches.append(f'result mismatch at case {index} {unit}.{name}: native '
                              f'{native_bits}/{native_status} spirv {value}/{spirv_status}')
    for unit, (path, exports) in modules.items():
        if covered[unit] != exports:
            raise Refuse(f'{unit}: untested kernels {sorted(exports - covered[unit])}')
    parity = os.path.join(directory, f'{prefix}-{tag}-parity.tsv')
    with open(parity, 'w', encoding='ascii') as handle:
        handle.write('\n'.join(lines) + '\n')
    if mismatches:
        for message in mismatches:
            print(message)
        print(f'FAIL parity mismatches={len(mismatches)} of cases={len(rows)}; table {parity}')
        return 1
    print(f'PASS parity cases={len(rows)} units={len(modules)} traps={traps} '
          f'matches={len(rows)}; every kernel exercised; table {parity}')
    if 'mutant' in modules and not mutant_mode:
        good = run_host(host, os.path.join(directory, 'mutant.spv'), 'mutadd', [19, 7],
                        os.path.join(directory, 'mutant-probe.base'))
        inspect_module(os.path.join(directory, 'mutant-flipped.spv'))
        bad = run_host(host, os.path.join(directory, 'mutant-flipped.spv'), 'mutadd', [19, 7],
                       os.path.join(directory, 'mutant-probe.flipped'))
        if good != (26, 0) or bad != (12, 0):
            print(f'result mismatch in mutant probe: baseline {good} flipped {bad}, '
                  'expected (26, 0) and (12, 0)')
            return 1
        print('PASS add-to-sub IR-byte mutant executes successfully and computes 19-7=12')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main(sys.argv))
    except Refuse as error:
        print(f'ERROR {error}')
        sys.exit(2)
    except Exception as error:  # noqa: BLE001 - any oracle fault is exit 2, never 1
        print(f'ERROR oracle fault {type(error).__name__}: {error}')
        sys.exit(2)
