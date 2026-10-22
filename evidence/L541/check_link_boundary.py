"""Expected RED on L538: static linked ancestor must refuse per stated boundary."""
import io,json,pathlib,subprocess,sys,tarfile
repo=pathlib.Path(sys.argv[1]).resolve();out=pathlib.Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=False)
pin='1e4cdbe91b444efe51af4e745f366e07d54de030';export=out/'export';export.mkdir()
raw=subprocess.check_output(['git','-C',str(repo),'archive',pin,'tests/compiler/progs','tests/compiler/slices','src/osh','tests/physics0/osc','tests/compiler/test_osc_ir_decode.c'])
with tarfile.open(fileobj=io.BytesIO(raw)) as archive:archive.extractall(export,filter='data')
cmd=[sys.executable,str(repo/'tests/compiler/verify_decoder_corpus.py'),str(export),str(repo)]
def invoke():
 p=subprocess.run(cmd,capture_output=True,text=True,timeout=5)
 return p,{'exit':p.returncode,'stderr':p.stderr,'programs':json.loads(p.stdout)['corpus_programs'] if p.returncode==0 else None}
_,ordinary=invoke()
parent=export/'tests/compiler';target=out/'outside-compiler';parent.rename(target);parent.symlink_to(target.resolve(),target_is_directory=True)
p,linked=invoke()
f=next((target/'progs').glob('*.osc'));data=f.read_bytes();f.write_bytes(data+b'\n// changed\n');_,changed=invoke();f.write_bytes(data)
report={'pin':pin,'ordinary':ordinary,'linked_ancestor':linked,'changed_link_target_negative_control':changed,'expected_link_exit':1,'actual_link_exit':p.returncode,'boundary_pass':p.returncode==1,'scope':'static link placement only; no concurrent writer and no wrong-byte acceptance'}
print(json.dumps(report,indent=2));sys.exit(0 if report['boundary_pass'] else 1)
