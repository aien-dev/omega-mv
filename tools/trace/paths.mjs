import fs from 'node:fs';
import path from 'node:path';
import {fail} from './schema.mjs';
const C=fs.constants;
// Walk every component without following links, retaining directory descriptors.
// Operations stay relative to the retained inode through Linux procfs, even on rename.
export class OwnedRoot {
 constructor(root,{create=false}={}){
  if(typeof root!=='string'||!path.isAbsolute(root)||root.split('/').includes('..')||root.includes('\0'))fail('absolute local root required; no traversal');
  this.root=path.resolve(root);this.fds=[];
  let fd=fs.openSync('/',C.O_RDONLY|C.O_DIRECTORY|C.O_NOFOLLOW);this.fds.push(fd);
  try{
   const parts=this.root.split('/').filter(Boolean);
   if(!parts.length)fail('refuse filesystem root');
   for(let i=0;i<parts.length;i++){
    const target=`/proc/self/fd/${fd}/${parts[i]}`;
    if(create&&i===parts.length-1){try{fs.mkdirSync(target,{mode:0o700});}catch(e){if(e.code!=='EEXIST')throw e;}}
    fd=fs.openSync(target,C.O_RDONLY|C.O_DIRECTORY|C.O_NOFOLLOW);this.fds.push(fd);
   }
   this.fd=fd;this.stat=fs.fstatSync(fd);
   if(this.stat.uid!==process.getuid()||(this.stat.mode&0o077)!==0)fail('root must be owner-only (0700)');
  }catch(e){this.close();throw e;}
 }
 check(){const s=fs.lstatSync(this.root);if(!s.isDirectory()||s.isSymbolicLink()||s.dev!==this.stat.dev||s.ino!==this.stat.ino||s.uid!==process.getuid()||(s.mode&0o077)!==0)fail('archive root swapped or unsafe');}
 target(name){this.check();if(!/^[a-zA-Z0-9_.-]{1,96}$/.test(name)||name==='.'||name==='..')fail('invalid owned filename');return `/proc/self/fd/${this.fd}/${name}`;}
 open(name,flags,mode=0o600){const fd=fs.openSync(this.target(name),flags|C.O_NOFOLLOW|C.O_NONBLOCK,mode);try{const s=fs.fstatSync(fd);if(!s.isFile()||s.uid!==process.getuid()||(s.mode&0o077)!==0||s.nlink!==1)fail('unsafe file owner/mode/type/links');return fd;}catch(e){fs.closeSync(fd);throw e;}}
 read(name,max=64*1024*1024){const fd=this.open(name,C.O_RDONLY);try{if(fs.fstatSync(fd).size>max)fail('file bound');return fs.readFileSync(fd);}finally{fs.closeSync(fd);}}
 names(){this.check();return fs.readdirSync(`/proc/self/fd/${this.fd}`).sort();}
 unlink(name){const fd=this.open(name,C.O_RDONLY);try{const s=fs.fstatSync(fd),t=fs.lstatSync(this.target(name));if(t.ino!==s.ino||t.dev!==s.dev)fail('file swapped');fs.unlinkSync(this.target(name));fs.fsyncSync(this.fd);}finally{fs.closeSync(fd);}}
 close(){for(const fd of (this.fds??[]).reverse())fs.closeSync(fd);this.fds=[];}
}
