import pathlib,sys,subprocess,re,json,hashlib
w=pathlib.Path(sys.argv[1]).resolve();p=w/'evidence/OSC-MANUAL-MIGRATION';oracle=json.loads((p/'gate6-wolfram.json').read_text())['result'];t=w/'seed/omc/tests/struct'
for filename,key in [('t1x.om','originalSHA256'),('t1x-swap.om','mutantSHA256')]:assert hashlib.sha256((t/filename).read_bytes()).hexdigest()==oracle[key]
assert (t/'t1x.om').read_bytes().count(b'get p x')==1
assert (t/'t1x-swap.om').read_bytes()==(t/'t1x.om').read_bytes().replace(b'get p x',b'get p y')
old=subprocess.check_output(['git','-C',str(w),'show','cd4436e8ffd5fc0b61d355cd6686f23a9f97472b:seed/omc/gate6.sh'],text=True);new=(w/'seed/omc/gate6.sh').read_text()
pattern=r'^(run_exit|refuse) (\w+) .* (\d+)$';expected=re.findall(pattern,old,re.M);assert re.findall(pattern,new,re.M)==expected
assert len([x for x in expected if x[0]=='run_exit'])==oracle['positiveCases'];assert len([x for x in expected if x[0]=='refuse'])==oracle['refusalCases']
assert {x.name for x in (t/'expected').iterdir()}=={name+'.txt' for kind,name,code in expected if kind=='refuse'}
for row in json.loads((p/'gate6-diagnostic-baseline.json').read_text()):
 assert (t/'expected'/(row['name']+'.txt')).read_text()==row['diagnostic'];assert hashlib.sha256((t/(row['name']+'.om')).read_bytes()).hexdigest()==row['source_sha256']
r=subprocess.run(['sh','seed/omc/gate6.sh'],cwd=w,capture_output=True,text=True,timeout=60);assert r.returncode==0,(r.stdout,r.stderr)
actual=re.findall(r'^PASS (\w+) (exit|error) (\d+)$',r.stdout,re.M);assert actual==[(name,'exit' if kind=='run_exit' else 'error',code) for kind,name,code in expected]
assert 'PASS mut field-swap exit '+str(oracle['mutantExit']) in r.stdout
assert 'PASS gate6 '+str(oracle['totalCases'])+' cases\nRC=0' in r.stdout
print(r.stdout,end='');print(json.dumps({'source_calls_preserved':len(expected),'mutation_cases':oracle['mutationCases'],'executed_cases':len(actual)+1,'wolfram_fixture_hashes_match':True,'status':'PASS'}))
