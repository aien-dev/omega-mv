#!/usr/bin/env python3
import json,subprocess,sys,random,hashlib
binary=sys.argv[1];tokens=b'1 100 50 2 0 1 160 160 2 1'.split();canonical=b' '.join(tokens);rows=[]
def check(name,data,valid,chunk=None):
 if chunk:
  p=subprocess.Popen([binary],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
  for n in range(0,len(data),chunk):p.stdin.write(data[n:n+chunk]);p.stdin.flush()
  p.stdin.close();p.stdin=None;out,err=p.communicate(timeout=3);rc=p.returncode
 else:
  p=subprocess.run([binary],input=data,capture_output=True,timeout=3);out,err,rc=p.stdout,p.stderr,p.returncode
 ok=(rc==0 and out==b'1\n1\n1\n0\n') if valid else (rc==20 and out==b'')
 rows.append({'case':name,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'exit':rc,'stdout':out.decode(errors='replace'),'pass':ok})
for c in range(256):
 if 48<=c<=57:continue
 check('separator-byte-'+str(c),bytes([c]).join(tokens),c in (9,10,11,12,13,32))
for c in (9,10,11,12,13,32):
 for n in (16383,16384):
  for side in ('prefix','suffix'):
   pad=bytes([c])*(n-len(canonical));check(f'{side}-{c}-{n}',pad+canonical if side=='prefix' else canonical+pad,n==16383)
for n in (16383,16384):
 data=b'0'*(n-len(canonical))+canonical
 check('leading-zero-'+str(n),data,n==16383)
for name,data in [('u64-overflow',b'1 100 50 2 0 1 160 160 2 18446744073709551616'),('empty',b''),('nine',b' '.join(tokens[:-1])),('eleven',canonical+b' 1'),('whitespace-only',b'\v'*16383),('513tokens',b'1 '*513)]:check(name,data,False)
r=random.Random(552)
for n in range(64):
 parts=[b'0'*r.randrange(16)+t for t in tokens];sep=[bytes(r.choice((9,10,11,12,13,32)) for _ in range(r.randrange(1,20))) for _ in range(11)]
 data=sep[0]+b''.join(t+sep[i+1] for i,t in enumerate(parts));check('fragmented-'+str(n),data,True,chunk=r.randrange(1,17))
print(json.dumps({'checks':len(rows),'failed':sum(not r['pass'] for r in rows),'rows':rows}));sys.exit(any(not r['pass'] for r in rows))
