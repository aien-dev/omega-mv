// #344: compiled controls must fail only the exact expected bash comparisons.
import {spawnSync} from 'node:child_process';
import {mkdtempSync, readFileSync, writeFileSync} from 'node:fs';
import {join} from 'node:path';
const root = mkdtempSync('/tmp/L553-exit-mutants-');
const source = readFileSync('src/osh/host/osh_shell.c', 'utf8');
const anchor = '(stop == 2 && src->exact_end)';
if (source.split(anchor).length !== 2) throw Error('source anchor not unique');
for (const [name,replacement,count,expectedMode] of [['continue-command','0',4,'command'],['stop-streams','stop == 2',8,'script|stdin']]) {
  const path = join(root,`${name}.c`), out = join(root,name);
  writeFileSync(path,source.replace(anchor,replacement));
  const build = spawnSync('make',['-s','-j2',`OUT_DIR=${out}`,`OSH_BIN_SRCS=$(OSH_DIR)/osh_units.c $(filter-out src/osh/host/osh_shell.c src/osh/host/osh_codes.c,$(OSH_PROG_HOSTSRCS)) ${path} src/osh/host/osh_codes.c $(OSC_LIB) src/runtime/rx_caproot.c`,'osh'],{encoding:'utf8',timeout:180000});
  writeFileSync(join(root,`${name}-build.log`),(build.stdout??'')+(build.stderr??''));
  writeFileSync(join(root,`${name}-build.status.json`),JSON.stringify({status:build.status,signal:build.signal,error:build.error?.message??null})+'\n');
  if(build.error||build.signal||build.status!==0) throw Error(`${name}: build failure is not a kill`);
  const r=spawnSync(process.execPath,['tests/osh/e2e/exit_boundary_controls.mjs',join(out,'osh/osh'),join(root,`${name}-cases`)],{encoding:'utf8',timeout:60000});
  writeFileSync(join(root,`${name}-test.log`),(r.stdout??'')+(r.stderr??''));
  writeFileSync(join(root,`${name}-test.status.json`),JSON.stringify({status:r.status,signal:r.signal,error:r.error?.message??null})+'\n');
  const failures=(r.stdout??'').split('\n').filter(l=>l.startsWith('FAIL '));
  const expected=new RegExp(`^FAIL (too-many|prefixed)-(${expectedMode})-(native|interp):`);
  if(r.error||r.signal||r.status!==1||failures.length!==count||!failures.every(l=>expected.test(l))||!r.stdout.includes(`${count} failed; actual bash reference`)) throw Error(`${name}: unexpected verdict; no kill credited`);
  console.log(`PASS ${name}: compiled, ${count} exact expected failing assertions`);
}
console.log(`EXIT_BOUNDARY_MUTANTS: 2 killed; artifacts ${root}`);
