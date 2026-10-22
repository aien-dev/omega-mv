// L592: compile each fault, require normal execution and an exact failing set.
import {spawnSync} from 'node:child_process';
import {mkdirSync,readFileSync,writeFileSync} from 'node:fs';
import {resolve,join} from 'node:path';
const [directory]=process.argv.slice(2);
if(!directory) throw Error('usage: node exit_argument_completion_mutants.mjs FRESH_OUT');
const out=resolve(directory);mkdirSync(out,{mode:0o700});
const source=readFileSync('src/osh/host/osh_builtin.c','utf8');
const blanks="while (*end == ' ' || *end == '\\t') end++;";
const delimiter='if (arg < c->nargv && strcmp(c->argv[arg], "--") == 0) arg++;';
const variants=[
 ['reject-blanks',blanks,'(void)end;', ['space-trailing','space-both','space-extra','tab-trailing','tab-both','tab-extra','delimiter-space-extra','pipeline-first-space','pipeline-last-space'],54],
 ['accept-all-whitespace',blanks,"while (*end == ' ' || (*end >= '\\t' && *end <= '\\r')) end++;",['newline','return','vertical','formfeed'].flatMap(n=>['trailing','both','extra'].map(x=>n+'-'+x)).concat(['mixed-space','mixed-space-extra']),84],
 ['ignore-delimiter',delimiter,'(void)arg;',['delimiter-no-operand','delimiter-valid','delimiter-negative','delimiter-valid-extra','delimiter-space-extra','prior-delimiter','pipeline-first-delimiter','pipeline-last-delimiter','pipeline-first-extra','pipeline-last-extra'],60],
 ['repeat-delimiter',delimiter,delimiter.replace('if (','while ('),['delimiter-again','delimiter-again-extra'],12],
];
for(const [name,from,to,names,count] of variants){
 if(source.split(from).length!==2)throw Error(name+': anchor not unique');
 const path=join(out,name+'.c');writeFileSync(path,source.replace(from,to));
 for(const leg of ['plain','sanitizer']){
  const buildDir=join(out,name+'-'+leg),exe=leg==='plain'?'osh':'osh_numeric_priority_asan';
  const args=['-s','-j2',`OUT_DIR=${buildDir}`,`OSH_BIN_SRCS=$(OSH_DIR)/osh_units.c $(filter-out src/osh/host/osh_builtin.c src/osh/host/osh_codes.c,$(OSH_PROG_HOSTSRCS)) ${path} src/osh/host/osh_codes.c $(OSC_LIB) src/runtime/rx_caproot.c`,leg==='plain'?'osh':join(buildDir,'osh',exe)];
  const build=spawnSync('make',args,{encoding:'utf8',timeout:180000});
  writeFileSync(join(out,name+'-'+leg+'-build.json'),JSON.stringify({command:['make',...args],status:build.status,signal:build.signal,error:build.error?.message??null,stdout:build.stdout,stderr:build.stderr},null,2)+'\n');
  if(build.error||build.signal||build.status!==0)throw Error(name+': compile failure is never a kill');
  const cases=join(out,name+'-'+leg+'-cases');
  const args2=['tests/osh/e2e/exit_argument_completion.mjs',join(buildDir,'osh',exe),cases];
  const r=spawnSync(process.execPath,args2,{encoding:'utf8',timeout:480000});
  writeFileSync(join(out,name+'-'+leg+'-test.log'),(r.stdout??'')+(r.stderr??''));
  writeFileSync(join(out,name+'-'+leg+'-test.json'),JSON.stringify({command:[process.execPath,...args2],status:r.status,signal:r.signal,error:r.error?.message??null})+'\n');
  if(r.error||r.signal||r.status!==1)throw Error(name+': abnormal execution is never a kill');
  const rows=JSON.parse(readFileSync(join(cases,'results.json'))).rows;
  const expected=rows.filter(x=>names.includes(x.name)),failures=rows.filter(x=>!x.ok);
  if(rows.length!==522||expected.length!==count||failures.length!==count||expected.some(x=>x.ok))throw Error(name+': wrong exact failure set; no kill');
  console.log(`PASS ${name}-${leg}: compiled, ${count} exact failures, all other assertions pass`);
 }
}
console.log('EXIT_ARGUMENT_COMPLETION_MUTANTS: 4 variants killed in plain and sanitizer builds');
