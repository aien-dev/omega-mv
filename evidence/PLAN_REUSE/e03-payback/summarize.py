#!/usr/bin/env python3
"""Summarise the E03 payback receipts into markdown tables. Every number comes from the JSON files.
Usage: summarize.py [evidence/PLAN_REUSE/e03-payback]"""
import json, sys, os
D = sys.argv[1] if len(sys.argv) > 1 else "evidence/PLAN_REUSE/e03-payback"
pred_wall = json.load(open(f"{D}/01-prediction.json"))
pred_cpu = json.load(open(f"{D}/01-prediction-cpu.json")) if os.path.exists(f"{D}/01-prediction-cpu.json") else None
runs = {}
for name, path in [("run1", "heldout/heldout-run1-receipt.json"), ("run2", "heldout/heldout-run2-receipt.json"),
                   ("run3-noadapt", "heldout/heldout-run3-noadapt-receipt.json"), ("run4", "heldout/heldout-run4-receipt.json"),
                   ("run5-noadapt", "heldout/heldout-run5-noadapt-receipt.json")]:
    p = f"{D}/{path}"
    if os.path.exists(p): runs[name] = json.load(open(p))

def us(ns): return f"{ns/1000:+.1f}"
def band(q):
    lo, hi = q["net_band_ns"]; return min(lo, hi), max(lo, hi)

print("## Predictions (development partition only, committed before the held-out runs they apply to)\n")
for label, pred, key in (("wall e2e (01-prediction.json @23472463, applies to runs 1 to 3)", pred_wall, None),
                         ("cognition CPU (01-prediction-cpu.json @64be1932, applies to runs 4 and 5)", pred_cpu, "cognition_cpu")):
    if pred is None: continue
    P = pred["prediction"][key] if key else pred["prediction"]
    print(f"### {label}\n")
    print("| stream | p | Delta us/hit [q1,q3] | C us | m us | predicted net(40) us [band] | predicted break-even (tasks) | no finite payoff |")
    print("|---|---|---|---|---|---|---|---|")
    for s in ("S1_reverse", "S1_merge_P+", "P0_no_payoff"):
        q = P[s]; lo, hi = band(q)
        print(f"| {s} | {q['p']:.2f} | {us(q['Delta_ns'])} [{us(q['Delta_band_ns'][0])}, {us(q['Delta_band_ns'][1])}] | {q['C_ns']/1000:.1f} | {q['m_ns']/1000:.1f} | {us(q['net_closed_form_ns'])} [{us(lo)}, {us(hi)}] | {q['break_even_tasks']} | {q['no_finite_payoff']} |")
    print()

for rn, r in runs.items():
    h = r["host"]
    print(f"### {rn}: arm B = {r.get('arm_B_variant','existing reuse with adaptation/repair (frozen protocol)')}; commit {r['run_commit'][:8]}; binary sha256 {r['test_binary_sha256'][:12]}; load {h['loadavg_start'].split()[0]} (runnable {h['loadavg_start'].split()[3]}); checks {r['checks']} failures {r['failures']} bad_rows {r['bad_rows']}; reconciliation {'OK' if r['reconciliation']['ok'] else 'MISMATCH'} ({r['reconciliation']['rows']} rows, {r['reconciliation']['bad_rows']} bad, unattributed {r['reconciliation']['unattributed_wall_ns']/1000:.0f} us)\n")
    has_cpu = "cognition_cpu" in next(iter(r["streams"].values()))
    print("| stream | hits/misses | acq | adapt | refusals | rederiv | restarts | floors | neg transfer | wall net sum us | wall break-even | wall per-task median us [q1,q3] |" + (" CPU net us | CPU break-even | CPU Delta/hit us [q1,q3] |" if has_cpu else "") + " saved_gross us | spent_learning us (acq/maint/repair/restart) | reinvested / unfunded us | templates (bytes) |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|" + ("---|---|---|" if has_cpu else "") + "---|---|---|---|")
    for s, a in r["streams"].items():
        L = a["reinvestment_ledger_ns"]; m = a["memory"]; rb = a.get("robust", {})
        row = f"| {s} | {a['hits']}/{a['misses']} | {a['acquisitions']} | {a['adaptations']} | {a['refusals']} | {a['rederivations']} | {a['restarts']} | {'ok' if a['floors_ok'] else 'VIOLATED'} | {a['negative_transfer']} | {us(a['net_final_ns'])} | {a['break_even_tasks']} | {us(rb.get('task_net_median_ns',0))} [{us(rb.get('task_net_q1_ns',0))}, {us(rb.get('task_net_q3_ns',0))}] |"
        if has_cpu:
            c = a["cognition_cpu"]; d = c["delta_cpu_per_hit_ns"]
            row += f" {us(c['net_cpu_ns'])} | {c['break_even_tasks_cpu']} | {us(d['median'])} [{us(d['q1'])}, {us(d['q3'])}] |"
        row += f" {us(L['saved_gross'])} | {L['spent_learning']/1000:.1f} ({L['spent_acquisition']/1000:.1f}/{L['spent_maintenance']/1000:.1f}/{L['spent_repair']/1000:.1f}/{L['spent_restart']/1000:.1f}) | {L['reinvested']/1000:.1f} / {L['unfunded']/1000:.1f} | {m['templates']} ({m['bytes']}) |"
        print(row)
    c = r["controls"]
    extra = f"; on cognition CPU: P+ {c.get('sign_P+_cpu','n/a')}, P0 {c.get('sign_P0_cpu','n/a')}" if "sign_P+_cpu" in c else ""
    print(f"\nControls: wall sum P+ {c['sign_P+']}, P0 {c['sign_P0']}{extra} (expected {c['expected']}); stale refused/rederived/revalidated {c['stale_refused']}/{c['stale_rederived']}/{c['stale_revalidated']}; corrupt {c['corrupt_refused']}/{c['corrupt_cases']} refused ({'; '.join(c['corrupt_why'])}).\n")
    print("Prediction vs measurement (wall):\n")
    print("| stream | predicted net(40) us [band] | measured sum us | in band | predicted break-even | measured break-even | measured Delta/hit median us [q1,q3] |")
    print("|---|---|---|---|---|---|---|")
    for s in ("S1_reverse", "S1_merge_P+", "P0_no_payoff"):
        q = pred_wall["prediction"][s]; mm = r["measured_for_prediction"][s]; lo, hi = band(q)
        print(f"| {s} | {us(q['net_closed_form_ns'])} [{us(lo)}, {us(hi)}] | {us(mm['net_final_ns'])} | {'yes' if lo <= mm['net_final_ns'] <= hi else 'no'} | {q['break_even_tasks']} | {mm['break_even_tasks']} | {us(mm['delta_median_ns'])} [{us(mm['delta_q1_ns'])}, {us(mm['delta_q3_ns'])}] |")
    if has_cpu and pred_cpu:
        print("\nPrediction vs measurement (cognition CPU):\n")
        print("| stream | predicted net(40) us [band] | measured net us | in band | predicted break-even | measured break-even | measured Delta/hit median us [q1,q3] |")
        print("|---|---|---|---|---|---|---|")
        for s in ("S1_reverse", "S1_merge_P+", "P0_no_payoff"):
            q = pred_cpu["prediction"]["cognition_cpu"][s]; mm = r["measured_for_prediction"][s]["cognition_cpu"]; lo, hi = band(q)
            print(f"| {s} | {us(q['net_closed_form_ns'])} [{us(lo)}, {us(hi)}] | {us(mm['net_cpu_ns'])} | {'yes' if lo <= mm['net_cpu_ns'] <= hi else 'no'} | {q['break_even_tasks']} | {mm['break_even_tasks_cpu']} | {us(mm['delta_cpu_median_ns'])} [{us(mm['delta_cpu_q1_ns'])}, {us(mm['delta_cpu_q3_ns'])}] |")
    print()
