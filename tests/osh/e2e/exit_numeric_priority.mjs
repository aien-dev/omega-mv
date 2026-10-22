// L577 / #344: actual Bash, never agreement between two shared-host cores,
// decides status/stdout/error class. Fresh directories must be private for journals.
import {spawnSync} from 'node:child_process';
import {mkdirSync, writeFileSync, statSync} from 'node:fs';
import {resolve, join} from 'node:path';
const [binary, directory] = process.argv.slice(2);
if (!directory) throw Error('usage: node exit_numeric_priority.mjs OSH FRESH_OUT');
const osh = resolve(binary), out = resolve(directory);
mkdirSync(out, {mode:0o700});
if ((statSync(out).mode & 0o777) !== 0o700) throw Error('fixture directory must be0700');
const env = {PATH:'/usr/bin:/bin', LC_ALL:'C', TZ:'UTC', HOME:out, XDG_STATE_HOME:out};
const tail = '; printf "same-list\\n"\nprintf "tail\\n"\n';
const cases = [
  ['bad-extra', 'exit bad 9'+tail],
  ['empty-extra', "exit '' 9"+tail],
  ['plus-only-extra', 'exit + 9'+tail],
  ['minus-only-extra', 'exit - 9'+tail],
  ['overflow-positive-extra', 'exit 9223372036854775808 9'+tail],
  ['overflow-negative-extra', 'exit -9223372036854775809 9'+tail],
  ['signed-bad-extra', 'exit +bad 9'+tail],
  ['many-invalid-extra', 'exit bad 9 10'+tail],
  ['bad-single', 'exit bad'+tail],
  ['empty-single', "exit ''"+tail],
  ['overflow-single', 'exit 9223372036854775808'+tail],
  ['valid-extra', 'exit 1 2'+tail],
  ['positive-extra', 'exit +7 9'+tail],
  ['negative-extra', 'exit -7 9'+tail],
  ['max-extra', 'exit 9223372036854775807 9'+tail],
  ['min-extra', 'exit -9223372036854775808 9'+tail],
  ['valid', 'exit 7'+tail],
  ['positive', 'exit +7'+tail],
  ['negative', 'exit -7'+tail],
  ['max', 'exit 9223372036854775807'+tail],
  ['min', 'exit -9223372036854775808'+tail],
  ['modulo', 'exit 256'+tail],
  ['missing', 'printf "%s" ""\nexit'+tail],
  ['ordinary', 'printf "before\\n"\nprintf "tail\\n"\n'],
  ['pipeline-first-invalid', 'exit bad 9 | printf "pipe\\n"; printf "status:%s\\n" "$?"\nprintf "tail\\n"\n'],
  ['pipeline-last-invalid', 'printf "%s" "" | exit bad 9; printf "status:%s\\n" "$?"\nprintf "tail\\n"\n'],
  ['pipeline-last-valid-extra', 'printf "%s" "" | exit 7 9; printf "status:%s\\n" "$?"\nprintf "tail\\n"\n'],
  ['pipeline-last-valid', 'printf "%s" "" | exit 7; printf "status:%s\\n" "$?"\nprintf "tail\\n"\n'],
];
function errorClass(stderr) {
  if (stderr.includes('numeric argument required')) return 'numeric';
  if (stderr.includes('too many arguments')) return 'count';
  return stderr === '' ? 'none' : 'other';
}
function run(key, program, args, input, runEnv) {
  const r = spawnSync(program,args,{env:runEnv,input,encoding:'utf8',timeout:20000,maxBuffer:1<<20});
  const row = {command:[program,...args],status:r.status,signal:r.signal,error:r.error?.message??null,stdout:r.stdout,stderr:r.stderr};
  writeFileSync(join(out,key+'.json'),JSON.stringify(row,null,2)+'\n');
  if (r.error || r.signal || r.status === null || /Sanitizer|runtime error:|journal unavailable/.test(r.stderr??'')) throw Error(key+': infrastructure/sanitizer/refusal failure is never a behavioral kill');
  return {...row,errorClass:errorClass(r.stderr)};
}
const rows=[];
for (const [name,text] of cases) for (const mode of ['command','script','stdin']) {
  const key=name+'-'+mode, path=join(out,key+'.sh');
  writeFileSync(path,text);
  const args=mode==='command'?['-c',text]:mode==='script'?[path]:[];
  const input=mode==='stdin'?text:undefined;
  const bash=run(key+'-bash','/bin/bash',['--noprofile','--norc',...args],input,env);
  for (const engine of ['native','interp']) {
    const got=run(key+'-'+engine,osh,['--journal',join(out,key+'-'+engine+'.journal'),...args],input,{...env,OSH_INTERP:engine==='interp'?'1':'0'});
    const ok=got.status===bash.status && got.stdout===bash.stdout && got.errorClass===bash.errorClass && got.errorClass!=='other';
    rows.push({name,mode,engine,ok,bash,got});
    console.log(`${ok?'PASS':'FAIL'} ${key}-${engine}: status=${got.status}/${bash.status} error=${got.errorClass}/${bash.errorClass} stdout=${JSON.stringify(got.stdout)}/${JSON.stringify(bash.stdout)}`);
  }
}
const failed=rows.filter(r=>!r.ok).length;
writeFileSync(join(out,'results.json'),JSON.stringify({osh,env,bashVersion:spawnSync('/bin/bash',['--version'],{env,encoding:'utf8'}).stdout,rows},null,2)+'\n');
console.log(`EXIT_NUMERIC_PRIORITY: ${rows.length-failed} passed, ${failed} failed; actual Bash, both cores`);
process.exitCode=failed?1:0;
