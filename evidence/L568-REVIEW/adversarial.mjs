import {spawnSync} from 'node:child_process';
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {resolve,join} from 'node:path';
const root=resolve('build/L568'),out=join(root,process.argv[2]||'adversarial-complete');mkdirSync(out);
let checks=0;const results=[];
function run(name,bin,args,opts={}){let r=spawnSync(bin,args,{encoding:'utf8',maxBuffer:4<<20,timeout:30000,...opts});writeFileSync(join(out,name+'.json'),JSON.stringify({bin,args,status:r.status,signal:r.signal,error:r.error?.message,stdout:r.stdout,stderr:r.stderr},null,2)+'\n');if(r.error||r.signal||/Sanitizer|runtime error:/.test(r.stderr||''))throw Error('infrastructure '+name);return r;}
function check(name,ok){checks++;results.push({name,pass:!!ok});if(!ok)console.log('FAIL '+name);}
const host=join(root,'core/osh/diff-host.result'),toy=join(root,'core/osh/osh_toy'),fx=resolve('tests/osh/diff/fixtures.txt');
const base=readFileSync(host,'utf8').replace('adapter=linux-host ','adapter=aienos-native ');
const pin=base.split('\n')[1];const cases=[
 ['max-physical',base.replace(pin,pin.padEnd(16383,' ')),0,'OSH_DIFF_COMPARE_ONLY_PASS'],
 ['over-max-physical',base.replace(pin,pin.padEnd(16384,' ')),1,'OSHDIFF line too long'],
 ['max-crlf',base.replace(pin,pin.padEnd(16382,' ')).replaceAll('\n','\r\n'),0,'OSH_DIFF_COMPARE_ONLY_PASS'],
 ['too-large-crlf',base.replace(pin,pin.padEnd(16383,' ')).replaceAll('\n','\r\n'),1,'OSHDIFF line too long'],
 ['value299',base.replace(/ cc=\S+/,' cc='+'z'.repeat(299)),0,'OSH_DIFF_COMPARE_ONLY_PASS'],
 ['value300',base.replace(/ cc=\S+/,' cc='+'z'.repeat(300)),1,'empty or long pin value for cc'],
 ['tab-count',base.replace('end n=33','end n=\t33'),1,'invalid end line count'],
 ['trailing-count-space',base.replace('end n=33','end n=33 '),1,'invalid end line count'],
 ['count-overflow',base.replace('end n=33','end n='+'9'.repeat(512)),1,'end line count does not match'],
 ['zero-padded-count',base.replace('end n=33','end n=00033'),0,'OSH_DIFF_COMPARE_ONLY_PASS'],
 ['post-end-protocol',base+'OSHDIFF-RESULT 1\n',1,'after the end line'],
 ['malformed-prefix',base.replace('OSHDIFF-RESULT 1','OSHDIFF-RESULT 1x'),1,'first OSHDIFF line'],
 ['nul-noise',Buffer.concat([Buffer.from('noise\0'+'x'.repeat(35000)+'OSHDIFF bogus\n'),Buffer.from(base)]),0,'OSH_DIFF_COMPARE_ONLY_PASS'],
 ['truncated-eof',base.replace(/OSHDIFF end n=33\n/,''),1,'no end line'],
 ['no-final-newline',base.trimEnd(),0,'OSH_DIFF_COMPARE_ONLY_PASS'],
 ['cross-token-last',base.replace(/ fixtures=\S+/,' fixtures other=foo'),1,'pin token without ='],
];
for(const engine of ['osh_diff','osh_diff_asan'])for(const [name,bytes,status,message]of cases){let p=join(out,name+'.result');writeFileSync(p,bytes);let r=run(engine+'-'+name,join(root,'core/osh',engine),['--host-results',host,'--native',p,'--require-native',toy,fx]);check(engine+'-'+name,r.status===status&&r.stdout.includes(message));}
const env={PATH:'/usr/bin:/bin',HOME:out,XDG_STATE_HOME:out,LC_ALL:'C',TZ:'UTC'};
const scripts=[['neg-many','exit -1 9\nprintf "%s\\n" "$?"\nprintf "tail\\n"'],['invalid-many','exit bad 9\nprintf "tail\\n"'],['list-condition','printf "%d" bad || exit 3 4\nprintf "%s\\n" "$?"'],['skipped-exit','printf "" || exit 3 4\nprintf "%s\\n" "$?"'],['redirected','exit 4 5 >&1\nprintf "%s\\n" "$?"'],['valid-wrap','exit 257\nprintf "tail\\n"']];
for(const [name,text]of scripts)for(const mode of ['command','stdin','script']){let p=join(out,name+'.sh');writeFileSync(p,text);const args=mode==='command'?['-c',text]:mode==='script'?[p]:[];let oracle=run('bash-'+name+'-'+mode,'/bin/bash',['--noprofile','--norc',...args],{env,input:mode==='stdin'?text:undefined});for(const engine of [0,1]){let r=run('osh-'+name+'-'+mode+'-'+engine,join(root,'core/osh/osh'),args,{env:{...env,OSH_INTERP:String(engine)},input:mode==='stdin'?text:undefined});check(name+'-'+mode+'-'+engine,r.status===oracle.status&&r.stdout===oracle.stdout);}}
writeFileSync(join(out,'results.json'),JSON.stringify({checks,results,native_results:'SYNTHETIC comparator controls only'},null,2)+'\n');console.log('ADVERSARIAL '+checks+' assertions '+results.filter(x=>!x.pass).length+' failures');process.exitCode=results.some(x=>!x.pass)?1:0;
