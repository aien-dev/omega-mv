"""Regression for failed attempts retaining prior successful evidence."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class EvidenceLifecycle(unittest.TestCase):
    def attempt(self, corrupt_contract):
        with tempfile.TemporaryDirectory(prefix='omega625-evidence-') as td:
            root = Path(td)
            (root/'tests/universal').mkdir(parents=True)
            (root/'spec/universal').mkdir(parents=True)
            for rel in ['tests/universal/semantics.py', 'spec/universal/semantics.md', 'spec/universal/profiles.json']:
                shutil.copyfile(ROOT/rel, root/rel)
            if corrupt_contract:
                with (root/'spec/universal/semantics.md').open('a') as f:
                    f.write('\nchanged contract\n')
            out = root/'out'
            out.mkdir()
            receipt = out/'receipt.json'
            receipt.write_text(json.dumps({'result':'PASS', 'attempt':'older'}))
            env = dict(os.environ, UNIVERSAL_OUT=str(out), CC=str(root/'compiler-does-not-exist'))
            proc = subprocess.run([sys.executable, str(root/'tests/universal/semantics.py')], env=env, capture_output=True, text=True, timeout=10)
            self.assertEqual(proc.returncode, 1, proc.stdout+proc.stderr)
            self.assertIn('FAIL:', proc.stderr)
            self.assertIn('STALE_CONTRACT_DIGEST' if corrupt_contract else 'compiler-does-not-exist', proc.stderr)
            self.assertFalse(receipt.exists(), 'failed attempt retained stale PASS receipt')

    def test_missing_compiler_invalidates_prior_pass(self):
        self.attempt(False)

    def test_probe_build_failure_invalidates_prior_pass(self):
        with tempfile.TemporaryDirectory(prefix='omega625-build-') as td:
            root = Path(td)
            (root/'mk').mkdir()
            (root/'tests/universal').mkdir(parents=True)
            for rel in ['mk/universal.mk', 'tests/universal/osc_probe.c']:
                shutil.copyfile(ROOT/rel, root/rel)
            receipt = root/'out/universal-semantics/receipt.json'
            receipt.parent.mkdir(parents=True)
            receipt.write_text('{"result":"PASS","attempt":"older"}')
            proc = subprocess.run(['make','-f','mk/universal.mk','OUT_DIR=out','CC=false','test-universal-semantics'], cwd=root, capture_output=True, text=True, timeout=10)
            self.assertEqual(proc.returncode, 2, proc.stdout+proc.stderr)
            self.assertIn('false', proc.stdout)
            self.assertFalse(receipt.exists(), 'failed probe build retained stale PASS receipt')

    def test_stale_contract_invalidates_prior_pass(self):
        self.attempt(True)

if __name__ == '__main__':
    unittest.main()
