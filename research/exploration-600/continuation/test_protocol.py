#!/usr/bin/env python3
"""Independent framing/separator checks for the ten-u64 native protocol."""
import json,subprocess,sys
binary=sys.argv[1]; tokens=b'1 100 50 2 0 1 160 160 2 1'.split();canonical=b' '.join(tokens);expected=[1,1,1//(100-50)+1,0];rows=[]
def check(name,data,valid):
 p=subprocess.run([binary],input=data,capture_output=True,timeout=3)
 if valid:
  try:actual=list(map(int,p.stdout.split()))
  except ValueError:actual=None
  ok=p.returncode==0 and actual==expected
 else:ok=p.returncode==20 and p.stdout==b''
 rows.append({'case':name,'bytes':len(data),'exit':p.returncode,'stdout':p.stdout.decode(errors='replace'),'pass':ok})
for c in (9,10,11,12,13,32):check('ascii-separator-'+str(c),bytes([c]).join(tokens),True)
check('mixed_ascii',b' \t\n\v\f\r'.join(tokens),True)
for n in (16382,16383):check('max-valid-'+str(n),canonical.ljust(n,b' '),True)
for n in (16384,16385):check('oversize-'+str(n),canonical.ljust(n,b' '),False)
for c in (0,28,133,160):check('nonseparator-'+str(c),bytes([c]).join(tokens),False)
check('leading-zero-boundary',b'0'*(16383-len(canonical))+canonical,True)
failed=[r for r in rows if not r['pass']];print(json.dumps({'checks':len(rows),'failed':len(failed),'passed':len(rows)-len(failed),'rows':rows}));sys.exit(bool(failed))
