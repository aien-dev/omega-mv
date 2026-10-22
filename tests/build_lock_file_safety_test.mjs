#!/usr/bin/env node
// Private fixtures only. Refusal requires bytes intact, no command, and exact exit.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
const [outArg, helperArg='tools/with_build_lock.sh', mode='all']=process.argv.slice(2);
if(!outArg)throw Error('PRIVATE_OUT required');
fs.mkdirSync(outArg,{recursive:true});
const out=fs.mkdtempSync(path.join(fs.realpathSync(outArg),'file-safety.'));
const helper=path.resolve(helperArg), hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const results=[];
function check(name, pass, details){const row={name,verdict:pass?'PASS':'FAIL',...details};results.push(row);console.log(JSON.stringify(row));}
for(const entry of mode==='linked'?['direct']:['direct','make']){
 for(const kind of mode==='linked'?['symlink','hardlink']:['new','regular','nonempty','alias','symlink','dangling','hardlink','fifo','directory']){
  const dir=path.join(out,entry+'-'+kind);fs.mkdirSync(dir);
  const output=path.join(dir,'build');fs.mkdirSync(output);
  const lock=output+'.omega-build.lock',victim=path.join(dir,'victim'),marker=path.join(dir,'executed');
  const bytes=Buffer.from('PRIVATE SYNTHETIC VICTIM MUST REMAIN INTACT\n');fs.writeFileSync(victim,bytes);
  if(kind==='symlink')fs.symlinkSync(victim,lock);
  if(kind==='dangling')fs.symlinkSync(path.join(dir,'absent-victim'),lock);
  if(kind==='hardlink')fs.linkSync(victim,lock);
  if(kind==='fifo'){const r=spawnSync('mkfifo',[lock]);if(r.status!==0)throw Error('mkfifo failed');}
  if(kind==='directory')fs.mkdirSync(lock);
  if(['regular','alias','nonempty'].includes(kind))fs.writeFileSync(lock,kind==='nonempty'?bytes:'');
  const old=fs.existsSync(lock)?fs.lstatSync(lock):null;
  const lockHash=kind==='nonempty'?hash(fs.readFileSync(lock)):null;
  let spelling=output;
  if(kind==='alias'){spelling=path.join(dir,'alias');fs.symlinkSync(output,spelling);}
  const command=path.join(dir,'command.sh');fs.writeFileSync(command,'#!/bin/sh\nprintf executed > "$1"\n');
  const makefile=path.join(dir,'probe.mk');
  // Exercise the actual public Makefile dispatch; the additional target is a command marker.
  fs.writeFileSync(makefile,`include ${path.resolve('Makefile')}\n.PHONY: safety-marker\nsafety-marker:\n\t@sh '${command}' '${marker}'\n`);
  const args=entry==='direct'?['sh',helper,spelling,'sh',command,marker]:['make','--no-print-directory','-f',makefile,'OUT_DIR='+spelling,'safety-marker'];
  const env={...process.env};delete env.OMEGA_BUILD_LOCKED_DIR;delete env.MAKEFLAGS;delete env.MFLAGS;
  const r=spawnSync(args[0],args.slice(1),{encoding:'utf8',timeout:4000,env});
  fs.writeFileSync(path.join(dir,'command.log'),JSON.stringify({args,status:r.status,signal:r.signal,error:r.error?.message??null,stdout:r.stdout,stderr:r.stderr},null,2));
  const before=hash(bytes),after=hash(fs.readFileSync(victim));
  const refused=['symlink','dangling','hardlink','fifo','directory'].includes(kind);
  const markerExists=fs.existsSync(marker);
  const now=fs.lstatSync(lock);
  const stable=old?old.ino===now.ino&&old.dev===now.dev:true;
  const leafPreserved=kind==='nonempty'?lockHash===hash(fs.readFileSync(lock)):true;
  const pass=!r.error&&!r.signal&&before===after&&stable&&leafPreserved&&
    (refused?r.status===(entry==='direct'?2:2)&&!markerExists&&r.stderr.includes('unsafe build lock:'):r.status===0&&markerExists&&now.isFile()&&now.nlink===1);
  check(entry+'-'+kind,pass,{assertion:'safe_lock_leaf_preserves_bytes_and_command_boundary',exit:r.status,signal:r.signal,error:r.error?.message??null,before_sha256:before,after_sha256:after,marker:markerExists,inode_stable:stable,lock_bytes_preserved:leafPreserved,refused});
 }
}
fs.writeFileSync(path.join(out,'results.json'),JSON.stringify({helper,helper_sha256:hash(fs.readFileSync(helper)),results},null,2));
console.log(JSON.stringify({out,pass:results.every(x=>x.verdict==='PASS')}));
if(results.some(x=>x.verdict==='FAIL'))process.exitCode=1;
