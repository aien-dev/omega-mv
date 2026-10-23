import {spawnSync} from 'node:child_process';
import {mkdirSync,writeFileSync} from 'node:fs';
const root='/home/drakestapleton/workspace/targets/L705';const commands=[];
function run(label,cmd,args,options={}){const p=spawnSync(cmd,args,{encoding:'utf8',timeout:30000,maxBuffer:4<<20,...options});const r={label,command:[cmd,...args],status:p.status,signal:p.signal,error:p.error?.message??null,stdout:p.stdout,stderr:p.stderr};commands.push(r);writeFileSync(root+'/remaining-commands.json',JSON.stringify(commands,null,2)+'\n');if(p.status!==0||p.signal||p.error)throw Error(label+' failure');return r;}
for(const core of ['0','1']){const home=root+'/embed-final-home-'+core;mkdirSync(home,{mode:0o700});run('embed-'+core,root+'/build/embed',[core,home]);}
for(const change of ['0','1'])for(const version of ['bash','native','interp']){
 const home=root+'/pty-'+version+'-'+change;mkdirSync(home,{mode:0o700});run('pty-'+version+'-'+change,root+'/pty',[version==='bash'?'/bin/bash':'/home/drakestapleton/workspace/targets/L611/green/osh/osh',home,version==='interp'?'1':'0',change],{env:{PATH:'/usr/bin:/bin',HOME:home,XDG_STATE_HOME:home,LC_ALL:'C',TZ:'UTC'}});
}
console.log(JSON.stringify({embedded_cores:2,embedded_boundaries:18,real_pty_sessions:6,commands:commands.length,pass:true}));
