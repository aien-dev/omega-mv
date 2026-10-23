// L611: successfully compiled semantic controls must fail exact Bash cases.
import {spawnSync} from 'node:child_process';
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {resolve,join} from 'node:path';
const [outArg,referenceArg]=process.argv.slice(2);if(!referenceArg)throw Error('usage: exit_abort_status_mutants.mjs FRESH_OUT GREEN_RESULTS_JSON');
const out=resolve(outArg);mkdirSync(out,{mode:0o700});const reference=JSON.parse(readFileSync(resolve(referenceArg))).rows;if(reference.length!==504||reference.some(r=>!r.pass))throw Error('complete green reference required');
const source=readFileSync('src/osh/host/osh_shell.c','utf8');
const condition='if (resume_lists && sh->s.abort_list && prior_status != 0)';
const entry='static int run_list(OshShell *sh, int resume_lists)\n{\n    uint64_t *w = sh->w;';
function replace(text,from,to){if(text.split(from).length!==2)throw Error('nonunique source hook');return text.replace(from,to);}
const parentNames=new Set(['count-print','count-alone','count-eof','count-operandless','delimiter','trailing-blank','update-same-list']);
function kind(r){const m=r.name.match(/^(.*)-prior([0-9]+)$/);if(!m)throw Error('case identity');return {name:m[1],prior:Number(m[2])};}
const nonzero=(r)=>{const k=kind(r);return parentNames.has(k.name)&&k.prior!==0&&k.prior!==1;};
const variants=[
 ['no-restore',replace(source,condition,condition.replace(' != 0)',' != 0 && 0)')),r=>r.mode!=='command'&&nonzero(r)],
 ['restore-zero',replace(source,condition,'if (resume_lists && sh->s.abort_list)'),r=>{const k=kind(r);return r.mode!=='command'&&(k.name==='reset-zero'||(parentNames.has(k.name)&&k.prior===0));}],
 ['restore-command',replace(source,'run_list(sh, !src->exact_end)','run_list(sh, 1)'),r=>r.mode==='command'&&nonzero(r)],
 ['line-start-snapshot',replace(replace(source,entry,entry+'\n    const int list_status = sh->s.last_status;'),'int prior_status = sh->s.last_status;','int prior_status = list_status;'),r=>{const k=kind(r);return r.mode!=='command'&&k.name==='reset-zero'&&k.prior!==0&&k.prior!==1;}],
];
const reports=[];const key=r=>[r.name,r.mode,r.core].join('|');
for(const [name,text,select]of variants){
 const file=join(out,name+'.c');writeFileSync(file,text);const expected=reference.filter(select).map(key).sort();if(!expected.length)throw Error('empty negative control');
 for(const leg of ['plain','sanitizer']){
  const build=join(out,name+'-'+leg),exe=leg==='plain'?'osh':'osh_numeric_priority_asan';
  const args=['--no-print-directory','-s','-j2',`OUT_DIR=${build}`,`OSH_BIN_SRCS=$(OSH_DIR)/osh_units.c $(filter-out src/osh/host/osh_shell.c src/osh/host/osh_codes.c,$(OSH_PROG_HOSTSRCS)) ${file} src/osh/host/osh_codes.c $(OSC_LIB) src/runtime/rx_caproot.c`,leg==='plain'?'osh':join(build,'osh',exe)];
  const c=spawnSync('make',args,{encoding:'utf8',timeout:180000,maxBuffer:16<<20});writeFileSync(join(out,name+'-'+leg+'-compile.json'),JSON.stringify({command:['make',...args],status:c.status,signal:c.signal,error:c.error?.message??null,stdout:c.stdout,stderr:c.stderr},null,2));
  if(c.error||c.signal||c.status!==0)throw Error(name+': compile failure is not a kill');const bin=join(build,'osh',exe);if(!readFileSync(bin).subarray(0,4).equals(Buffer.from([127,69,76,70])))throw Error('not an ELF');
  const cases=join(out,name+'-'+leg+'-cases');const p=spawnSync('python3',['tests/osh/e2e/exit_abort_parallel.py',bin,cases,resolve(referenceArg)],{encoding:'utf8',timeout:180000,maxBuffer:16<<20});writeFileSync(join(out,name+'-'+leg+'-run.log'),(p.stdout??'')+(p.stderr??''));
  if(p.error||p.signal||p.status!==1)throw Error(name+': incomplete execution is not a kill');
  const rows=JSON.parse(readFileSync(join(cases,'results.json'))).rows,actual=rows.filter(r=>!r.pass).map(key).sort();const pass=!p.error&&!p.signal&&p.status===1&&rows.length===504&&JSON.stringify(actual)===JSON.stringify(expected);const report={name,leg,compile_exit:c.status,test_exit:p.status,signal:p.signal,error:p.error?.message??null,comparisons:rows.length,expected_failure_count:expected.length,actual_failure_count:actual.length,expected_keys:expected,actual_keys:actual,pass};reports.push(report);writeFileSync(join(out,'results.json'),JSON.stringify(reports,null,2));if(!pass)throw Error(name+': wrong behavioral failure set');console.log(JSON.stringify({name,leg,expected_failures:expected.length,actual_failures:actual.length,pass}));
 }
}
console.log('EXIT_ABORT_STATUS_MUTANTS_PASS');
