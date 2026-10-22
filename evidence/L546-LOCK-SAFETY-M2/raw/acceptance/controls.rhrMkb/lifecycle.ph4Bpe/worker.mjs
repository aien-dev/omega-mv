import fs from 'node:fs';import net from 'node:net';import {spawnSync} from 'node:child_process';
const args=process.argv.slice(2);const source=args[args.indexOf('-c')+1],out=args[args.indexOf('-o')+1];
if(process.env.L537_GATE && (args.includes('-c')||args[0]==='parallel')) await new Promise((resolve,reject)=>{const c=net.createConnection(process.env.L537_GATE,()=>c.write(JSON.stringify({pid:process.pid,source,args})+'\n'));c.on('data',()=>{c.end();resolve();});c.on('error',reject);c.on('end',()=>reject(Error('barrier disconnected')));});
if(args[0]==='parallel')process.exit(0);
if(args.includes('-c')&&process.env.L537_FAIL){fs.writeFileSync(out,'partial invalid object');process.exit(1);}
const r=spawnSync('cc',args,{stdio:'inherit'});process.exit(r.status??1);
