import {spawnSync} from 'node:child_process';
import {readFileSync,writeFileSync,mkdirSync,statSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {join,resolve} from 'node:path';
const out=resolve(process.argv[2]);mkdirSync(out,{mode:0o700});
if((statSync(out).mode&511)!==448)throw Error('private HOME required');
const versions={old:'/home/drakestapleton/workspace/targets/L592/green/osh/osh',fixed:'/home/drakestapleton/workspace/targets/L611/green/osh/osh',asan:'/home/drakestapleton/workspace/targets/L611/green/osh/osh_numeric_priority_asan'};
const published=JSON.parse(readFileSync('evidence/L611-ABORT-STATUS-M1/binary-pins.json'));
const sha=b=>createHash('sha256').update(b).digest('hex');
for(const p of Object.values(versions))if(sha(readFileSync(p))!==published[p].sha256)throw Error('binary pin');
const seed='printf "" | exit 37\n', print='printf "seen:%s\\n" "$?"\n';
const cases=[
 ['F1-print',seed+'exit 7 9; printf "LEAK\\n"\n'+print],
 ['F1-exit',seed+'exit 7 9\nexit\n'],
 ['zero','exit 7 9\n'+print],
 ['same-line-reset',seed+'printf ""; exit 7 9\n'+print],
 ['same-line-update','printf "" | exit 12; printf "" | exit 37; exit 7 9\n'+print],
 ['same-line-error',seed+'cd /L705-path-that-does-not-exist; exit 7 9\n'+print],
 ['consecutive',seed+'exit 7 9\nexit 8 10\n'+print],
 ['consecutive-reset',seed+'exit 7 9\nprintf ""\nexit 8 10\n'+print],
 ['eof',seed+'exit 7 9'],
 ['numeric',seed+'exit bad 9\n'+print],
 ['child-first',seed+'exit 7 9 | printf "child\\n"; printf "same:%s\\n" "$?"\n'+print],
 ['child-last',seed+'printf "" | exit 7 9; printf "same:%s\\n" "$?"\n'+print],
 ['and',seed+'printf "" && exit 7 9; printf "LEAK\\n"\n'+print],
 ['or','printf "" | exit 37 || exit 7 9; printf "LEAK\\n"\n'+print],
 ['blank',seed+'exit 7 9\n\n# retained status\n'+print],
 ['later-valid',seed+'exit 7 9\nexit 19\n'],
 ['later-numeric',seed+'exit 7 9\nexit bad 9\n'+print],
 ['connector-resume',seed+'exit 7 9\nprintf "" | exit 0 && printf "ok:%s\\n" "$?"\n']
].map(([name,text])=>({name,text}));
const env={PATH:'/usr/bin:/bin',HOME:out,XDG_STATE_HOME:out,LC_ALL:'C',TZ:'UTC'};
const ec=s=>[...s.matchAll(/numeric argument required|too many arguments/g)].map(x=>x[0]).join('|')+(s.includes('L705-path-that-does-not-exist')?'|missing-path':'');
const rows=[];function run(exe,args,input){let p=spawnSync(exe,args,{env,input,encoding:'utf8',timeout:20000,maxBuffer:1<<20});if(p.error||p.signal||p.status===null||/Sanitizer|runtime error:|journal unavailable/.test(p.stderr??''))throw Error('abnormal '+JSON.stringify({exe,args,status:p.status,signal:p.signal,error:p.error?.message,stderr:p.stderr}));return {command:[exe,...args],status:p.status,signal:p.signal,error:null,stdout:p.stdout,stderr:p.stderr};}
for(const c of cases)for(const mode of ['command','script','stdin']){
 let file=join(out,c.name+'.sh');writeFileSync(file,c.text);let args=mode==='command'?['-c',c.text]:mode==='script'?[file]:[],input=mode==='stdin'?c.text:undefined;
 const bash=run('/bin/bash',['--noprofile','--norc',...args],input);
 for(const [version,binary]of Object.entries(versions))for(const core of ['native','interp']){
  env.OSH_INTERP=core==='interp'?'1':'0';const got=run(binary,['--journal',join(out,[c.name,mode,version,core].join('-')+'.journal'),...args],input);
  rows.push({name:c.name,text:c.text,mode,version,core,bash,got,pass:got.status===bash.status&&got.stdout===bash.stdout&&ec(got.stderr)===ec(bash.stderr)});
 }
}
writeFileSync(join(out,'results.json'),JSON.stringify({cases,rows},null,2)+'\n');const summary=Object.fromEntries(Object.keys(versions).map(v=>{let a=rows.filter(r=>r.version===v);return [v,{comparisons:a.length,failed:a.filter(r=>!r.pass).map(r=>[r.name,r.mode,r.core])}];}));writeFileSync(join(out,'summary.json'),JSON.stringify(summary,null,2)+'\n');console.log(JSON.stringify(summary));
if(summary.fixed.failed.length||summary.asan.failed.length||!summary.old.failed.length)process.exitCode=1;
