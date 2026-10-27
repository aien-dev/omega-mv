# fuzz.awk -- one random OM-2 program of nested constant-count loops, ifs and calls, for gate19's
# differential run (seed/out/omc against the same compiler with hoisting and implied checks disabled).
# awk -v seed=N -f fuzz.awk < /dev/null. The program's exit is the low byte of a wrapping hash; every
# index is in range for its counter, so a trap (exit 100 + code) in either build is itself a difference.
function r(n) { return int(rand() * n) }
function k() {
    if (r(3) == 0) return r(4096)
    return KP[r(NK)]
}
function body(d, fnum, ctrs, nc,    i, m, c, t, cn) {
    m = 1 + r(4)
    if (r(4) == 0) m += 6 + r(6)
    for (i = 0; i < m; i++) {
        t = r(9)
        if (t == 0 && d < 3) {
            cn = "c" (++NC); c = 1 + r(64)
            print "for " cn " ulong " c
            CT[cn] = c
            body(d + 1, fnum, ctrs " " cn, nc + 1)
            print "end"
        } else if (t == 1 && d < 3) {
            print "if gt and s 7 " r(8)
            body(d + 1, fnum, ctrs, nc)
            print "end"
        } else if (t == 2 && fnum > 1) {
            print "s = add s f" (1 + r(fnum - 1)) " s"
        } else if ((t == 3 || t == 4) && nc > 0) {
            split(ctrs, a, " "); cn = a[1 + r(nc)]
            if (t == 3) print "put g " cn " add get g " cn " " k()
            else print "s = add s get g sub " (CT[cn] - 1) " " cn
        } else if (t == 5) {
            if (r(2)) print "s = sub s " k()
            else print "s = add s ulong argcnt"
        } else if (t == 6) {
            print "s = xor s " k()
        } else {
            print "s = add mul s 31 " k()
        }
    }
}
BEGIN {
    srand(seed)
    NK = 3 + r(10)
    for (i = 0; i < NK; i++) KP[i] = 4096 + r(1000000)
    NC = 0
    print "om2"
    print "global g ulong 64"
    nf = 1 + r(3)
    for (f = 1; f <= nf; f++) {
        print "fn f" f " a ulong to ulong"
        print "s = a"
        body(0, f, "", 0)
        print "ret s"
        print "end"
    }
    print "fn main to int"
    print "s = ulong argcnt"
    body(0, nf + 1, "", 0)
    print "ret int and s 255"
    print "end"
}
