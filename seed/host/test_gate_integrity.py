#!/usr/bin/env python3
"""Offline orchestration controls; no GPU, service, or historical receipt writes."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class GateIntegrity(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="omega-gate-control-")
        self.root = Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)
        subprocess.run(["git", "init", "-q", str(self.root)], check=True)
        self.put("marker", "offline control\n")
        self.git("add", "marker")
        self.git("-c", "user.name=Offline Control", "-c", "user.email=control@example.invalid",
                 "commit", "-qm", "control")

    def git(self, *args):
        return subprocess.run(["git", *args], cwd=self.root, check=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def put(self, name, content, executable=False):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
        if executable:
            path.chmod(0o700)
        return path

    def copy(self, name):
        dest = self.root / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / name, dest)

    def invoke(self, *args):
        return subprocess.run(args, cwd=self.root, capture_output=True, text=True, timeout=60)

    def secret_gate(self):
        self.copy("tests/gates/run.sh")
        return self.invoke("bash", "tests/gates/run.sh", "zero-disk-secrets")

    def test_clean_secret_control(self):
        result = self.secret_gate()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("AIEN-OBS env_files_found false", result.stdout)

    def test_deep_secret(self):
        self.put("a/b/c/d/e/space and\nnewline/.env.production", "not a credential\n")
        self.assertNotEqual(self.secret_gate().returncode, 0)

    def test_long_positive_search(self):
        for index in range(2000):
            self.put(f"secrets/.env.{index:04d}." + "x" * 180, "control\n")
        self.assertNotEqual(self.secret_gate().returncode, 0)

    def test_tracked_generated_secret(self):
        self.put("build/output/.env.production", "control\n")
        self.git("add", "-f", "build/output/.env.production")
        self.assertNotEqual(self.secret_gate().returncode, 0)

    def test_search_failure(self):
        bindir = self.root / "bin"
        self.put("bin/find", "#!/bin/sh\nexit 2\n", executable=True)
        self.copy("tests/gates/run.sh")
        result = subprocess.run(["bash", "tests/gates/run.sh", "zero-disk-secrets"],
                                cwd=self.root, capture_output=True, text=True,
                                env={**os.environ, "PATH": str(bindir) + ":" + os.environ["PATH"]})
        self.assertNotEqual(result.returncode, 0)

    def regression_setup(self):
        self.copy("seed/host/regression.sh")
        self.copy("seed/rung4/om4.bin")
        self.copy("seed/out/omc")
        manifest = ROOT / "seed/host/original-fixtures.tsv"
        if manifest.exists():
            self.copy("seed/host/original-fixtures.tsv")
            paths = [line.split("\t")[0] for line in manifest.read_text().splitlines()]
        else:
            paths = ["seed/rung4/tests/echo.om"]
        for name in paths:
            self.copy(name)
        self.git("add", "seed")
        # Remaining corpus roots are also part of the committed baseline.
        for name in ("tests", "tools"):
            if (self.root / name).exists():
                self.git("add", name)
        return paths

    def test_nonempty_regression_control(self):
        self.regression_setup()
        result = self.invoke("sh", "seed/host/regression.sh")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_missing_baseline(self):
        paths = self.regression_setup()
        for name in paths:
            (self.root / name).unlink()
        self.assertNotEqual(self.invoke("sh", "seed/host/regression.sh").returncode, 0)

    def test_both_compilers_refuse(self):
        self.regression_setup()
        for name in ("seed/rung4/om4.bin", "seed/out/omc"):
            self.put(name, "#!/bin/sh\ncat >/dev/null\nexit 9\n", executable=True)
        self.assertNotEqual(self.invoke("sh", "seed/host/regression.sh").returncode, 0)

    def test_empty_manifest(self):
        self.regression_setup()
        self.put("seed/host/original-fixtures.tsv", "")
        self.assertNotEqual(self.invoke("sh", "seed/host/regression.sh").returncode, 0)

    def test_duplicate_manifest(self):
        self.regression_setup()
        manifest = self.root / "seed/host/original-fixtures.tsv"
        line = manifest.read_text().splitlines()[0] if manifest.exists() else "seed/rung4/tests/echo.om\t0"
        self.put("seed/host/original-fixtures.tsv", (line + "\n") * 62)
        self.assertNotEqual(self.invoke("sh", "seed/host/regression.sh").returncode, 0)

    def test_missing_ir_is_not_qualification(self):
        self.copy("seed/host/check.sh")
        self.copy("seed/out/omc")
        self.put("seed/omega-from-seed.sh", "#!/bin/sh\ntouch rebuild-was-run\n")
        for name in ("gate", "negative", "regression"):
            self.put(f"seed/host/{name}.sh", "#!/bin/sh\nexit 0\n")
        for name in ["gate-ir"] + [f"gate{i}" for i in range(7, 19)]:
            self.put(f"seed/omc/{name}.sh", "#!/bin/sh\nexit 0\n")
        result = self.invoke("sh", "seed/host/check.sh")
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse((self.root / "rebuild-was-run").exists())
        self.assertNotIn("PASS native-host qualification", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
