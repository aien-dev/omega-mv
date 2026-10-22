const fs=require('fs'),cp=require('child_process'),crypto=require('crypto');
const root='/home/drakestapleton/workspace/cudapp-verification-l478';
const out='/home/drakestapleton/handoffs/omega-614-618-20261022/L478';
const source=root+'/src/omega_blackwell_codegen.c', green=fs.readFileSync(source,'utf8');
const sha=x=>crypto.createHash('sha256').update(x).digest('hex');
const original=cp.execFileSync('git',['show','349913f17c083996680c72b32f767b225005155d:src/omega_blackwell_codegen.c'],{cwd:root,encoding:'utf8'});
const variants=[['original',original],
 ['mode-0666',green.replace('O_CLOEXEC, 0600);','O_CLOEXEC, 0666);')],
 ['shared-directory',green.replace(' || (st.st_mode & 0077) != 0','')],
 ['overwrite-existing',green.replace('O_CREAT | O_EXCL | O_NOFOLLOW','O_CREAT | O_TRUNC | O_NOFOLLOW')],
 ['unconfined-directory',green.replace('if (strncmp(path, "/tmp/", 5) != 0 &&\n        !(rn > 1 && strncmp(path, root, rn) == 0 && path[rn] == \'/\')) return;','(void)rn;')],
 ['green',green]];
let results=[];
try {
 for(const [name,text] of variants) {
  if(name!=='original' && name!=='green' && text===green) throw Error('mutation did not apply '+name);
  fs.writeFileSync(source,text);
  const build=cp.spawnSync('timeout',['60','quiet-build','make','OUT_DIR=build-l478-mutants','build-l478-mutants/kernel_dump_test'],{cwd:root,encoding:'utf8'});
  fs.writeFileSync(`${out}/capture-${name}-build.log`,build.stdout+build.stderr);
  if(build.status!==0) throw Error('invalid mutant build '+name);
  const run=cp.spawnSync('timeout',['20','quiet-build','./build-l478-mutants/kernel_dump_test'],{cwd:root,encoding:'utf8'});
  fs.writeFileSync(`${out}/capture-${name}.log`,run.stdout+run.stderr);
  results.push({name,source_sha256:sha(text),binary_sha256:sha(fs.readFileSync(root+'/build-l478-mutants/kernel_dump_test')),build_command:'timeout 60 quiet-build make OUT_DIR=build-l478-mutants build-l478-mutants/kernel_dump_test',build_rc:build.status,command:'timeout 20 quiet-build ./build-l478-mutants/kernel_dump_test',rc:run.status,signal:run.signal,output:run.stdout.trim()});
  console.log(name,'build='+build.status,'run='+run.status,run.stdout.trim().split('\n').join('; '));
  if(run.status!==(name==='green'?0:1)) throw Error('unexpected behavior '+name);
 }
} finally {fs.writeFileSync(source,green); fs.writeFileSync(`${out}/capture-mutants.json`,JSON.stringify(results,null,2)+'\n');}
