# Builds sha256-s3.om (TEMPORARY S3 test unit, L838) from the delivered unit. Functions marked VERBATIM
# are copied byte for byte; the others are listed with their reduction in the unit's header notes.
import re
U = open('/home/drakestapleton/workspace/omega-om2-sha/om/sha256/sha256-om2.om').read()
def block(src, start):
    i = src.index(start); lines = src[i:].split('\n'); d = 0
    for n, l in enumerate(lines):
        w = l.split()[:1]
        if w and w[0] in ('fn', 'export', 'for', 'if', 'while', 'layout'): d += 1
        elif w == ['end']:
            d -= 1
            if d == 0: return '\n'.join(lines[:n + 1]) + '\n'
    raise ValueError(start)
def fn(name):
    m = re.search(r'^(export )?fn %s .*$' % name, U, re.M); return block(U, m.group(0))
pre = U[U.index('layout shactx 104'):U.index('\nend\n', U.index('layout shactx 104')) + 5]
kinit, iv, rounds, init, final = fn('kinit'), fn('iv'), fn('rounds'), fn('sha256_init'), fn('sha256_final')
sched = fn('sched').replace('m view uchar * o ulong', 'm view uchar 128 o ulong')
compress = fn('compress').replace('m view uchar * o ulong', 'm view uchar 128 o ulong')
finish = fn('finish')
init = init.replace('export fn', 'fn')
final = final.replace('export fn', 'fn').replace('o out uchar 32', 'o edit uchar 32')
upd = fn('sha256_update').replace('export fn sha256_update c edit shactx d view uchar * to void\nn = len d\n',
                                  'fn sha256_update c edit shactx d view uchar 128 n ulong to void\n')
assert upd != fn('sha256_update')
head = '''note sha256-s3.om: REDUCED test unit for OM-2 compiler slice S3 (ledger L838). It is NOT the delivered
note unit om/sha256/sha256-om2.om and is never linked into the product. Built by gen.py.
note From the delivered unit, byte for byte: layout shactx, kinit, iv, rounds, finish, sha256_init (no
note export), sha256_final (o edit instead of out). sched (w fresh) and compress take m view uchar 128
note instead of the unsized view (S4); sha256_update takes (d view uchar 128, n) instead of len d.
note Reduced: absorb completes one pending block from at most 64 new bytes (no blocks/run, no part);
note program mode: stdin in chunks of 1, 64, 3, 55, 63, 56, 17 bytes with an empty update between,
note hex digest on stdout. Receipts in main (exit 3 on failure): a byte copy of the context taken
note after the third chunk and continued (erratum E3), and a context reused after a final (section 7).
om2

'''
absorb = '''note REDUCED absorb: n new bytes with n >= 64 - bi and n <= 64; completes the pending block
fn absorb c edit shactx d view uchar 128 bi ulong n ulong to void
s = new uint 8 0
w = new uint 64 0
kt = new uint 64 0
blk = new uchar 128 0
kinit kt
for i ulong 8 unroll
put s i get c state i
end
off = sub 64 bi
for i ulong bi
put blk i get c buf i
end
for i ulong off
put blk add bi i get d i
end
compress s w kt blk 0
for i ulong sub n off
put c buf i get d add off i
end
for i ulong 8 unroll
put c state i get s i
end
end

'''
upd = upd.replace('absorb c d bi n', 'absorb c d bi n')
main = '''fn hexd d uint to uchar
if lt d 10
ret uchar add d 48
end
ret uchar add d 87
end

fn chunk j ulong to ulong
k = rem j 7
if eq k 0
ret 1
end
if eq k 1
ret 64
end
if eq k 2
ret 3
end
if eq k 3
ret 55
end
if eq k 4
ret 63
end
if eq k 5
ret 56
end
ret 17
end

fn copy a view uchar 104 b edit uchar 104 to void
for i ulong 104
put b i get a i
end
end

fn same a view uchar 32 b view uchar 32 to ulong
for i ulong 32
if ne get a i get b i
ret 0
end
end
ret 1
end

fn hexout o view uchar 32 to void
x = new uchar 65 10
for i ulong 32
put x mul i 2 hexd uint shr get o i 4
put x add mul i 2 1 hexd uint and get o i 15
end
z = write 1 x 0 65
end

fn main to int
c = new uchar 104 0
r = new uchar 104 0
k = new uchar 104 0
d = new uchar 128 0
o = new uchar 32 0
o2 = new uchar 32 0
o3 = new uchar 32 0
sha256_init r
sha256_update r d 50
sha256_update r d 50
sha256_final r o
sha256_init r
sha256_init c
j = ulong 0
got = long 1
while gt got 0
got = read 0 d 0 chunk j
if lt got 0
exit 2
end
sha256_update c d ulong got
sha256_update r d ulong got
if eq j 2
copy c k
end
if gt j 2
sha256_update k d ulong got
end
sha256_update c d 0
j = add j 1
end
if lt j 3
copy c k
end
sha256_final c o
sha256_final r o2
sha256_final k o3
if eq same o o2 0
exit 3
end
if eq same o o3 0
exit 3
end
hexout o
ret 0
end
'''
out = head + pre + '\n' + kinit + '\n' + iv + '\n' + sched + '\n' + rounds + '\n' + compress + '\n' + finish + '\n' + init + '\n' + absorb + upd + '\n' + final + '\n' + main
open('sha256-s3.om', 'w').write(out)
