import {readFileSync as read,writeFileSync as write} from 'node:fs';import {spawnSync as spawn} from 'node:child_process';import {resolve,join} from 'node:path';
const root=resolve('targets/L601'), raw=join(root,'author592/raw');
const json=p=>JSON.parse(read(p));const ec=s=>s.includes('numeric argument required')?'numeric':s.includes('too many arguments')?'count':s===''?'none':'other';
function check(p,count,fail){const j=json(p),rows=j.rows; if(rows.length!==count||rows.filter(x=>!x.ok).length!==fail)throw Error(p);for(const x of rows){const ok=x.bash.status===x.got.status&&x.bash.stdout===x.got.stdout&&ec(x.bash.stderr)===ec(x.got.stderr)&&ec(x.got.stderr)!=='other';if(ok!==x.ok||x.got.signal||x.bash.signal||x.got.error||x.bash.error||/Sanitizer|runtime error:|journal unavailable/.test(x.got.stderr))throw Error('bad assertion '+p);}return {path:p,rows:rows.length,failures:fail,assertionsRecomputed:true};}
const audit=[];
for(const leg of ['plain','asan'])audit.push(check(join(raw,'green-'+leg+'-cases/results.json'),522,0));
for(const leg of ['plain','sanitizer'])for(const [group,name,count,fail]of [['numeric-mutants','count-first',168,60],['numeric-mutants','invalid-extra-continues',168,48],['new-mutants','reject-blanks',522,54],['new-mutants','accept-all-whitespace',522,84],['new-mutants','ignore-delimiter',522,60],['new-mutants','repeat-delimiter',522,12]]){
 const prefix=join(raw,group,name+'-'+leg);const build=json(prefix+'-build.json'),test=json(prefix+'-test.json');if(build.status!==0||build.signal||test.status!==1||test.signal||test.error)throw Error('bad control status');audit.push(check(prefix+'-cases/results.json',count,fail));
}
// Selected actual replays preserve the original corpus and require exact named failing sets.
for(const leg of ['plain','sanitizer'])for(const [group,name,script,count,fail,names]of [
 ['numeric-mutants','count-first','exit_numeric_priority.mjs',168,60,['bad-extra','overflow-extra','empty-extra','invalid-extra','invalid-extra-pipeline']],
 ['new-mutants','repeat-delimiter','exit_argument_completion.mjs',522,12,['delimiter-again','delimiter-again-extra']]]){
 const exe='/home/drakestapleton/workspace/targets/L592/raw/'+group+'/'+name+'-'+leg+'/osh/'+(leg==='plain'?'osh':'osh_numeric_priority_asan');
 const directory=join(root,'replay-'+name+'-'+leg),args=['tests/osh/e2e/'+script,exe,directory];
 const r=spawn(process.execPath,args,{encoding:'utf8',timeout:480000,maxBuffer:8<<20});write(join(root,'replay-'+name+'-'+leg+'.log'),r.stdout+r.stderr);write(join(root,'replay-'+name+'-'+leg+'.command.json'),JSON.stringify({command:[process.execPath,...args],status:r.status,signal:r.signal,error:r.error?.message??null}));if(r.status!==1||r.signal||r.error)throw Error('replay not normal assertion exit1');
 const result=check(join(directory,'results.json'),count,fail),got=json(join(directory,'results.json')).rows.filter(x=>!x.ok);
 // Exact identity is independently tied to the published, fully recomputed rows.
 const historical=json(join(raw,group,name+'-'+leg+'-cases/results.json')).rows.filter(x=>!x.ok);
 const key=x=>[x.name,x.mode,x.engine].join('/');if(JSON.stringify(got.map(key))!==JSON.stringify(historical.map(key)))throw Error('wrong failure identities');audit.push({...result,replay:true,processStatus:r.status});console.log(name+' '+leg+' exact failures '+fail);
}
write(join(root,'author-audit.json'),JSON.stringify(audit,null,2));console.log('AUTHOR_AUDIT_PASS');
