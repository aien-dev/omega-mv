# builds sha256-s2.om from the S1 test unit and the delivered unit's sched/rounds bodies (verbatim)
s1 = open('../sha-s1/sha256-s1.om').read()
U = open('/home/drakestapleton/workspace/omega-om2-sha/om/sha256/sha256-om2.om').read()
def block(src, start):
    i = src.index(start); j = src.index('\nend\n\n', i); return src[i:j + 5]
s = s1.replace('note sha256-s1.om: REDUCED test unit for OM-2 compiler slice S1', 'note sha256-s2.om: REDUCED test unit for OM-2 compiler slice S2')
s = s.replace("note Reduced: rolled loops instead of unroll (S2); the state", "note The schedule and round loops are `for ... unroll` with the delivered unit's exact bodies.\nnote Reduced: the state")
for name in ('bsig0', 'bsig1', 'ssig0', 'ssig1'):
    s = s.replace(block(s, 'fn %s x uint to uint\n' % name), '')
s = s.replace('note FIPS 180-4 4.1.2: Sigma0, Sigma1, sigma0, sigma1 on 32-bit words\n', '')
d = block(U, 'fn sched w edit uint 64 fresh m view uchar * o ulong to void\n')
d = d.replace('fn sched w edit uint 64 fresh m view uchar * o ulong to void', 'fn sched w edit uint 64 b view uchar 64 to void')
d = d.replace('q = add o mul i 4', 'q = mul i 4').replace('get m ', 'get b ')
s = s.replace(block(s, 'fn sched w edit uint 64 b view uchar 64 to void\n'), d)
s = s.replace(block(s, 'fn rounds s edit uint 8 w view uint 64 kt view uint 64 to void\n'),
              block(U, 'fn rounds s edit uint 8 w view uint 64 kt view uint 64 to void\n'))
open('sha256-s2.om', 'w').write(s)
