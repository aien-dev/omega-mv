from pathlib import Path
import hashlib,json,subprocess,importlib.util
root=Path.home()/'workspace/hermes-desk/universal-c-u64-import';out=root/'build/L589/source-regression-red';out.mkdir(exist_ok=False)
spec=importlib.util.spec_from_file_location('cg',root/'tests/universal/c_import_gate.py');g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
cases={'octal':'return 010ULL;','line-splice':'return 1ULL // comment '+chr(92)+'\n +2ULL\n;','trigraph-splice':'return 1ULL // comment ??/\n +2ULL\n;'};rows=[]
for name,body in cases.items():
 src=(g.HEADER+'uint64_t calc(void){'+body+'}\n').encode();c=out/(name+'.c');c.write_bytes(src+b'\n#include <stdio.h>\nint main(void){printf("%llu\\n",(unsigned long long)calc());}\n');binary=out/name
 cc=subprocess.run(['cc','-std=c17','-O2',str(c),'-o',str(binary)],capture_output=True);assert cc.returncode==0,cc.stderr
 native=subprocess.run([str(binary)],capture_output=True);r=subprocess.run([str(root/'build/L589/c-import')],input=src,capture_output=True);assert r.returncode==0,r.stderr
 got=g.evaluate(g.decode(r.stdout,src,0),[]);want=int(native.stdout);assert got!=want,(name,got,want)
 rows.append({'case':name,'source_hex':src.hex(),'native_C':want,'imported_upper':got,'source_sha256':hashlib.sha256(src).hexdigest(),'result':'REPRODUCED_WRONG_SEMANTICS','compiler_stderr':cc.stderr.decode()})
(out/'result.json').write_text(json.dumps(rows,indent=2)+'\n');print(json.dumps(rows,indent=2))
