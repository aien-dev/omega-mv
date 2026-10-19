#!/usr/bin/env python3
"""Summarise the E03 payback receipts into a markdown table. Every number comes from the JSON files."""
import json, sys, os
D = sys.argv[1] if len(sys.argv) > 1 else "evidence/PLAN_REUSE/e03-payback"
pred = json.load(open(f"{D}/01-prediction.json"))
runs = {}
for name, path in [("run1", "heldout/heldout-run1-receipt.json"), ("run2", "heldout/heldout-run2-receipt.json"),
                   ("run3-noadapt", "heldout/heldout-run3-noadapt-receipt.json")]:
    p = f"{D}/{path}"
    if os.path.exists(p): runs[name] = json.load(open(p))

def us(ns): return f"{ns/1000:+.1f}"
print("## Held-out results (microseconds; wall e2e per task, arm A minus arm B, cumulative over H=40)\n")
print("Prediction from the development partition (01-prediction.json, committed at 23472463 before any held-out run):\n")
print("| stream | p | Delta us/hit [q1,q3] | C us | m us | predicted net(40) us [band] | predicted break-even (tasks) | no finite payoff |")
print("|---|---|---|---|---|---|---|---|")
for s in ("S1_reverse", "S1_merge_P+", "P0_no_payoff"):
    q = pred["prediction"][s]
    print(f"| {s} | {q['p']:.2f} | {us(q['Delta_ns'])} [{us(q['Delta_band_ns'][0])}, {us(q['Delta_band_ns'][1])}] | {q['C_ns']/1000:.1f} | {q['m_ns']/1000:.1f} | {us(q['net_closed_form_ns'])} [{us(q['net_band_ns'][0])}, {us(q['net_band_ns'][1])}] | {q['break_even_tasks']} | {q['no_finite_payoff']} |")
print()
for rn, r in runs.items():
    h = r["host"]
    print(f"### {rn}: arm B = {r['arm_B_variant']}; commit {r['run_commit'][:8]}; binary {r['test_binary_sha256'][:12]}; load start {h['loadavg_start'].split()[0]} end {h['loadavg_end'].split()[0]}; checks {r['checks']} failures {r['failures']} bad_rows {r['bad_rows']}; reconciliation {'OK' if r['reconciliation']['ok'] else 'MISMATCH'} ({r['reconciliation']['rows']} rows, unattributed {r['reconciliation']['unattributed_wall_ns']/1000:.0f} us)\n")
    print("| stream | hits/misses | acq | adapt | refusals | rederiv | restarts | floors | neg transfer | net sum us | break-even | per-task median us [q1,q3] | net robust us | saved_gross us | spent_learning us (acq/maint/repair/restart) | reinvested / unfunded us | templates (bytes) | applic cpu first->last ns |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    for s, a in r["streams"].items():
        L = a["reinvestment_ledger_ns"]; m = a["memory"]; rb = a.get("robust", {})
        print(f"| {s} | {a['hits']}/{a['misses']} | {a['acquisitions']} | {a['adaptations']} | {a['refusals']} | {a['rederivations']} | {a['restarts']} | {'ok' if a['floors_ok'] else 'VIOLATED'} | {a['negative_transfer']} | {us(a['net_final_ns'])} | {a['break_even_tasks']} | {us(rb.get('task_net_median_ns',0))} [{us(rb.get('task_net_q1_ns',0))}, {us(rb.get('task_net_q3_ns',0))}] | {us(rb.get('net_robust_ns',0))} | {us(L['saved_gross'])} | {L['spent_learning']/1000:.1f} ({L['spent_acquisition']/1000:.1f}/{L['spent_maintenance']/1000:.1f}/{L['spent_repair']/1000:.1f}/{L['spent_restart']/1000:.1f}) | {L['reinvested']/1000:.1f} / {L['unfunded']/1000:.1f} | {m['templates']} ({m['bytes']}) | {m['applicability_cpu_ns_first']}->{m['applicability_cpu_ns_last']} |")
    c = r["controls"]
    print(f"\nControls: P+ sign {c['sign_P+']}, P0 sign {c['sign_P0']} (expected {c['expected']}); stale refused/rederived/revalidated {c['stale_refused']}/{c['stale_rederived']}/{c['stale_revalidated']}; corrupt {c['corrupt_refused']}/{c['corrupt_cases']} refused ({', '.join(c['corrupt_why'])}).\n")
    print("Prediction vs measurement:\n")
    print("| stream | predicted net(40) us [band] | measured net sum us | measured robust us | in band (sum / robust) | predicted break-even | measured break-even | measured Delta/hit median us [q1,q3] |")
    print("|---|---|---|---|---|---|---|---|")
    for s in ("S1_reverse", "S1_merge_P+", "P0_no_payoff"):
        q = pred["prediction"][s]; mm = r["measured_for_prediction"][s]
        lo, hi = q["net_band_ns"]; lo, hi = min(lo, hi), max(lo, hi)
        inb = lambda v: "yes" if lo <= v <= hi else "no"
        print(f"| {s} | {us(q['net_closed_form_ns'])} [{us(lo)}, {us(hi)}] | {us(mm['net_final_ns'])} | {us(mm.get('net_robust_ns',0))} | {inb(mm['net_final_ns'])} / {inb(mm.get('net_robust_ns',0))} | {q['break_even_tasks']} | {mm['break_even_tasks']} | {us(mm['delta_median_ns'])} [{us(mm['delta_q1_ns'])}, {us(mm['delta_q3_ns'])}] |")
    print()
