// L592 / #344: actual Bash, never agreement between two shared-host cores,
// decides status/stdout/error class. Fresh directories must be private for journals.
import {spawnSync} from 'node:child_process';
import {mkdirSync, writeFileSync, statSync} from 'node:fs';
import {resolve, join} from 'node:path';
const [binary, directory] = process.argv.slice(2);
if (!directory) throw Error('usage: node exit_argument_completion.mjs OSH FRESH_OUT');
const osh = resolve(binary), out = resolve(directory);
mkdirSync(out, {mode:0o700});
if ((statSync(out).mode & 0o777) !== 0o700) throw Error('fixture directory must be0700');
const env = {PATH:'/usr/bin:/bin', LC_ALL:'C', TZ:'UTC', HOME:out, XDG_STATE_HOME:out};
const tail = '; printf "same-list\\n"\nprintf "tail\\n"\n';
const quote = x => "'" + x.replaceAll("'", "'\\''") + "'";
const cases = [];
const add = (name, args, prior='') => cases.push([name, prior+'exit'+(args.length?' '+args.map(quote).join(' '):'')+tail]);
for (const [name,w] of [['space',' '],['tab','\t'],['newline','\n'],['return','\r'],['vertical','\v'],['formfeed','\f']]) {
  add(name+'-leading',[w+'7']); add(name+'-trailing',['7'+w]);
  add(name+'-both',[w+'-7'+w]); add(name+'-only',[w]);
  add(name+'-interior',['7'+w+'8']); add(name+'-extra',['7'+w,'9']);
}
for (const [name,value] of [['empty',''],['plus','+'],['minus','-'],['max','9223372036854775807'],['min','-9223372036854775808'],['overflow','9223372036854775808'],['underflow','-9223372036854775809'],['junk','7x'],['hex','0x10'],['octal','010'],['decimal','7.0'],['sign-gap','+ 7'],['double-sign','--7'],['mixed-space',' \t\n\r\v\f+7\f\v\r\n\t ']]) {
  add(name,[value]); add(name+'-extra',[value,'9']);
}
for (const [name,args] of [['no-operand',[]],['valid',['7']],['negative',['-7']],['empty',['']],['bad',['bad']],['option',['-x']],['again',['--']],['valid-extra',['7','9']],['bad-extra',['bad','9']],['space-extra',['7 ','9']],['empty-extra',['','9']],['again-extra',['--','7']]]) add('delimiter-'+name,['--',...args]);
add('prior-missing',[], 'printf "%s" "" | exit 13\n');
add('prior-delimiter',['--'], 'printf "%s" "" | exit 13\n');
add('missing',[]);
for (const [name,args] of [['space',['7 ']],['delimiter',['--','7']],['extra',['--','7','9']],['bad',['--','bad','9']]]) {
  const exit='exit '+args.map(quote).join(' ');
  cases.push(['pipeline-first-'+name,exit+' | printf "pipe\\n"; printf "status:%s\\n" "$?"\nprintf "tail\\n"\n']);
  cases.push(['pipeline-last-'+name,'printf "%s" "" | '+exit+'; printf "status:%s\\n" "$?"\nprintf "tail\\n"\n']);
}

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
console.log(`EXIT_ARGUMENT_COMPLETION: ${rows.length-failed} passed, ${failed} failed; actual Bash, both cores`);
process.exitCode=failed?1:0;
