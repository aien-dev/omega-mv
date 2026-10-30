E1Open3 Wolfram derivation of the a-priori ulp bounds for omega_math_exp / omega_math_log
(src/omega_numeric.c at omega-mv 0919efc3c, lines 477-553). Run 2026-10-27 ~11:05Z through
mcp__wolfram_wolframlanguageevaluator (hosted, stateless kernel). First-order rounding analysis:
each rounded op contributes half an ulp of its own result times the derivative of the final value;
terms of order u^2 are dropped. Integer ulp distance against RN(exact) is bounded by Floor[Epre + 1],
where Epre is the error before the final rounding, in ulps of the exact result's binade.

## EXP

Query:
f32[x_]:=With[{e=Floor[Log2[Abs[x]]]},Round[x 2^(23-e)] 2^(e-23)];
ul[y_]:=If[y==0,0,2^(Floor[Log2[Abs[y]]]-23)];hu[y_]:=ul[y]/2;
c3=f32[1666666716/10^10];c4=f32[416666679/10^10];c5=f32[83333338/10^10];
inv=f32[14426950408889634/10^16];hi=f32[693145751953125/10^15];lo=f32[14286068203094172/10^22];
slack=2^-17+104 Abs[inv-1/Log[2]];  h=Log[2](1/2+slack);
p[r_]:=1+r+r^2/2+c3 r^3+c4 r^4+c5 r^5;
dr[r_]:=hu[150 lo]+hu[r]+150 Abs[Log[2]-hi-lo];
rndPre[r_]:= Horner roundings a1=r c5, t1=c4+a1, p1=r t1, t2=c3+p1, p2=r t2, t3=1/2+p2, p3=r t3, t4=1+p3, t5=r t4,
  weighted by r^4,r^4,|r|^3,|r|^3,r^2,r^2,|r|,|r|,1;
epre[r_]:=(Abs[p[r]-Exp[r]]+rndPre[r]+Exp[r] dr[r])/ul[Exp[r]];  grid of 8001 r over [-h,h], 30 digits.

Result:
c3 -> 11184811/67108864, c4 -> 11184811/268435456, c5 -> 8947849/1073741824,
hi -> 22713/32768, lo -> 6283079/4398046511104, inv -> 12102203/8388608,
ln2-hi-lo -> 5.4979e-14, slack -> 9.6324e-6, h -> 0.346580266948,
maxApproxUlp -> 38.47122619 at r = -h; approx at +h -> 21.23492274,
maxEpreUlp -> 39.15458979 at r = -h (rounding part 0.68336360),
bound = Floor[Epre+1] -> 40 ulp.  Declared NUM_ORACLE_EXP_ULP = 40.
Subnormal results: the final scale rounds once into a coarser ulp, so Epre in that ulp is no larger; same bound.

## LOG

Query:
c1=f32[333333343/10^9];c2=f32[200000003/10^9];c3=f32[142857149/10^9];L=f32[6931471805599453/10^16];M=f32[141421356/10^8];
z=(m-1)/(m+1) (m-1 exact by Sterbenz; m+1 and the correctly rounded divide each add half an ulp),
Horner s1=z2 c3, s2=c2+s1, s3=z2 s2, s4=c1+s3, s5=z2 s4, pl=1+s5, lm=2 z pl;
Epre(e,m) = (|e| |L-ln2| + hu[e L] + |lm-Log[m]| + roundings of lm + hu[lm] (e!=0)) / ul[e ln2 + Log[m]];
m over 3001 grid points in (M/2, M], e in {-149,-148,-127,-126,-125,-100,-64,-10..10,64,100,126,127,128}.

Result:
c1 -> 11184811/33554432, c2 -> 13421773/67108864, c3 -> 9586981/67108864, LN2f -> 1453635/2097152,
ln2-LN2f -> -1.9047e-9.
Per-e max Epre: e=-1 4.2186, e=1 4.1653, e=0 2.7825, e=-3 1.7727, e=3 1.8233, e=-6 1.3624, e=2 1.3526,
all |e|>=7 below 0.75.
worst -> {-1, 4.2186}; bound = Floor[Epre+1] -> 5 ulp.  Declared NUM_ORACLE_LOG_ULP = 4.
The a-priori analysis does not prove 4; only the exhaustive measurement can (see ulp-LOG.log).
