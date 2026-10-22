const fs=require('fs'),cp=require('child_process'),crypto=require('crypto');
const root='/home/drakestapleton/workspace/omega-verification-614-618-20261022',out='/home/drakestapleton/handoffs/omega-614-618-20261022/L478';
const path=root+'/bench/prime_race/tests/gb10_sieve_host_test.c',green=fs.readFileSync(path,'utf8');
const original=cp.execFileSync('git',['show','19eebe0e04811002f641c0f7b58c1b87ea9b5c42:bench/prime_race/tests/gb10_sieve_host_test.c'],{cwd:root,encoding:'utf8'});
const sha=x=>crypto.createHash('sha256').update(x).digest('hex');
const args=['cc','-std=gnu11','-O2','-Wall','-Wextra','-Werror','-D_GNU_SOURCE','-I/home/drakestapleton/workspace/cudapp-verification-l478/src','-Isrc','-Ibench/prime_race','-Itests','-o','build-l478/gb10_sieve_listing_test','bench/prime_race/tests/gb10_sieve_listing_test.c','/home/drakestapleton/workspace/cudapp-verification-l478/build-l478-green/libomega_gpu.a','-lpthread','-lm'];
let results=[];
try {
 for(const [name,text] of [['original',original],['green',green]]) {
  fs.writeFileSync(path,text);
  const build=cp.spawnSync('timeout',['60','quiet-build',...args],{cwd:root,encoding:'utf8'});
  fs.writeFileSync(`${out}/sieve-listing-${name}-build.log`,build.stdout+build.stderr);
  if(build.status!==0) {console.log(build.stderr);throw Error('build failed');}
  const run=cp.spawnSync('timeout',['20','quiet-build','./build-l478/gb10_sieve_listing_test'],{cwd:root,encoding:'utf8'});
  fs.writeFileSync(`${out}/sieve-listing-${name}.log`,run.stdout+run.stderr);
  results.push({name,source_sha256:sha(text),binary_sha256:sha(fs.readFileSync(root+'/build-l478/gb10_sieve_listing_test')),build_command:['timeout','60','quiet-build',...args],build_rc:build.status,command:'timeout 20 quiet-build ./build-l478/gb10_sieve_listing_test',rc:run.status,output:run.stdout.trim()});
  console.log(name,'build='+build.status,'run='+run.status,run.stdout.trim());
  if(run.status!==(name==='green'?0:1))throw Error('unexpected behavior');
 }
} finally {fs.writeFileSync(path,green);fs.writeFileSync(`${out}/sieve-listing-results.json`,JSON.stringify(results,null,2)+'\n');}
