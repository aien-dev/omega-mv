// L611: actual Bash decides status propagation at an aborted parent list.
import {spawnSync} from 'node:child_process';
import {mkdirSync,writeFileSync,statSync} from 'node:fs';
import {resolve,join} from 'node:path';
const [binaryArg,outArg]=process.argv.slice(2);if(!outArg)throw Error('usage: exit_abort_status.mjs BINARY FRESH_OUT');
const binary=resolve(binaryArg),out=resolve(outArg);mkdirSync(out,{mode:0o700});if((statSync(out).mode&511)!==448)throw Error('0700 HOME required');
const env={PATH:'/usr/bin:/bin',HOME:out,XDG_STATE_HOME:out,LC_ALL:'C',TZ:'UTC'};
const cases=[];
for(const prior of [0,1,2,37,128,255]){
 const seed=`printf "" | exit ${prior}\n`,tail='printf "next:%s\\n" "$?"\n';
 for(const [name,body]of [
  ['count-print','exit 7 9; printf "SKIPPED\\n"\n'+tail],
  ['count-alone','exit 7 9\n'+tail],
  ['count-eof','exit 7 9'],
  ['count-operandless','exit 7 9\nexit\n'],
  ['delimiter','exit -- 7 9\n'+tail],
  ['trailing-blank',"exit '7 ' 9\n"+tail],
  ['reset-zero','printf ""; exit 7 9\n'+tail],
  ['update-same-list',`printf "" | exit ${prior}; exit 7 9\n`+tail],
  ['numeric-invalid','exit bad 9\n'+tail],
  ['valid-exit','exit 7\n'+tail],
  ['no-operand','exit\n'+tail],
  ['delimiter-no-operand','exit --\n'+tail],
  ['pipeline-first','exit 7 9 | printf "pipe\\n"; printf "same:%s\\n" "$?"\n'+tail],
  ['pipeline-last','printf "" | exit 7 9; printf "same:%s\\n" "$?"\n'+tail],
 ])cases.push({name:name+'-prior'+prior,text:seed+body});
}
const rows=[];const errorClass=s=>s.includes('numeric argument required')?'numeric':s.includes('too many arguments')?'count':s===''?'none':'other';
function run(cmd,args,input,extra={}){
 const p=spawnSync(cmd,args,{env:{...env,...extra},input,encoding:'utf8',timeout:20000,maxBuffer:4<<20});
 if(p.error||p.signal||p.status===null||/Sanitizer|runtime error:|journal unavailable/.test(p.stderr??''))throw Error('abnormal test process '+JSON.stringify({status:p.status,signal:p.signal,error:p.error?.message,stderr:p.stderr}));
 return {status:p.status,stdout:p.stdout,stderr:p.stderr,errorClass:errorClass(p.stderr)};
}
for(const c of cases)for(const mode of ['command','script','stdin']){
 const key=c.name+'-'+mode,file=join(out,key+'.sh');writeFileSync(file,c.text);const args=mode==='command'?['-c',c.text]:mode==='script'?[file]:[],input=mode==='stdin'?c.text:undefined;
 const bash=run('/bin/bash',['--noprofile','--norc',...args],input);
 for(const core of ['native','interp']){
  const got=run(binary,['--journal',join(out,key+'-'+core+'.journal'),...args],input,{OSH_INTERP:core==='interp'?'1':'0'});
  const pass=bash.status===got.status&&bash.stdout===got.stdout&&bash.errorClass===got.errorClass&&got.errorClass!=='other';rows.push({name:c.name,mode,core,text:c.text,bash,got,pass});
 }
}
const failed=rows.filter(r=>!r.pass);writeFileSync(join(out,'results.json'),JSON.stringify({programs:cases.length,rows},null,2)+'\n');
console.log(JSON.stringify({programs:cases.length,comparisons:rows.length,passed:rows.length-failed.length,failed:failed.length,failures:failed.map(x=>({name:x.name,mode:x.mode,core:x.core,bash:[x.bash.status,x.bash.stdout],got:[x.got.status,x.got.stdout]}))}));process.exitCode=failed.length?1:0;
