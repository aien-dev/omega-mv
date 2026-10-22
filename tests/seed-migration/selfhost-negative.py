import pathlib,tempfile,subprocess,json,sys
w=pathlib.Path(sys.argv[1]);parent=w/'tests/seed-migration';script=(parent/'native-closure.sh').read_text()
needle='"$D/equal" 3< "$D/stage2" 4< "$D/stage3"'
assert script.count(needle)==1
# Deliberately append one byte after stage3 ELF inspection, before its comparison.
changed=script.replace(needle,'printf x >> "$D/stage3"\n'+needle)
with tempfile.NamedTemporaryFile(mode='w',suffix='.sh',prefix='negative-',dir=parent) as f:
 f.write(changed);f.flush();r=subprocess.run(['sh',f.name],capture_output=True,text=True,timeout=30)
assert r.returncode==1,(r.returncode,r.stdout,r.stderr)
assert 'PASS Omega binary comparison' not in r.stdout
print(json.dumps({'case':'stage3-appended-byte','expected_exit':1,'actual_exit':r.returncode,'stdout':r.stdout,'stderr':r.stderr,'result':'PASS refusal'},indent=2))
