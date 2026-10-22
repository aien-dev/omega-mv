#!/usr/bin/env node
import fs from 'node:fs'; import net from 'node:net'; import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
const args=process.argv.slice(2);
if(!args.includes('-c')) {const r=spawnSync('cc',args,{stdio:'inherit'});process.exit(r.status??1);}
const source=args[args.indexOf('-c')+1],target=args[args.indexOf('-o')+1];
const include=args.find(x=>x.startsWith('-I')&&x.endsWith('/m16'));
if(!include) throw Error('missing actual physics include directory');
const header=include.slice(2)+'/m16_native.h';
const r=spawnSync('git',['-C',include.slice(2), 'rev-parse','HEAD'],{encoding:'utf8'});
const record={kind:'SYNTHETIC_NON_OBJECT',source,header,head:r.stdout.trim(),header_sha256:crypto.createHash('sha256').update(fs.readFileSync(header)).digest('hex'),stamp:fs.existsSync(process.env.L532_STAMP)?fs.readFileSync(process.env.L532_STAMP,'utf8'):null};
if(process.env.L532_BARRIER) await new Promise((resolve,reject)=>{const s=net.createConnection(process.env.L532_BARRIER,()=>s.write(JSON.stringify(record)+'\n'));s.on('data',()=>{s.end();resolve();});s.on('error',reject);});
fs.appendFileSync(process.env.L532_LOG,JSON.stringify(record)+'\n');
fs.writeFileSync(target,JSON.stringify(record));
fs.writeFileSync(target.replace(/\.o$/,'.d'),target+': '+source+' '+header+'\n');
