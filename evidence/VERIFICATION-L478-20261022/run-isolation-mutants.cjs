const fs=require('fs'),cp=require('child_process'),crypto=require('crypto');
const root='/home/drakestapleton/workspace/cudapp-verification-l478',out='/home/drakestapleton/handoffs/omega-614-618-20261022/L478';
const path=root+'/tests/gpu_isolation_test.c',green=fs.readFileSync(path,'utf8');
const original=cp.execFileSync('git',['show','349913f17c083996680c72b32f767b225005155d:tests/gpu_isolation_test.c'],{cwd:root,encoding:'utf8'});
const sha=x=>crypto.createHash('sha256').update(x).digest('hex');
const variants=[['original',original],['mode-0666',green.replace('O_CLOEXEC, 0600);','O_CLOEXEC, 0666);')],['overwrite-existing',green.replace('O_CREAT | O_EXCL | O_NOFOLLOW','O_CREAT | O_TRUNC | O_NOFOLLOW')],['green',green]];
let results=[];
try {
 for (const [name,text] of variants) {
  if(name!=='original'&&name!=='green'&&text===green) throw Error('mutation did not apply');
  fs.writeFileSync(path,text);
  const build=cp.spawnSync('timeout',['60','quiet-build','make','OUT_DIR=build-l478-green','build-l478-green/gpu_isolation_test'],{cwd:root,encoding:'utf8'});
  fs.writeFileSync(`${out}/isolation-${name}-build.log`,build.stdout+build.stderr);
  if(build.status!==0) throw Error('invalid mutant build');
  const run=cp.spawnSync('timeout',['30','quiet-build','sh','tests/test_isolation_dump.sh','./build-l478-green/gpu_isolation_test'],{cwd:root,encoding:'utf8'});
  fs.writeFileSync(`${out}/isolation-${name}.log`,run.stdout+run.stderr);
  results.push({name,source_sha256:sha(text),binary_sha256:sha(fs.readFileSync(root+'/build-l478-green/gpu_isolation_test')),build_command:'timeout 60 quiet-build make OUT_DIR=build-l478-green build-l478-green/gpu_isolation_test',build_rc:build.status,command:'timeout 30 quiet-build sh tests/test_isolation_dump.sh ./build-l478-green/gpu_isolation_test',rc:run.status,signal:run.signal,output:run.stdout.trim()});
  console.log(name,'build='+build.status,'run='+run.status,run.stdout.trim().split('\n').slice(-1)[0]);
  if(run.status!==(name==='green'?0:1)) throw Error('unexpected behavior');
 }
} finally {fs.writeFileSync(path,green);fs.writeFileSync(`${out}/isolation-mutants.json`,JSON.stringify(results,null,2)+'\n');}
