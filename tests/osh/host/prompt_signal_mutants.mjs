// #344: exact behavioral controls for interactive caller signal ownership.
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync} from 'node:fs';
import {join} from 'node:path';
const root=mkdtempSync('/tmp/L553-prompt-mutants-');
const source=readFileSync('src/osh/host/osh_shell.c','utf8');
for(const [name,from,to,count] of [
  ['no-restore','if (sh->interactive && restore_prompt_signals(old, 5) != 0)','if (0)',95],
  ['no-rollback','            (void)restore_prompt_signals(old, i);','            (void)i;',2],
]) {
  if(source.split(from).length!==2) throw Error(`${name}: anchor not unique`);
  const path=join(root,`${name}.c`), out=join(root,name);
  writeFileSync(path,source.replace(from,to));
  const build=spawnSync('make',['-s','-j2',`OUT_DIR=${out}`,
    'OSH_XFLAGS=$(OSH_FLAGS) -Isrc/osh/host -Isrc/runtime -DPROMPT_SIGNAL_TEST_FAULTS -Wl,--wrap=sigaction',
    `OSH_BIN_SRCS=$(OSH_DIR)/osh_units.c $(filter-out src/osh/host/osh_shell.c src/osh/host/osh_main.c src/osh/host/osh_codes.c,$(OSH_PROG_HOSTSRCS)) ${path} src/osh/host/osh_codes.c tests/osh/host/test_prompt_signal_restore.c $(OSC_LIB) src/runtime/rx_caproot.c`,'osh'],{encoding:'utf8',timeout:180000});
  writeFileSync(join(root,`${name}-build.log`),(build.stdout??'')+(build.stderr??''));
  writeFileSync(join(root,`${name}-build.status.json`),JSON.stringify({status:build.status,signal:build.signal,error:build.error?.message??null})+'\n');
  if(build.error||build.signal||build.status!==0) throw Error(`${name}: compile failure, no kill`);
  const r=spawnSync(join(out,'osh/osh'),[],{encoding:'utf8',timeout:30000});
  writeFileSync(join(root,`${name}-test.log`),(r.stdout??'')+(r.stderr??''));
  writeFileSync(join(root,`${name}-test.status.json`),JSON.stringify({status:r.status,signal:r.signal,error:r.error?.message??null})+'\n');
  const failures=(r.stdout??'').split('\n').filter(l=>l.startsWith('FAIL '));
  const expected=name==='no-restore'?/^FAIL (caller-action-restored|signal-failure-status|failure-action-boundary) /:/^FAIL failure-action-boundary .*payload=3 /;
  if(r.error||r.signal||r.status!==1||failures.length!==count||!failures.every(l=>expected.test(l))||!r.stdout.includes(`${count} failed`)) throw Error(`${name}: incorrect behavioral verdict, no kill`);
  console.log(`PASS ${name}: compiled, ${count} exact expected failing assertions`);
}
console.log(`PROMPT_SIGNAL_MUTANTS: 2 killed; artifacts ${root}`);
