// SPDX-License-Identifier: AGPL-3.0-only
// Test orchestration only. Never used by the native executable.
import fs from 'node:fs';
import {spawnSync} from 'node:child_process';
const cases=JSON.parse(fs.readFileSync('seed/math/cases.json'));
const exe=process.argv[2]||'build/math-native/omega-math';
const r=spawnSync(exe,[],{input:cases.map(c=>c[0]).join('\n')+'\n',encoding:'utf8',timeout:60000});
if(r.error||r.status!==0) throw new Error(`runtime failure ${r.error} ${r.status} ${r.stderr}`);
const actual=r.stdout.trimEnd().split('\n');
let pass=0;
cases.forEach(([input,want],i)=>{const ok=actual[i]===want;pass+=ok;console.log(JSON.stringify({input,want,got:actual[i],ok}));});
console.log(JSON.stringify({requested:cases.length,executed:actual.length,passed:pass}));
process.exit(pass===cases.length&&actual.length===cases.length?0:1);
