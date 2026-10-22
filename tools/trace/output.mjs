import fs from 'node:fs';import path from 'node:path';import {OwnedRoot} from './paths.mjs';import {fail} from './schema.mjs';
export function writeOutput(filename,bytes,source){
 if(!path.isAbsolute(filename))fail('absolute output path required');if(source&&(path.resolve(filename)===path.resolve(source)||path.resolve(filename).startsWith(path.resolve(source)+path.sep)))fail('output must be outside source archive');
 const root=new OwnedRoot(path.dirname(filename));try{const fd=root.open(path.basename(filename),fs.constants.O_WRONLY|fs.constants.O_CREAT|fs.constants.O_EXCL);try{let off=0,b=Buffer.from(bytes);while(off<b.length){const n=fs.writeSync(fd,b,off);if(n<=0)fail('short output write');off+=n;}fs.fsyncSync(fd);}finally{fs.closeSync(fd);}fs.fsyncSync(root.fd);}finally{root.close();}
}
