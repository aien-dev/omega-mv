import {readFileSync,writeFileSync,existsSync,readdirSync} from 'node:fs';
import {gunzipSync} from 'node:zlib';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
const out=process.argv[2],sha=b=>createHash('sha256').update(b).digest('hex');
const git=(...args)=>{const p=spawnSync('git',args,{maxBuffer:64<<20});if(p.status)throw Error(p.stderr.toString());return p.stdout;};
const check=(v,m)=>{if(!v)throw Error(m);};
const parse=f=>JSON.parse(readFileSync(f)),unzip=f=>JSON.parse(gunzipSync(readFileSync(f)));
const root='evidence/L611-',checks=[];
for(const name of ['ABORT-STATUS-M1','ORCHESTRATION']){
 const dir=root+name;
 const sums=readFileSync(dir+'/SHA256SUMS','utf8').trim().split('\n');
 for(const line of sums){const m=line.match(/^([0-9a-f]{64})  (.*)$/);check(m,'sum syntax');const p=existsSync(m[2])?m[2]:dir+'/'+m[2];check(sha(readFileSync(p))===m[1],'payload '+p);}
 const pins=parse(dir+'/source-pins.json');const ref=name==='ABORT-STATUS-M1'?'26b15c01':'8d2afd87';for(const [p,h]of Object.entries(pins))check(sha(git('show',ref+':'+p))===h,'Git source '+p);
 const raws=parse(dir+(name==='ABORT-STATUS-M1'?'/raw-pins.json':'/pilot-raw-pins.json'));for(const pin of raws){const raw=gunzipSync(readFileSync(dir+'/'+pin.path));check(raw.length===pin.raw_bytes&&sha(raw)===pin.raw_sha256,'raw '+pin.path);}
 checks.push({packet:dir,payloads:sums.length,git_source_pins:Object.keys(pins).length,lossless_raw:raws.length});
}
const runtime=git('diff','--name-only','26b15c01','8d2afd87','--','src/osh');check(runtime.length===0,'runtime changed');
check(git('diff','--name-only','be37dcca','26b15c01','--','src/osh').toString().trim().split('\n').filter(p=>!p.endsWith('/.crumb')).join('\n')==='src/osh/host/osh_shell.c','scope');
let old=git('show','7ccc00f8:evidence/L601-EXIT-REVIEW/review.mjs').toString().split('// Independent arithmetic oracle')[1];
let current=readFileSync('tests/osh/e2e/exit_independent_601.mjs','utf8').split('// Independent arithmetic oracle')[1];
current=current.replace(/const shardParts=.*?const selectedCases=cases.filter\([^\n]*\);\n/s,'').replace('for(const c of selectedCases)','for(const c of cases)').replaceAll('cases:selectedCases.length','cases:cases.length');
check(current===old,'generator/assertion normalization');
const reference=unzip('tests/osh/e2e/fixtures/l601-complete-observed-reference.json.gz');
const observed=unzip(root+'ORCHESTRATION/independent-results.json.gz');
const provenance=parse('tests/osh/e2e/fixtures/l601-reference-provenance.json');
const original=gunzipSync(readFileSync(root+'ABORT-STATUS-M1/raw/green-independent__independent-results.json.gz'));
check(sha(original)===provenance.raw_sha256&&original.length===provenance.raw_bytes,'reference provenance');check(sha(original)===sha(gunzipSync(readFileSync('tests/osh/e2e/fixtures/l601-complete-observed-reference.json.gz'))),'fixture byte identity');
const ec=s=>s.includes('numeric argument required')?'numeric':s.includes('too many arguments')?'count':s===''?'none':'other';
const key=r=>r.key+'|'+r.core,triple=r=>[r.status,r.stdout,ec(r.stderr)];
function verify(rows,leg){
 check(reference.rows.length===1224,'reference rows');const expected=new Map(reference.rows.map(r=>[key(r),r]));check(expected.size===1224,'reference uniqueness');const seen=new Set();
 for(const r of rows){check(!seen.has(key(r)),'duplicate');check(expected.has(key(r)),'unexpected');seen.add(key(r));let ref=expected.get(key(r));
  for(const p of [r.bash,r.got])check(p.status!==null&&Number.isInteger(p.status)&&p.signal===null&&p.error===null&&typeof p.stdout==='string'&&typeof p.stderr==='string'&&!/Sanitizer|runtime error:|journal unavailable/.test(p.stderr),'abnormal');
  check(r.text===ref.text,'text');check(r.leg===leg,'leg');check(JSON.stringify(triple(r.bash))===JSON.stringify(triple(ref.bash)),'Bash');check(r.ok===true&&JSON.stringify(triple(r.got))===JSON.stringify(triple(r.bash))&&ec(r.got.stderr)!=='other','false-ok');
 }
 check(seen.size===expected.size,'missing');return {rows:rows.length,identities:seen.size,programs:new Set(rows.map(r=>r.key.replace(/-(command|script|stdin)-(parent|first|last)$/,''))).size};
}
const coverage=verify(observed.rows,'sanitizer');check(coverage.programs===68,'68 programs');
const controls=[];for(const [name,fn,reason]of [
 ['complete',r=>r,null],['missing',r=>r.slice(1),'missing'],['duplicate',r=>[...r,r[0]],'duplicate'],['text',r=>(r[0].text+='\n',r),'text'],['Bash',r=>(r[0].bash.status=99,r),'Bash'],['false-ok',r=>(r[0].got.status=99,r),'false-ok'],['abnormal',r=>(r[0].got.signal='SIGSEGV',r),'abnormal'],['leg',r=>(r[0].leg='plain',r),'leg'],['sanitizer-diagnostic',r=>(r[0].got.stderr+='\nAddressSanitizer failure',r),'abnormal']]){let actual=null;try{verify(fn(structuredClone(observed.rows)),'sanitizer');}catch(e){actual=e.message;}check(actual===reason,'independent control '+name);controls.push({name,actual,pass:true,scope:'SYNTHETIC replayed observation only'});}
const dispatch=unzip(root+'ORCHESTRATION/dispatch.json.gz');check(dispatch.results.length===4&&dispatch.results.every(r=>r.exit===0),'dispatch exits');check(sha(readFileSync(dispatch.binary))===dispatch.binary_sha256,'pilot binary');
const selected=dispatch.results.map((r,i)=>{check(r.command.slice(0,2).join(' ')==='node tests/osh/e2e/exit_independent_601.mjs','actual Node executor');check(r.command.at(-1)===i+'/4','shard selector');return Array.from({length:68},(_,j)=>j).filter(j=>j%4===i);});check(new Set(selected.flat()).size===68&&selected.flat().length===68,'partition');
const matrices=[];for(const [name,path,flag,expected]of [
 ['red-focused','red-focused__results.json.gz','pass',112],['green-focused','green-focused__results.json.gz','pass',0],['red-independent','red-independent__independent-results.json.gz','ok',12],['green-independent','green-independent__independent-results.json.gz','ok',0]]){const x=unzip(root+'ABORT-STATUS-M1/raw/'+path);let bad=x.rows.filter(r=>!r[flag]);check(bad.length===expected,'matrix '+name);matrices.push({name,rows:x.rows.length,failures:bad.length});}
const report={checks,runtime_byte_identity:true,only_driver_runtime_delta:true,independent_body_sha256:sha(Buffer.from('// Independent arithmetic oracle'+old)),oracle_body_unchanged:true,reference_provenance:true,coverage,synthetic_controls:controls,real_pilot:{dispatch_binary:dispatch.binary,binary_sha256:dispatch.binary_sha256,exits:dispatch.results.map(r=>r.exit),case_indices:selected},matrices};
writeFileSync(out,JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify({packets:checks,coverage,controls:controls.length,matrices,runtime_byte_identity:true,oracle_body_unchanged:true}));
