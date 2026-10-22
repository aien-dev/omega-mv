// #344: actual bash is the independent behavioral oracle. Native/interpreter
// agreement alone is insufficient, since both share this host driver loop.
import {spawnSync} from 'node:child_process';
import {mkdirSync, writeFileSync, readFileSync} from 'node:fs';
import {resolve, join} from 'node:path';
const [oshArg, outArg] = process.argv.slice(2);
if (!outArg) throw Error('usage: node exit_boundary_controls.mjs OSH FRESH_OUT');
const osh = resolve(oshArg), out = resolve(outArg);
mkdirSync(out, {mode:0o700});
const bash = '/bin/bash';
const env = {PATH:'/usr/bin:/bin', LC_ALL:'C', TZ:'UTC', HOME:out, XDG_STATE_HOME:out};
let pass = 0, fail = 0;
const cases = [
  ['too-many', 'exit 1 2; printf "after\\n"\nprintf "next\\n"', ['',1], ['next\n',0]],
  ['prefixed', 'printf "before\\n"\nexit 1 2; printf "after\\n"\nprintf "next\\n"', ['before\n',1], ['before\nnext\n',0]],
  ['invalid-number', 'exit bad; printf "after\\n"\nprintf "next\\n"', ['',2], ['',2]],
  ['valid-exit', 'exit 7; printf "after\\n"\nprintf "next\\n"', ['',7], ['',7]],
  ['ordinary', 'printf "before\\n"\nprintf "next\\n"', ['before\nnext\n',0], ['before\nnext\n',0]],
  ['same-line', 'exit 1 2; printf "after\\n"', ['',1], ['',1]],
];
function run(name, program, args, input, runEnv) {
  const r = spawnSync(program, args, {env:runEnv, input, encoding:'utf8', timeout:20000, maxBuffer:1<<20});
  const record = {status:r.status,signal:r.signal,error:r.error?.message ?? null,stdout:r.stdout,stderr:r.stderr};
  writeFileSync(join(out, `${name}.json`), JSON.stringify(record,null,2)+'\n');
  if (r.error || r.signal || r.status === null || /Sanitizer|runtime error:/.test(r.stderr ?? '')) throw Error(`${name}: infrastructure/sanitizer failure, never a behavioral kill`);
  return r;
}
for (const [name, text, commandExpected, streamExpected] of cases) {
  for (const mode of ['command','script','stdin']) {
    const key = `${name}-${mode}`;
    const path = join(out, `${key}.sh`);
    writeFileSync(path, text);
    const args = mode === 'command' ? ['-c',text] : mode === 'script' ? [path] : [];
    const input = mode === 'stdin' ? text : undefined;
    const oracle = run(`${key}-bash`, bash, ['--noprofile','--norc',...args], input, env);
    const [wantOut,wantStatus] = mode === 'command' ? commandExpected : streamExpected;
    if (oracle.stdout !== wantOut || oracle.status !== wantStatus) throw Error(`${key}: actual bash disagrees with predeclared criterion`);
    for (const engine of ['native','interp']) {
      const r = run(`${key}-${engine}`, osh, ['--journal',join(out,`${key}-${engine}.journal`),...args], input,
        {...env, OSH_INTERP:engine === 'interp' ? '1' : '0'});
      const ok = r.status === oracle.status && r.stdout === oracle.stdout &&
        (!['too-many','prefixed','same-line'].includes(name) || r.stderr.includes('exit: too many arguments')) &&
        (name !== 'invalid-number' || r.stderr.includes('numeric argument required'));
      console.log(`${ok?'PASS':'FAIL'} ${key}-${engine}: exit=${r.status}, stdout=${JSON.stringify(r.stdout)}`);
      if(ok) pass++; else fail++;
    }
  }
}
writeFileSync(join(out,'oracle-environment.json'), JSON.stringify({bash,version:spawnSync(bash,['--version'],{encoding:'utf8',env}).stdout.split('\n')[0],env,osh},null,2)+'\n');
console.log(`EXIT_BOUNDARY: ${pass} passed, ${fail} failed; actual bash reference, both Omega modes`);
process.exitCode=fail?1:0;
