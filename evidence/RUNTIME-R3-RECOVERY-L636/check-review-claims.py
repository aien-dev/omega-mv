#!/usr/bin/env python3
"""External documentary oracle for L637 findings; not production runtime."""
from pathlib import Path
import csv, json, sys
root=Path(__file__).resolve().parents[2]
rows={r['file']:r for r in csv.DictReader((l for l in (root/'docs/runtime/portability_table.tsv').read_text().splitlines() if not l.startswith('#')),delimiter='\t')}
cases={
 'src/omega_gpu_matmul_api.h':('OMEGA_GPU_MATMUL_TILE_M 16','tile'),
 'src/omega_gpu_moe_api.h':('col0 % 8 == 0','packing'),
 'src/omega_gpu_platform.h':('one GPFIFO entry','GPFIFO'),
 'src/omega_gpu_session.h':('QMD default (1024)','QMD'),
 'src/omega_gpu_serving.h':('GB10 serving buffers','GB10'),
 'src/omega_gpu_serving.c':('omega_gpu_matmul_reserve','omega_gpu_matmul_reserve'),
}
results=[]
for file,(source_anchor,reason_anchor) in cases.items():
    assert source_anchor in (root/file).read_text(),f'source evidence changed: {file}'
    r=rows[file]
    ok=r['class']=='CHIP_SPECIFIC' and reason_anchor.lower() in r['reason'].lower()
    results.append(dict(file=file,source_anchor=source_anchor,expected_class='CHIP_SPECIFIC',actual_class=r['class'],reason_anchor=reason_anchor,pass_check=ok))
print(json.dumps(results,indent=2))
sys.exit(0 if all(r['pass_check'] for r in results) else 1)
