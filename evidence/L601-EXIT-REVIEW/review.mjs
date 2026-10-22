import {readFileSync as read,writeFileSync as write,mkdirSync as mkdir,statSync} from 'node:fs';
import {spawnSync as spawn} from 'node:child_process';
import {createHash} from 'node:crypto';
import {resolve,join} from 'node:path';
const root=resolve('targets/L601'), pub=resolve('evidence/L601-EXIT-REVIEW');
const sha=b=>createHash('sha256').update(b).digest('hex');
const commands=[];
function run(cmd,args,opts={}){const r=spawn(cmd,args,{encoding:'utf8',timeout:20000,maxBuffer:8<<20,...opts});const x={command:[cmd,...args],status:r.status,signal:r.signal,error:r.error?.message??null,stdout:r.stdout,stderr:r.stderr};commands.push(x);if(r.error||r.signal||r.status===null||/Sanitizer|runtime error:|journal unavailable/.test(r.stderr??''))throw Error(JSON.stringify(x));return x;}
function assert(x,msg){if(!x)throw Error(msg);}
const loc=JSON.parse(read('evidence/L592-EXIT-ARGUMENT-COMPLETION/artifact-locations.json'));
const pins=JSON.parse(read('evidence/L592-EXIT-ARGUMENT-COMPLETION/binary-pins.json'));
const binaries=[];
for(const [p,pin]of Object.entries(pins)){let q=p.replace(loc.executionRoot,loc.retainedRoot);assert(sha(read(q))===pin.sha256,'binary '+q);if(q.startsWith(loc.retainedRoot+'/green/osh/')&&!q.endsWith('quietlock'))binaries.push(q);}
const srcpins=JSON.parse(read('evidence/L592-EXIT-ARGUMENT-COMPLETION/source-pins.json'));
for(const [p,h]of Object.entries(srcpins.files)){if(!p.endsWith('/.crumb'))assert(sha(read(p))===h,'current source '+p);}
write(join(root,'closure.json'),JSON.stringify({binaryPinsVerified:Object.keys(pins).length,sourcePinsVerified:Object.keys(srcpins.files).length,binaries},null,2));
assert(binaries.length===2,'plain and sanitizer binaries');
// Independent arithmetic oracle uses BigInt, not libc conversion.
const numbers=[-9223372036854775808n,-9223372036854775807n,-65537n,-513n,-512n,-511n,-257n,-256n,-255n,-129n,-128n,-1n,0n,1n,127n,128n,255n,256n,257n,511n,512n,65537n,9223372036854775806n,9223372036854775807n];
const cases=[];for(const n of numbers){const s=n.toString();cases.push({name:'arith'+s,args:[s],status:Number((n%256n+256n)%256n),kind:'none'});}
for(const v of ['000000000000000000000000000000000000000000000000000000000000000001','-000000000000000000000000000000000000000000000000000000000000000257','+000256','\t \r\n\v\f-513 \t \t'])cases.push({name:'padded'+cases.length,args:[v],status:v.includes('-513')?255:v.includes('-000')?255:v.includes('256')?0:1,kind:'none'});
for(const v of ['92233720368547758080','-92233720368547758080','18446744073709551615','-18446744073709551616','+','-','', ' \t ','7\r ','7\n\t','7\v ','7\f\t','\u00a07','7\u00a0','++1','+-1','1e2'])for(const extra of [false,true])cases.push({name:'invalid'+cases.length,args:[v,...extra?['2','3']:[]],status:2,kind:'numeric'});
for(const v of ['+000256','-000257','\t-513 \t'])cases.push({name:'count'+cases.length,args:['--',v,'junk','4'],status:1,kind:'count'});
cases.push({name:'repeat',args:['--','--','--','7'],status:2,kind:'numeric'},{name:'noarg',args:[],status:37,kind:'none'},{name:'delimiter-only',args:['--'],status:37,kind:'none'});
const quote=x=>"'"+x.replaceAll("'","'\\''")+"'";
const ec=s=>s.includes('numeric argument required')?'numeric':s.includes('too many arguments')?'count':s===''?'none':'other';
const rows=[];
for(const binary of binaries){const leg=binary.endsWith('_asan')?'sanitizer':'plain';const out=join(root,'independent-'+leg);mkdir(out,{mode:0o700});assert((statSync(out).mode&511)===448,'0700');const env={PATH:'/usr/bin:/bin',HOME:out,XDG_STATE_HOME:out,LC_ALL:'C',TZ:'UTC'};
for(const c of cases)for(const mode of ['command','script','stdin'])for(const position of ['parent','first','last']){
 const exit='exit '+c.args.map(quote).join(' ');let text='printf "" | exit 37\n';
 text+=(position==='first'?exit+' | printf "pipe\\n"':position==='last'?'printf "" | '+exit:exit)+'; printf "same:%s\\n" "$?"\nprintf "next:%s\\n" "$?"\n';
 const key=[c.name,mode,position].join('-'),file=join(out,key+'.sh');write(file,text);
 const args=mode==='command'?['-c',text]:mode==='script'?[file]:[];const input=mode==='stdin'?text:undefined;
 const bash=run('/bin/bash',['--noprofile','--norc',...args],{env,input});
 if(position==='parent')assert(bash.status===(c.kind==='count'&&mode!=='command'?0:c.status)&&ec(bash.stderr)===c.kind,'independent explicit oracle '+key);
 for(const core of ['native','interp']){const got=run(binary,['--journal',join(out,key+'-'+core+'.journal'),...args],{env:{...env,OSH_INTERP:core==='interp'?'1':'0'},input});const ok=got.status===bash.status&&got.stdout===bash.stdout&&ec(got.stderr)===ec(bash.stderr)&&ec(got.stderr)!=='other';rows.push({leg,key,core,text,bash,got,ok});}
}}
write(join(root,'independent-results.json'),JSON.stringify({cases:cases.length,rows},null,2));
write(join(root,'independent-commands.json'),JSON.stringify(commands,null,2));
const failed=rows.filter(x=>!x.ok);console.log(JSON.stringify({cases:cases.length,plain:rows.filter(x=>x.leg==='plain').length,sanitizer:rows.filter(x=>x.leg==='sanitizer').length,failed:failed.length}));assert(failed.length===0,JSON.stringify(failed.slice(0,3)));
