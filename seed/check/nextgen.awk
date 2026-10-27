# nextgen.awk -- random well-formed Omegese programs over the om4 words (OMEGESE.md section 9), for the
# next-word fuzz (decision 22). awk -v seed=N -f nextgen.awk  writes one program on stdout.
# Every program is meant to be accepted; the only allowed refusal is error 12 (out of registers, a
# documented limit the generator does not model exactly).
function rnd(n) { return int(rand() * n) }
function pick(s,   a, n) { n = split(s, a, " "); return a[rnd(n) + 1] }
function tname(t) { return TN[t] }
function lit(t,   v) {           # a typed number of type t
  if (t == 1) return (rnd(2) ? "true" : "false")
  if (t <= 5) v = rnd(t == 2 ? 256 : 300); else v = rnd(200) - 100
  if (t == 6 && (v < -128 || v > 127)) v = 7
  return tname(t) " " v
}
# visible scalar names of type t (scope arrays SN/ST, count NS)
function scal(t,   i, c, n) { c = ""; n = 0
  for (i = 1; i <= NS; i++) if (ST[i] == t) { c = c " " SN[i]; n++ }
  if (n == 0) return ""; return pick(c) }
# visible arrays: AK kind (1 local, 2 view, 3 edit, 4 global, 5 text), AT type, AL length
function arr(t, wr,   i, c, n) { c = ""; n = 0
  for (i = 1; i <= NA; i++) if ((t == 0 || AT[i] == t) && (!wr || AK[i] == 1 || AK[i] == 3 || AK[i] == 4)) { c = c " " i; n++ }
  if (n == 0) return 0; return pick(c) + 0 }
function val(t, d,   r, a, u, op, f) {
  if (d <= 0) { r = scal(t); return (r != "" && rnd(2)) ? r : lit(t) }
  r = rnd(12)
  if (t == 1) {
    if (r < 4) { u = 2 + rnd(8); return pick("eq ne lt le gt ge") " " val(u, d - 1) " " val(u, d - 1) }
    if (r < 6) return pick("and or xor") " " val(1, d - 1) " " val(1, d - 1)
    if (r < 7) return "not " val(1, d - 1)
    if (r < 8) return "( " val(1, d - 1) " )"
    return val(1, 0)
  }
  if (r < 3) { op = pick("add sub mul div rem and or xor")
    if (rnd(4) == 0) return op " " val(t, d - 1) " " (rnd(50) + 1)          # one bare number
    return op " " val(t, d - 1) " " val(t, d - 1) }
  if (r < 4) return pick("shl shr") " " val(t, d - 1) " " (rnd(2) ? rnd(TW[t]) : "ulong " rnd(TW[t]))
  if (r < 5) return pick("neg not") " " val(t, d - 1)
  if (r < 6) { a = arr(t, 0); if (a) return "get " AN[a] " " idx(a, d - 1) }
  if (r < 7 && t == 5) { a = arr(0, 0); if (a) return "len " AN[a] }
  if (r < 8) { u = 2 + rnd(8); if (u != t) return tname(t) " " val(u, d - 1) }  # a checked conversion
  if (r < 9) { f = vfn(t); if (f) { a = callargs(f); if (a != "") return a } }
  if (r < 10 && t == 9) { a = (rnd(4) ? arr(0, 1) : arr(0, 0))
    if (a && AK[a] != 4) return (AK[a] == 1 || AK[a] == 3 ? pick("read read write") : "write") " int " rnd(3) " " AN[a] " ulong 0 ulong " (rnd(AL[a]) + 1) }
  if (r < 11) return "( " val(t, d - 1) " )"
  return val(t, 0)
}
function vfn(t,   i, c, n) { c = ""; n = 0
  for (i = 1; i <= NF2; i++) if (FR[i] == t && (PM || i != CUR)) { c = c " " i; n++ }
  if (n == 0) return 0; return pick(c) + 0 }
function callargs(f,   s, j, a, used, k) { s = FN2[f]; used = " "
  for (j = 1; j <= FP[f]; j++) {
    k = FPK[f, j]
    if (k == 0) s = s " " val(FPT[f, j], 1)
    else { a = pass(FPT[f, j], FPL[f, j], k, used); if (!a) return ""; used = used a " "
      s = s " " (rnd(2) ? (k == 3 ? "edit " : "view ") : "") AN[a] }
  }
  return s }
function pass(t, l, k,   i, c, n) { c = ""; n = 0   # an array for a view (k 2) / edit (k 3) parameter
  for (i = 1; i <= NA; i++) if (AT[i] == t && AL[i] == l && AK[i] != 4 && (k == 2 || AK[i] == 1 || AK[i] == 3)) { c = c " " i; n++ }
  if (n == 0) return 0; return pick(c) + 0 }
function line(s) { if (s != "") print (rnd(9) ? s : (rnd(2) ? s "\t" : "  " s)) }
function newname(p) { NM++; return p NM }
function stmts(n, depth, top,   i) { for (i = 0; i < n; i++) stmt(depth, top) }
function stmt(depth, top,   r, t, nm, a, c, f, s, ns, na, u, n) {
  r = rnd(14)
  if (r < 3 && NS < 5) { t = 1 + rnd(9); nm = newname("v"); line(nm " " (rnd(2) ? "= " : "") val(t, 2)); NS++; SN[NS] = nm; ST[NS] = t; return }
  if (r < 4 && NS > 0) { u = 1 + rnd(NS); line(SN[u] " = " val(ST[u], 2)); return }
  if (r < 5 && top && NA < 6) { t = 2 + rnd(8); nm = newname("a"); n = rnd(20) + 1; line(nm " = new " tname(t) " " n " " val(t, 1))
    NA++; AN[NA] = nm; AT[NA] = t; AL[NA] = n; AK[NA] = 1; return }
  if (r < 6) { a = arr(0, 1); if (a) { line("put " AN[a] " " idx(a, 1) " " val(AT[a], 1)); return } }
  if (r < 8 && depth < 2) { ns = NS; na = NA; line("if " val(1, 2)); stmts(1 + rnd(2), depth + 1, 0)
    if (rnd(2)) { NS = ns; NA = na; line("else"); stmts(1 + rnd(2), depth + 1, 0) }
    NS = ns; NA = na; line("end"); return }
  if (r < 9 && depth < 2) { ns = NS; na = NA; line("while " val(1, 1) (rnd(2) ? " bound " (rnd(999999) + 1) : "")); stmts(1 + rnd(2), depth + 1, 0)
    NS = ns; NA = na; line("end"); return }
  if (r < 10 && depth < 2) { ns = NS; na = NA; t = 2 + rnd(8); nm = newname("k")
    line("for " nm " " tname(t) " " (rnd(2) ? rnd(t == 6 ? 100 : 50) : val(t, 1)))
    NS++; SN[NS] = nm; ST[NS] = t; stmts(1 + rnd(2), depth + 1, 0); NS = ns; NA = na; line("end"); return }
  if (r < 11) { f = voidfn(); if (f) { s = callargs(f); if (s != "") { line(s); return } } }
  if (r < 12) { line("note " pick("a b c") " " rnd(9)); return }
  t = 1 + rnd(9); nm = newname("v"); line(nm " = " val(t, 1)); NS++; SN[NS] = nm; ST[NS] = t
}
function voidfn(   i, c, n) { c = ""; n = 0
  for (i = 1; i <= NF2; i++) if (FR[i] == 10 && (PM || i != CUR)) { c = c " " i; n++ }
  if (n == 0) return 0; return pick(c) + 0 }
function mkfn(name, ret, np,   s, j, k, t, l, pn) {
  NF2++; CUR = NF2; FN2[NF2] = name; FR[NF2] = ret; FP[NF2] = np
  NS = 0; NA = NG; s = "fn " name
  for (j = 1; j <= np; j++) { pn = newname("p"); k = (rnd(3) == 0 ? 2 + rnd(2) : 0); t = (k ? 2 + rnd(8) : 1 + rnd(9))
    FPK[NF2, j] = k; FPT[NF2, j] = t
    if (k) { l = rnd(8) + 1; FPL[NF2, j] = l; s = s " " pn " " (k == 2 ? "view " : "edit ") tname(t) " " l
      NA++; AN[NA] = pn; AT[NA] = t; AL[NA] = l; AK[NA] = k }
    else { s = s " " pn " " tname(t); NS++; SN[NS] = pn; ST[NS] = t } }
  print s " to " (ret == 10 ? "void" : tname(ret))
  stmts(1 + rnd(4), 0, 1)
  if (ret == 10) { if (rnd(3) == 0) line("ret") }
  else if (name == "main" && rnd(4) == 0) line("exit " val(8, 1))
  else line("ret " val(ret, 2))
  print "end"
}
BEGIN {
  srand(seed); split("bool uchar ushort uint ulong byte short int long", TN, " ")
  split("1 8 16 32 64 8 16 32 64", TW, " ")
  split("ab a\\nb x\\ty q\\\\r say\\\"hi\\\"", TXS, " "); split("2 3 3 3 6", TXL, " ")
  PM = (rnd(4) != 0)                       # program mode (fn main) 3 in 4
  NG = 0; NA = 0
  if (PM) for (g = rnd(3); g > 0; g--) { t = 2 + rnd(8); l = rnd(30) + 1; nm = newname("g")
    print "global " nm " " tname(t) " " l; NA++; AN[NA] = nm; AT[NA] = t; AL[NA] = l; AK[NA] = 4 }
  if (PM) for (g = rnd(2); g > 0; g--) { nm = newname("t"); k = rnd(5) + 1
    print "text " nm " \"" TXS[k] "\""; NA++; AN[NA] = nm; AT[NA] = 2; AL[NA] = TXL[k]; AK[NA] = 5 }
  NG = NA
  nf = 1 + rnd(3)
  for (f = 1; f <= nf; f++) mkfn(newname("f"), (rnd(3) ? 1 + rnd(9) : 10), rnd(4))
  if (PM) mkfn("main", 8, 0)
}
function idx(a, d,   r) {      # an index into array a: a ulong value, or a number inside the array
  r = scal(5); if (r != "" && rnd(2)) return r
  if (d > 0 && rnd(3) == 0) return "rem " val(5, d - 1) " ulong " AL[a]
  return "ulong " rnd(AL[a]) }
