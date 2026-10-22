import {readFileSync as read,writeFileSync as write,mkdirSync as mkdir} from 'node:fs';
import {spawnSync as spawn} from 'node:child_process';
import {resolve,join} from 'node:path';
const root=resolve('targets/L601'), source=read('src/osh/host/osh_builtin.c','utf8');
const needle='st = (int)(v & 0xff);';if(source.split(needle).length!==2)throw Error('anchor');
write(join(root,'negative-modulo-mutant.c'),source.replace(needle,'st = (int)((v % 256) < 0 ? 0 : (v % 256));'));
const tests=[
 [[],511,255,'none'],[['--'],-1,255,'none'],[['--','--'],17,2,'numeric'],[['--','--','4'],17,2,'numeric'],
 [['-1'],17,255,'none'],[['-257'],17,255,'none'],[['-513'],17,255,'none'],[['-9223372036854775807'],17,1,'none'],
 [['-9223372036854775808'],17,0,'none'],[['9223372036854775807'],17,255,'none'],[['9223372036854775808','3'],17,2,'numeric'],
 [[' \t\n\r\v\f-257\t '],17,255,'none'],[[' \t'],17,2,'numeric'],[[''],17,2,'numeric'],[['7\r'],17,2,'numeric'],
 [['--','+000256','2'],17,1,'count'],[['junk','2'],17,2,'numeric'],[['7','2'],17,1,'count'],[['--','-1'],17,255,'none']];
const records=[];
for(const variant of ['normal','mutant'])for(const leg of ['plain','sanitizer']){
 const exe=join(root,'direct-'+variant+'-'+leg);
 const args=['-std=gnu11','-Wall','-Wextra','-Werror','-O1','-g','-ffunction-sections','-fdata-sections','-Isrc/osh/host','-Wl,--gc-sections',...(leg==='sanitizer'?['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']:[]),...(variant==='mutant'?['-DL601_BUILTIN="'+join(root,'negative-modulo-mutant.c')+'"']:[]),'evidence/L601-EXIT-REVIEW/direct_probe.c','-o',exe];
 const build=spawn('gcc',args,{encoding:'utf8',timeout:60000});records.push({type:'build',variant,leg,command:['gcc',...args],status:build.status,signal:build.signal,stdout:build.stdout,stderr:build.stderr});if(build.status!==0||build.signal||build.error)throw Error('build failure never kill');
 const rows=[];
 for(const [argv,prior,status,kind]of tests)for(const parent of [0,1]){
  const expected=[status,parent&&kind!=='count'?1:0,parent&&kind!=='count'?status:93,parent&&kind==='count'?1:0,prior];
  const args=[String(parent),String(prior),...argv];const r=spawn(exe,args,{encoding:'utf8',timeout:20000,env:{PATH:'/usr/bin:/bin',LC_ALL:'C'}});
  if(r.status!==0||r.signal||r.error||/Sanitizer|runtime error:/.test(r.stderr))throw Error('abnormal is not kill');
  const actual=r.stdout.trim().split(' ').map(Number), errorClass=r.stderr.includes('numeric argument required')?'numeric':r.stderr.includes('too many arguments')?'count':r.stderr===''?'none':'other';
  const ok=JSON.stringify(expected)===JSON.stringify(actual)&&kind===errorClass;
  rows.push({argv,prior,parent,expected,actual,errorClass,kind,ok,status:r.status,signal:r.signal,stdout:r.stdout,stderr:r.stderr});
 }
 const failures=rows.filter(r=>!r.ok);
 records.push({type:'test',variant,leg,rows,failures:failures.length});
 const expectedMutant=rows.filter(r=>r.kind==='none'&&r.argv.some(a=>/^-\d/.test(a.trim()))&&r.expected[0]!==0);
 if(variant==='normal'&&failures.length!==0||variant==='mutant'&&(failures.length!==expectedMutant.length||expectedMutant.some(r=>r.ok)))throw Error('wrong exact failure set');
 console.log(JSON.stringify({variant,leg,rows:rows.length,failures:failures.length,normalProcessExit:true}));
}
write(join(root,'direct-results.json'),JSON.stringify(records,null,2));
