import{readFileSync as read,writeFileSync as write,mkdirSync as mkdir}from'node:fs';import{spawnSync as spawn}from'node:child_process';import{createHash}from'node:crypto';import{resolve,join}from'node:path';
const out=resolve('targets/L601/finding-repro');mkdir(out,{mode:0o700});const env={PATH:'/usr/bin:/bin',LC_ALL:'C',TZ:'UTC',HOME:out,XDG_STATE_HOME:out};
const source={baseline:'/home/drakestapleton/workspace/targets/L577/red/osh/',candidate:'/home/drakestapleton/workspace/targets/L592/green/osh/'};
const pins=JSON.parse(read('evidence/L577-EXIT-NUMERIC-PRIORITY/binary-pins.json'));for(const suffix of ['osh','osh_numeric_priority_asan']){const p=source.baseline+suffix;if(createHash('sha256').update(read(p)).digest('hex')!==pins[p].sha256)throw Error('baseline ELF');}
const programs=[['minimal','exit 7 9; printf x\nprintf "%s\\n" "$?"\n'],['alone','exit 7 9\nprintf "%s\\n" "$?"\n'],['delimiter','exit -- 7 9; printf x\nprintf "%s\\n" "$?"\n'],['prior','printf "" | exit 37\nexit 7 9; printf x\nprintf "%s\\n" "$?"\n'],['operandless','exit 7 9\nexit\n']];const rows=[];
for(const [name,text]of programs)for(const mode of ['command','script','stdin']){
 const path=join(out,name+'-'+mode+'.sh');write(path,text);const args=mode==='command'?['-c',text]:mode==='script'?[path]:[],input=mode==='stdin'?text:undefined;
 function run(cmd,argv,env2){const r=spawn(cmd,argv,{env:env2,input,encoding:'utf8',timeout:20000});const row={command:[cmd,...argv],status:r.status,signal:r.signal,error:r.error?.message??null,stdout:r.stdout,stderr:r.stderr};if(r.error||r.signal||/Sanitizer|runtime error:|journal unavailable/.test(r.stderr))throw Error('abnormal run');return row;}
 const bash=run('/bin/bash',['--noprofile','--norc',...args],env);
 for(const [version,prefix]of Object.entries(source))for(const leg of ['plain','sanitizer'])for(const core of ['native','interp']){
  const binary=prefix+(leg==='plain'?'osh':'osh_numeric_priority_asan');const got=run(binary,['--journal',join(out,[name,mode,version,leg,core].join('-')+'.journal'),...args],{...env,OSH_INTERP:core==='interp'?'1':'0'});
  const mismatch=got.status!==bash.status||got.stdout!==bash.stdout;
  rows.push({name,text,mode,version,leg,core,bash,got,mismatch});
 }
}
write(resolve('targets/L601/finding-repro.json'),JSON.stringify({rows},null,2));console.log(JSON.stringify({rows:rows.length,mismatches:rows.filter(x=>x.mismatch).length,baselineMismatch:rows.filter(x=>x.mismatch&&x.version==='baseline').length,candidateMismatch:rows.filter(x=>x.mismatch&&x.version==='candidate').length,conclusion:'record actual list-shape boundary; preexisting count-error continuation mismatch'}));
