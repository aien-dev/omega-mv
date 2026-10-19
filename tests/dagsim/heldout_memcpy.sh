#!/bin/sh
# heldout_memcpy.sh DAGSIM_BIN EVIDENCE_DIR {predict|compare}
#
# Held-out test of the shared-bandwidth model (omega-mv#583, acceptance
# boxes 3 and 4). The DAG for k threads copying 1 GB is k independent copy
# ops that share one bandwidth resource. Fitting set: M1 block4 rows at
# k=1 (per-thread busy rate) and k=8 (shared pipe cap); both are loaded-box
# measurements from 2026-10-19. Held-out: fresh measurements at k=2,3,5,6
# (never measured before) plus k=4 and k=16, which block4 did measure, as
# the lookup control's home ground and the out-of-envelope case.
#
# predict: writes fixtures and predictions.csv. Run BEFORE any measurement.
# compare: reads measured/*.csv, applies the precommitted gates, writes
#          heldout_results.md. Controls: nearest-k lookup from block4 and a
#          spec roofline that uses the 273 GB/s datasheet figure.
set -u
BIN=${1:?dagsim}; EV=${2:?evidence dir}; MODE=${3:?predict|compare}
ASOF=2026-10-20
# fitted parameters (B/ns), from block4 1 GB rows: k=1 median 0.1117 s for 1 GB
# (lo 0.1079, hi 0.1166); k=8 read+write 65.4 GB/s median (lo 60.8, hi 69.8).
T1=0.1117; T1LO=0.1079; T1HI=0.1166
CAP=65.4e9; CAPLO=60.8e9; CAPHI=69.8e9
CALSRC="~/yardsticks/aien-model/M1/block4.csv"; CALID="M1-20261019-block4"; CALLOAD=14.8
KS="2 3 5 6 4 16"
# Round 2 and later: FIT_DIR holds memcpy_k1.csv and memcpy_k8.csv from this probe on the
# same box state; T1 and CAP are refitted from them (median, min, max). PRED_SUB/MEAS_SUB
# pick the output subdirectories so rounds do not overwrite each other.
PRED_SUB=${PRED_SUB:-predictions}; MEAS_SUB=${MEAS_SUB:-measured}; PRED_CSV="$EV/${PRED_CSV:-predictions.csv}"; RESULT_MD="$EV/${RESULT_MD:-heldout_results.md}"
if [ -n "${FIT_DIR:-}" ]; then
  med3() { awk -F, '!/^#/ && !/^gb/ && $2>0 {print $'"$1"'}' "$2" | sort -g | awk '{v[NR]=$1} END{n=NR; printf "%.6g %.6g %.6g", (n%2? v[(n+1)/2] : (v[n/2]+v[n/2+1])/2), v[1], v[n]}'; }
  set -- $(med3 4 "$FIT_DIR/memcpy_k1.csv"); T1=$1; T1LO=$2; T1HI=$3
  set -- $(med3 5 "$FIT_DIR/memcpy_k8.csv"); CAP=${1}e9; CAPLO=${2}e9; CAPHI=${3}e9
  CALSRC="$FIT_DIR"; CALID="round2-same-state"; CALLOAD=$(sed -n 's/^# before .*loadavg=\([0-9.]*\).*/\1/p' "$FIT_DIR/memcpy_k1.csv")
  echo "# refit from $FIT_DIR: T1=$T1 [$T1LO..$T1HI] s, CAP=$CAP [$CAPLO..$CAPHI] B/s, load1=$CALLOAD"
fi
mkdir -p "$EV/fixtures" "$EV/$PRED_SUB" "$EV/$MEAS_SUB"

fixture() { # k
  k=$1
  f="$EV/fixtures/${FIXPFX:-}memcpy_k$k.oev"
  {
    echo "# held-out fixture: $k threads copy 1 GB total; fitted from M1 block4 k=1 and k=8 only"
    echo "oev version=1"
    echo "machine id=spark-b87b soc=GB10 cores=20 mem_bytes=137438953472 prov=specified src=nvidia-dgx-spark-datasheet"
    echo "build repo=omega-mv sha=f4a9849f00000000000000000000000000000000"
    echo "calibration id=$CALID taken=2026-10-19 valid_days=30 load1=$CALLOAD src=$CALSRC"
    echo "resource id=cpu kind=compute slots=20 prov=specified src=nproc"
    echo "resource id=dram_cpu kind=bandwidth cap=$CAP lo=$CAPLO hi=$CAPHI prov=measured src=$CALSRC:gb=1,threads=8"
    echo "resource id=unified_mem kind=capacity cap=137438953472 prov=specified src=nvidia-dgx-spark-datasheet"
    i=0
    while [ $i -lt $k ]; do
      t=$(awk -v t=$T1 -v k=$k 'BEGIN{printf "%.0f", t/k*1e9}')
      lo=$(awk -v t=$T1LO -v k=$k 'BEGIN{printf "%.0f", t/k*1e9}')
      hi=$(awk -v t=$T1HI -v k=$k 'BEGIN{printf "%.0f", t/k*1e9}')
      b=$(awk -v k=$k 'BEGIN{printf "%.0f", 2e9/k}')
      echo "op id=copy$i kind=copy engine=cpu t_ns=$t lo=$lo hi=$hi prov=measured src=$CALSRC:gb=1,threads=1 bytes=$b bw=dram_cpu"
      i=$((i+1))
    done
    echo "buffer id=src bytes=1000000000 cap=unified_mem"
    echo "buffer id=dst bytes=1000000000 cap=unified_mem"
    echo "energy instrument=none"
  } > "$f"
  echo "$f"
}

if [ "$MODE" = predict ]; then
  echo "k,pred_s,pred_lo_s,pred_hi_s,pred_GBps,lookup_GBps,spec_roofline_GBps,status" > "$PRED_CSV"
  for k in $KS; do
    f=$(fixture $k)
    "$BIN" eval "$f" --asof $ASOF > "$EV/$PRED_SUB/memcpy_k$k.json"
    st=$(grep -o '"status": "[A-Z_]*"' "$EV/$PRED_SUB/memcpy_k$k.json" | head -1 | cut -d'"' -f4)
    nom=$(grep -o '"nominal": [0-9.e+]*' "$EV/$PRED_SUB/memcpy_k$k.json" | head -1 | awk '{print $2}')
    lo=$(grep -o '"lo": [0-9.e+]*' "$EV/$PRED_SUB/memcpy_k$k.json" | head -1 | awk '{print $2}')
    hi=$(grep -o '"hi": [0-9.e+]*' "$EV/$PRED_SUB/memcpy_k$k.json" | head -1 | awk '{print $2}')
    # lookup control: block4 1 GB median GBps at the nearest measured k in log2 distance (1,4,8,16)
    if [ -n "${FIT_DIR:-}" ]; then
      lk=$(awk -v k=$k 'BEGIN{d1=log(k);d8=log(k/8);if(d8<0)d8=-d8;print (d1<=d8)?1:8}')
      lookup=$(awk -F, '!/^#/ && !/^gb/ && $2>0 {print $5}' "$FIT_DIR/memcpy_k$lk.csv" | sort -g | awk '{v[NR]=$1} END{n=NR; if(!n){print "NA"; exit} printf "%.1f", (n%2? v[(n+1)/2] : (v[n/2]+v[n/2+1])/2)}')
    else
      lk=$(awk -v k=$k 'BEGIN{best=1;bd=99;for(i=1;i<=4;i++){m=(i==1?1:i==2?4:i==3?8:16);d=log(k/m);if(d<0)d=-d;if(d<bd){bd=d;best=m}}print best}')
      lookup=$(awk -F, -v m=$lk '$1==1 && $2==m {print $5}' "$HOME/yardsticks/aien-model/M1/block4.csv" | sort -g | awk '{v[NR]=$1} END{n=NR; if(!n){print "NA"; exit} printf "%.1f", (n%2? v[(n+1)/2] : (v[n/2]+v[n/2+1])/2)}')
    fi
    spec=$(awk -v t=$T1 -v k=$k 'BEGIN{tt=t/k; tp=2/273; if(tp>tt)tt=tp; printf "%.1f", 2/tt}')
    awk -v k=$k -v n=$nom -v l=$lo -v h=$hi -v lk=$lookup -v sp=$spec -v st=$st 'BEGIN{printf "%d,%.4f,%.4f,%.4f,%.1f,%s,%s,%s\n", k, n/1e9, l/1e9, h/1e9, 2/(n/1e9), lk, sp, st}' >> "$PRED_CSV"
  done
  cat "$PRED_CSV"
  exit 0
fi

if [ "$MODE" = compare ]; then
  out="$RESULT_MD"
  {
    echo "# Held-out results: shared-bandwidth model vs fresh memcpy measurements"
    echo
    echo "Gates were precommitted in PRECOMMIT.md before any of these rows were measured. Calibration: $CALID ($CALSRC)."
    echo
    echo "| k | measured median GB/s (n, min..max) | predicted GB/s [hi..lo] | rel. error | covered | lookup ctrl GB/s (err) | spec roofline GB/s (err) | envelope |"
    echo "|---|---|---|---|---|---|---|---|"
  } > "$out"
  pass_err=0; n_err=0; cover=0; n_cov=0
  for k in $KS; do
    m="$EV/$MEAS_SUB/memcpy_k$k.csv"
    [ -f "$m" ] || { echo "| $k | NOT_RUN | | | | | | |" >> "$out"; continue; }
    stats=$(awk -F, '!/^#/ && !/^gb/ && $2>0 {print $5}' "$m" | sort -g | awk '{v[NR]=$1} END{n=NR; med=(n%2? v[(n+1)/2] : (v[n/2]+v[n/2+1])/2); printf "%.1f %d %.1f %.1f", med, n, v[1], v[n]}')
    med=$(echo $stats | cut -d' ' -f1); n=$(echo $stats | cut -d' ' -f2); mn=$(echo $stats | cut -d' ' -f3); mx=$(echo $stats | cut -d' ' -f4)
    row=$(awk -F, -v k=$k '$1==k' "$PRED_CSV")
    ps=$(echo "$row" | cut -d, -f2); plo=$(echo "$row" | cut -d, -f3); phi=$(echo "$row" | cut -d, -f4); pg=$(echo "$row" | cut -d, -f5); lk=$(echo "$row" | cut -d, -f6); sp=$(echo "$row" | cut -d, -f7)
    g_hi=$(awk -v s=$plo 'BEGIN{printf "%.1f", 2/s}'); g_lo=$(awk -v s=$phi 'BEGIN{printf "%.1f", 2/s}')
    err=$(awk -v p=$pg -v m=$med 'BEGIN{printf "%+.1f%%", (p-m)/m*100}')
    lerr=$(awk -v p=$lk -v m=$med 'BEGIN{printf "%+.1f%%", (p-m)/m*100}')
    serr=$(awk -v p=$sp -v m=$med 'BEGIN{printf "%+.1f%%", (p-m)/m*100}')
    cov=$(awk -v m=$med -v lo=$g_lo -v hi=$g_hi 'BEGIN{print (m>=lo && m<=hi)?"yes":"no"}')
    env="in"; case $k in 16) env="OUT (16 threads + load above 20 cores)";; 4) env="in (lookup home ground)";; esac
    echo "| $k | $med ($n, $mn..$mx) | $pg [$g_lo..$g_hi] | $err | $cov | $lk ($lerr) | $sp ($serr) | $env |" >> "$out"
    case $k in 2|3|5|6)
      n_err=$((n_err+1)); n_cov=$((n_cov+1))
      awk -v p=$pg -v m=$med 'BEGIN{e=(p-m)/m; if(e<0)e=-e; exit !(e<=0.25)}' && pass_err=$((pass_err+1))
      [ "$cov" = yes ] && cover=$((cover+1));;
    esac
  done
  {
    echo
    echo "## Gates (precommitted)"
    echo
    echo "- G-err: nominal prediction within 25% of the measured median on each of k=2,3,5,6: $pass_err of $n_err passed. $( [ $pass_err -eq $n_err ] && [ $n_err -gt 0 ] && echo PASS || echo FAIL )"
    echo "- G-cov: [lo..hi] interval covers the measured median on at least 3 of 4 held-out k: $cover of $n_cov covered. $( [ $cover -ge 3 ] && echo PASS || echo FAIL )"
    echo "- Energy: UNMEASURED (no wall instrument integrated over these runs)."
    echo
    echo "Load during measurement is in the header lines of measured/*.csv; every number is a loaded-box number."
  } >> "$out"
  cat "$out"
  exit 0
fi
echo "mode must be predict or compare"; exit 2
