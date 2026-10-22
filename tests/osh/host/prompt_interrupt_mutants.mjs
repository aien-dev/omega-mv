// Compile broken behaviors, then require their exact wrong state. No deadline,
// crash, compile error or unrelated failure is accepted as a mutant kill.
import {spawnSync} from 'node:child_process';
import {mkdirSync,readFileSync,writeFileSync} from 'node:fs';
import {resolve,join} from 'node:path';
const root=resolve(process.argv[2]??'build/L563/mutants'); mkdirSync(root,{recursive:true});
const source=readFileSync('src/osh/host/osh_shell.c','utf8');
const cases=[
 ['drop-handled-flag','if (g_prompt_int) { g_prompt_int = 0; result = -2; break; }','if (g_prompt_int) { g_prompt_int = 0; }',[1,2,7,14],b=>b.includes('progress=0')&&b.includes('stdout=[0\nAGAIN\n]')&&b.includes('wait=[72 ')],
 ['unmasked-wait','sigaddset(&block, SIGINT);','sigaddset(&block, SIGUSR2);',[7],b=>b.includes('progress=0')&&b.includes('stdout=[130\nAGAIN\n]')&&b.includes('wait=[72 ')],
 ['commit-interrupted-newline','if (g_prompt_int && result != -1) { g_prompt_int = 0; return -2; }','if (0) return -2;',[8],b=>b.includes('progress=1')&&b.includes('stdout=[DISCARD130\nAGAIN\n]')],
 ['hide-select-error','            result = -1;\n            break;','            result = 0;\n            break;',[4,10],b=>b.includes('status=256')&&b.includes('stdout=[AGAIN\n]')],
 ['blocking-after-readiness','int changed = !(flags & O_NONBLOCK);','int changed = 0;',[14],b=>b.includes('progress=0')&&b.includes('stdout=[AGAIN\n]')&&b.includes('wait=[63 ')&&b.includes('rintf: command not found')],
];
for(const [name,from,to,windows,verify] of cases) {
 if(source.split(from).length!==2) throw Error(`${name}: anchor not unique`);
 const path=join(root,`${name}.c`),out=join(root,name); writeFileSync(path,source.replace(from,to));
 const build=spawnSync('make',['-s','-j2',`OUT_DIR=${out}`,`OSH_BIN_SRCS=$(OSH_DIR)/osh_units.c $(filter-out src/osh/host/osh_shell.c,$(OSH_PROG_HOSTSRCS)) ${path} $(OSC_LIB) src/runtime/rx_caproot.c`,`${out}/osh/test_prompt_interrupt`],{encoding:'utf8',timeout:180000});
 writeFileSync(join(root,`${name}-build.log`),(build.stdout??'')+(build.stderr??''));
 writeFileSync(join(root,`${name}-build.json`),JSON.stringify({status:build.status,signal:build.signal,error:build.error?.message??null})+'\n');
 if(build.error||build.signal||build.status!==0) throw Error(`${name}: compile failed, no kill`);
 const r=spawnSync(join(out,'osh/test_prompt_interrupt'),[],{encoding:'utf8',timeout:30000});
 writeFileSync(join(root,`${name}-run.log`),(r.stdout??'')+(r.stderr??''));
 writeFileSync(join(root,`${name}-run.json`),JSON.stringify({status:r.status,signal:r.signal,error:r.error?.message??null})+'\n');
 const blocks=(r.stdout??'').split(/(?=^(?:PASS|FAIL) window=)/m).filter(b=>b.startsWith('FAIL window='));
 const expected=windows.flatMap(w=>[`${w}:0`,`${w}:1`]).sort();
 const actual=blocks.map(b=>{const m=b.match(/^FAIL window=(\d+) native=(\d+)/); return `${m[1]}:${m[2]}`;}).sort();
 if(r.error||r.signal||r.status!==1||JSON.stringify(actual)!==JSON.stringify(expected)||!blocks.every(verify)) throw Error(`${name}: wrong behavioral verdict, no kill`);
 console.log(`PASS ${name}: compiled; ${blocks.length} exact wrong-state assertions, windows ${windows}`);
}
console.log('PROMPT_INTERRUPT_MUTANTS: 5 killed');
