import copy,hashlib,importlib.util,json,pathlib,tempfile,unittest
spec=importlib.util.spec_from_file_location('receipt',pathlib.Path(__file__).with_name('receipt.py'));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class ContractTests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.base=pathlib.Path(self.tmp.name)
  self.fixture={'p1_hex':'00010203','p2_hex':'04050607','keystream_hex':'08090a0b','expected_payload_xor_hex':'04040404'}
  self.a={'id':'fixture','argv':['/pinned/test'],'source_commit':'a'*40,'required_output':['one assertion PASS']}
  self.c={'historical_control':self.fixture,'adapters':[self.a]}
  log=b'one assertion PASS\n';(self.base/'test.log').write_bytes(log)
  self.r={'schema':m.SCHEMA,'contract_sha256':m.digest(json.dumps(self.c,sort_keys=True,separators=(',',':')).encode()),'historical_control':m.control(self.fixture),'overall':'PASS','results':[{'id':'fixture','argv':self.a['argv'],'source_commit':'a'*40,'pins':self.a,'status':'PASS','exit':0,'expected':self.a['required_output'],'observed':'PASS','mutant':'control','tracked_dirty':False,'log':'test.log','log_sha256':m.digest(log)}]}
 def tearDown(self):self.tmp.cleanup()
 def test_valid(self):m.validate(self.c,self.r,self.base)
 def test_expected_tampering_refused(self):
  self.r['results'][0]['expected']=['different expectation']
  with self.assertRaises(ValueError):m.validate(self.c,self.r,self.base)
 def test_observed_tampering_refused(self):
  self.r['results'][0]['observed']='NOT_RUN'
  with self.assertRaises(ValueError):m.validate(self.c,self.r,self.base)
 def test_missing_result_refused(self):
  self.r['results']=[]
  with self.assertRaises(ValueError):m.validate(self.c,self.r,self.base)
 def test_missing_log_refused(self):
  (self.base/'test.log').unlink()
  with self.assertRaises(ValueError):m.validate(self.c,self.r,self.base)
 def test_tampered_log_refused(self):
  (self.base/'test.log').write_text('different')
  with self.assertRaises(ValueError):m.validate(self.c,self.r,self.base)
 def test_pins_refused(self):
  self.r['results'][0]['source_commit']='b'*40
  with self.assertRaises(ValueError):m.validate(self.c,self.r,self.base)
 def test_wrong_oracle_refused(self):
  self.fixture['expected_payload_xor_hex']='00000000'
  with self.assertRaises(ValueError):m.control(self.fixture)
 def test_nonassertion_failure_cannot_pass(self):
  for rc in [1,101,124,-11,None]:
   r=copy.deepcopy(self.r);r['results'][0]['exit']=rc
   with self.assertRaises(ValueError):m.validate(self.c,r,self.base)
 def test_empty_or_skipped_run_refused(self):
  for text in ['running 0 tests\none assertion PASS','1 passed; 1 ignored;\none assertion PASS','0 passed; 0 failed\none assertion PASS']:
   (self.base/'test.log').write_text(text);r=copy.deepcopy(self.r);r['results'][0]['log_sha256']=m.digest(text.encode())
   with self.assertRaises(ValueError):m.validate(self.c,r,self.base)
 def test_duplicate_json_keys_refused(self):
  p=self.base/'duplicate.json';p.write_text('{"a":1,"a":2}')
  with self.assertRaises(ValueError):m.load(p)
if __name__=='__main__':unittest.main()
