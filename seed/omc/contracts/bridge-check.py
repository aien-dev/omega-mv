#!/usr/bin/env python3
"""Verification-only ctypes caller; it does not implement Omega contracts."""
import ctypes
import sys

library = ctypes.CDLL(sys.argv[1])
check = library.omega2_check
check.argtypes = [ctypes.POINTER(ctypes.c_uint64), ctypes.c_int64]
check.restype = ctypes.c_uint32
cases = [(-1, 9, 42), (0, 0, 0), (1, 0, 1), (9, 0, 9),
         (10, 0, 10), (11, 10, 42), (20, 10, 42), (1, 0, 1)]
if len(sys.argv) == 3 and sys.argv[2] == "--wrong-expectation":
    cases[0] = (-1, 0, 42)
elif len(sys.argv) != 2:
    raise SystemExit("usage: bridge-check.py LIB [--wrong-expectation]")
failed = 0
for argument, expected_status, expected_value in cases:
    result = ctypes.c_uint64(42)
    status = check(ctypes.byref(result), argument)
    matched = (status, result.value) == (expected_status, expected_value)
    if not matched:
        failed += 1
    print("PASS" if matched else "FAIL", "argument", argument,
          "status", status, "result", result.value,
          "expected", expected_status, expected_value)
print("BRIDGE", "PASS" if failed == 0 else "FAIL", "cases", len(cases), "fail", failed)
raise SystemExit(0 if failed == 0 else 1)
