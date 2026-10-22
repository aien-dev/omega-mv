// Read-only audit of the exact published L168 evidence. No model or timing run.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';

const fixed = '4c9fae6c2cbf1295d78841142d73e234b10e3cfe';
const base = 'a8ffd2b912c84197d08f963b05f9c7d8b30a3f8a';
const campaign = '66e092ac50c8fea66960f38e1a704aa39cf07f69';
const root = 'evidence/PLAN_REUSE/adapt-quality-fix/';
const git = (...args) => execFileSync('git', args, { encoding: 'utf8' });
const files = new Map();
function read(path) {
  const full = root + path;
  const data = git('show', `${fixed}:${full}`);
  files.set(full, createHash('sha256').update(data).digest('hex'));
  return data;
}
function pairs(csv) {
  const lines = csv.trim().split('\n').filter(l => !l.startsWith('#'));
  const headers = lines.shift().split(',');
  const map = new Map();
  for (const line of lines) {
    const row = Object.fromEntries(line.split(',').map((v, i) => [headers[i], v]));
    const key = `${row.stream}/${row.task}`;
    const pair = map.get(key) ?? {};
    assert(!pair[row.arm], `duplicate ${key}/${row.arm}`);
    pair[row.arm] = row;
    map.set(key, pair);
  }
  return map;
}
function quality(map) {
  let failures = 0;
  const lengths = {};
  for (const [key, { A, B }] of map) {
    assert(A && B, `missing arm: ${key}`);
    for (const field of ['shape', 'pert', 'seed']) assert.equal(A[field], B[field], `${key}/${field}`);
    for (const row of [A, B]) {
      assert.equal(row.success, '1', `${key}/${row.arm}/success`);
      assert.equal(row.goal_ok, '1', `${key}/${row.arm}/goal_ok`);
    }
    if (A.moves !== B.moves) {
      failures++;
      const k = `${A.moves} vs ${B.moves}`;
      lengths[k] = (lengths[k] ?? 0) + 1;
    }
  }
  return { pairs: map.size, f2_failures: failures, lengths };
}

assert.equal(git('diff', base, campaign, '--', 'src/'), '', 'campaign changed baseline runtime');
const red = read('01-red-base.log');
assert.match(red, /exit 1/);
assert.match(red, /checks 1326 failures 8/);
assert.match(red, /merge:.*12 moves; search from scratch 9 moves/);
assert.match(red, /swapped reverse:.*10 moves; search from scratch 7 moves/);
const green = read('02-green-test-plan-reuse.log');
assert.match(green, /exit 0/);
assert.match(green, /checks 1326 failures 0/);
assert.match(green, /merge:.*9 moves; search from scratch 9 moves/);
assert.match(green, /swapped reverse:.*7 moves; search from scratch 7 moves/);
assert.match(green, /OMEGA_PLAN_REUSE_PASS: FAIL/); // Preserve original verdict.
assert.match(read('04-make-test.log'), /head 4060eca5, exit 0/);
read('03-g2-ab-load.log');
read('00-precommit.md');
read('README.md');
read('payback-harness-L168.patch');

const results = {};
for (const mode of ['dev', 'heldout']) {
  const maps = {};
  for (const arm of ['before', 'after']) {
    const folder = `payback-${arm}-${mode}`;
    const receipt = JSON.parse(read(`${folder}/${mode}-receipt.json`));
    const map = pairs(read(`${folder}/${mode}-trace.csv`));
    maps[arm] = map;
    const result = quality(map);
    assert.equal(result.pairs, receipt.ledger.arm_A.rows);
    assert.equal(result.pairs, receipt.ledger.arm_B.rows);
    assert.equal(result.pairs * 2, receipt.reconciliation.rows);
    const log = read(`05-payback-${mode}-${arm}.log`);
    // dev json_head snapshots counters before four sign checks. The final
    // console summary includes them. Preserve both, rather than label dev green.
    const summary = log.match(/checks (\d+) failures (\d+) bad_rows (\d+)/);
    assert(summary, 'missing final console summary');
    const final = { checks: +summary[1], failures: +summary[2], bad_rows: +summary[3] };
    if (mode === 'dev') {
      assert.equal(final.checks, receipt.checks + 4);
      assert.equal(final.failures, receipt.failures + 1);
      assert.equal(final.bad_rows, receipt.bad_rows + 1);
      assert.equal(receipt.controls['sign_P+_dev'], 'negative');
    } else {
      assert.deepEqual(final, { checks: receipt.checks, failures: receipt.failures, bad_rows: receipt.bad_rows });
    }
    assert.equal((log.match(/FAIL .*F2 plan length/g) ?? []).length, result.f2_failures);
    if (arm === 'after') {
      assert.equal(result.f2_failures, 0);
      assert.equal(receipt.reconciliation.ok, true);
      assert.equal(receipt.reconciliation.bad_rows, 0);
      for (const stream of Object.values(receipt.streams)) {
        assert.equal(stream.floors_ok, true);
        assert.equal(stream.negative_transfer, 0);
      }
      for (const field of ['stale_refused', 'stale_rederived', 'stale_revalidated']) assert.equal(receipt.controls[field], true);
      assert.equal(receipt.controls.corrupt_cases, 3);
      assert.equal(receipt.controls.corrupt_refused, 3);
      assert.equal(receipt.controls.corrupt_stored, 0);
      if (mode === 'heldout') {
        assert.equal(receipt.checks, 3060);
        assert.equal(receipt.failures, 0);
        assert.equal(receipt.bad_rows, 0);
      } else {
        assert.equal(final.failures, 1); // Existing wall-sign failure is retained.
        assert.match(log, /FAIL .*P\+.*sign/);
      }
    }
    results[`${mode}_${arm}`] = { ...result, checks: receipt.checks, failures: receipt.failures,
      receipt_bad_rows: receipt.bad_rows, final_console: final, run_commit: receipt.run_commit,
      binary_sha256: receipt.test_binary_sha256 };
  }
  assert.deepEqual([...maps.before.keys()], [...maps.after.keys()], 'task order changed');
  for (const [key, a] of maps.before) {
    const b = maps.after.get(key);
    for (const arm of ['A', 'B']) for (const field of ['seed', 'shape', 'pert'])
      assert.equal(a[arm][field], b[arm][field], `changed training/task data: ${key}`);
    assert.equal(a.A.moves, b.A.moves, `search reference changed: ${key}`);
  }
}
assert.equal(results.heldout_before.f2_failures, 51);
assert.deepEqual(results.heldout_before.lengths, { '9 vs 12': 33, '7 vs 10': 18 });
assert.equal(results.dev_before.f2_failures, 0);

// Prove that this verifier rejects altered quality and missing-arm evidence.
const held = pairs(read('payback-after-heldout/heldout-trace.csv'));
const altered = structuredClone(held);
altered.values().next().value.B.moves = '999';
assert.equal(quality(altered).f2_failures, 1);
assert.throws(() => { const missing = structuredClone(held); delete missing.values().next().value.B; quality(missing); });

const changed = git('diff', '--name-only', base, fixed).trim().split('\n');
assert.deepEqual(changed.filter(p => !p.startsWith('evidence/')).sort(), [
  'spec/plan-reuse.md', 'src/runtime/rx_plan_arrange.c', 'src/runtime/rx_plan_arrange.h', 'tests/runtime/rx_plan_reuse.c'
]);
assert.equal(git('diff', fixed, 'HEAD', '--', 'src/', 'tests/', 'spec/', 'Makefile'), '', 'closeout changed implementation');
console.log(JSON.stringify({ audit: 'PASS', fixed, base, campaign, results,
  mutation_checks: 'PASS: longer plan detected; missing arm rejected',
  implementation_unchanged: true, historical_overall_gate: 'FAIL (preserved)',
  fresh_build_or_timing: 'NOT_RUN: verified existing published solution per assignment',
  evidence_sha256: Object.fromEntries([...files].sort()) }, null, 2));
