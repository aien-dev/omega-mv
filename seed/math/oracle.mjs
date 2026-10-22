// SPDX-License-Identifier: AGPL-3.0-only
// Arbitrary precision BigInt fractions are test oracles only. No native code reuse.
import fs from 'node:fs';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
const abs=x=>x<0n?-x:x;
function gcd(a,b){a=abs(a);b=abs(b);while(b)[a,b]=[b,a%b];return a;}
const rat=(n,d=1n)=>{if(d===0n)throw Error('zero denominator');if(d<0n){n=-n;d=-d;}let g=gcd(n,d);return [n/g,d/g];};
const add=(a,b)=>rat(a[0]*b[1]+b[0]*a[1],a[1]*b[1]);
const neg=a=>[-a[0],a[1]],sub=(a,b)=>add(a,neg(b));
const mul=(a,b)=>rat(a[0]*b[0],a[1]*b[1]);
const div=(a,b)=>rat(a[0]*b[1],a[1]*b[0]);
const fmt=a=>a[0]+(a[1]===1n?'':'/'+a[1]);
const z=rat(0n),one=rat(1n);
function poly(p){let s='';p.forEach((c,i)=>{if(!c[0])return;let a=fmt([abs(c[0]),c[1]]);s+=(c[0]<0n?'-':s?'+':'')+(i===0?a:(a==='1'?'':a+'*')+'x'+(i===1?'':'^'+i));});return s||'0';}
const pmul=(a,b)=>{let p=Array.from({length:a.length+b.length-1},()=>z);a.forEach((v,i)=>b.forEach((w,j)=>p[i+j]=add(p[i+j],mul(v,w))));return p;};
const padd=(a,b)=>Array.from({length:Math.max(a.length,b.length)},(_,i)=>add(a[i]||z,b[i]||z));
const diff=p=>p.slice(1).map((v,i)=>mul(v,rat(BigInt(i+1))));
const integ=p=>[z,...p.map((v,i)=>div(v,rat(BigInt(i+1))))];
const subst=(p,x)=>p.reduceRight((a,v)=>add(v,mul(a,x)),z);
let state=494n;
function rand(n){state=(state*6364136223846793005n+1442695040888963407n)&((1n<<64n)-1n);return Number(state%BigInt(n));}
function big(d){let s=String(1+rand(9));for(let i=1;i<d;i++)s+=rand(10);return BigInt(s)*(rand(2)?1n:-1n);}
function rp(n){return Array.from({length:n},()=>rat(BigInt(rand(19)-9),BigInt(rand(9)+1)));}
const cases=[];
function test(input,want,kind){cases.push({input,want:want.startsWith('REFUSED')?want:'OK '+want,kind});}
for(let i=0;i<100;i++){
 let a=rat(big(1+rand(50)),abs(big(1+rand(40)))),b=rat(big(1+rand(50)),abs(big(1+rand(40))));
 for(const [op,f] of [['+',add],['-',sub],['*',mul],['/',div]])test(`eval (${fmt(a)})${op}(${fmt(b)})`,fmt(f(a,b)),'BigInt rational');
}
for(let i=0;i<70;i++){
 let a=rp(1+rand(7)),b=rp(1+rand(7)),x=rat(BigInt(rand(21)-10),BigInt(rand(9)+1));
 test(`expand (${poly(a)})*(${poly(b)})`,poly(pmul(a,b)),'coefficient convolution');
 test(`collect (${poly(a)})+(${poly(b)})`,poly(padd(a,b)),'coefficient addition');
 test(`diff ${poly(a)}`,poly(diff(a)),'formal derivative');
 test(`integ ${poly(a)}`,poly(integ(a)),'formal antiderivative');
 test(`diff ${poly(integ(a))}`,poly(a),'coefficient identity D(I(p))=p');
 test(`subst ${poly(a)} ; ${fmt(x)}`,fmt(subst(a,x)),'rational substitution');
 let c=rat(BigInt(rand(18)+1)),d=rat(BigInt(rand(19)-9));
 test(`solve (${fmt(c)})*x+(${fmt(d)}) ; ${fmt(x)}`,fmt(div(sub(x,d),c)),'linear equation');
 let aa=rat(BigInt(rand(15)-7)),bb=rat(BigInt(rand(15)-7)),cc=rat(BigInt(rand(15)-7)),dd=rat(BigInt(rand(15)-7));
 let det=sub(mul(aa,dd),mul(bb,cc));
 if(det[0]){
  const yy=rat(BigInt(rand(11)-5),BigInt(rand(8)+1)),e=add(mul(aa,x),mul(bb,yy)),f=add(mul(cc,x),mul(dd,yy));
  test(`linear ${[aa,bb,cc,dd,e,f].map(fmt).join(';')}`,fmt(x)+';'+fmt(yy),'known-solution matrix');
 }
}
// Euclidean GCD with independently constructed common factor and coprime linear residuals.
for(let i=0;i<30;i++){
 const c=rp(1+rand(5)); c[c.length-1]=one;
 const a=pmul(c,[rat(BigInt(i)),one]),b=pmul(c,[rat(BigInt(i+1)),one]);
 test(`gcd ${poly(a)} ; ${poly(b)}`,poly(c),'constructed exact common factor');
}
const max='9'.repeat(128);
for(const [input,want] of [
 ['eval '+max,max],['eval '+max+'+1','REFUSED 4'],['eval 1'+ '0'.repeat(128),'REFUSED 4'],
 ['eval '+max+'*1',max],['eval '+max+'/'+max,'1'],['eval '+max+'*2','REFUSED 4'],
 ['eval ('+max+'/2)*(2/'+max+')','1'],['eval 10^127','1'+'0'.repeat(127)],
 ['eval 10^128','REFUSED 4'],['eval -(0)','0'],['eval 0^-1','REFUSED 2'],
 ['eval (1-1)/(2-2)','REFUSED 2'],['eval 2^(1/2)','REFUSED 3'],['eval 2^1025','REFUSED 4'],
 ['eval 1^1024','1'],['eval x^0','1'],['eval (x-x)^0','REFUSED 2'],
 ['eval 1/(x-x)','REFUSED 2'],['eval (x-x)/(x+1)','REFUSED 3'],
 ['eval 1/(x+1)','REFUSED 3'],['eval x*x/x','REFUSED 3'],['eval 0*(1/0)','REFUSED 2'],
 ['eval (x/x)^0','REFUSED 3'],['eval sqrt(4)','REFUSED 3'],['eval exp(0)','REFUSED 3'],
 ['eval 1.5','REFUSED 1'],['eval 2x','REFUSED 1'],['eval x y','REFUSED 1'],
 ['eval 1\u0000+2','REFUSED 1'],['eval 1\u00ff','REFUSED 1'],['eval 1;2','REFUSED 1'],
 ['eval ','REFUSED 1'],['','REFUSED 1'],['bad 1','REFUSED 3'],
 ['solve x=1','REFUSED 1'],['subst x;x','REFUSED 3'],['linear x;0;0;1;1;2','REFUSED 3'],
 ['linear 0;0;0;0;0;0','REFUSED 7'],['linear 0;0;0;0;1;0','REFUSED 8'],
 ['linear 0;0;2;1;0;3','REFUSED 7'],['linear 0;0;2;1;1;3','REFUSED 8'],
 ['linear 0;1;1;0;2;3','3;2'],['gcd 0;0','0'],['gcd 0;2*x+4','2+x'],
 ['gcd 2;4','1'],['gcd (x-1)^4;(x-1)^3','-1+3*x-3*x^2+x^3'],
 ['eval '+'('.repeat(32)+'1'+')'.repeat(32),'REFUSED 6'],
 ['eval '+' '.repeat(4096),'REFUSED 4'],['eval 1'+' '.repeat(4089),'1'],
 ['eval 1'+' '.repeat(4090),'REFUSED 4'],
 ['eval '+Array.from({length:260},(_,i)=>`x+${i+1}`).join('+'),'REFUSED 5']
])test(input,want,'boundary/refusal');
// Record exactly how each supplied Wolfram output is translated; consume every fixture.
const wf=JSON.parse(fs.readFileSync('evidence/math-native/wolfram-oracle.json'));
for(const w of wf.arithmetic)test('eval '+w.expr,w.result,'Wolfram actual output');
const wc=['expand (x+1)^5','diff 3*x^5-7*x^2+2','integ 6*x^2-4*x+1','gcd x^4-1 ; x^3-x'];
wf.polynomials.forEach((w,i)=>test(wc[i],w.result.replaceAll(' ',''),'Wolfram actual output'));
test('solve 3*x+7 ; 22',wf.linear[0].result.split('=')[1],'Wolfram actual output');
test('linear 2;1;1;-1;5;1',wf.linear[1].result.slice(1,-1).replace(',', ';'),'Wolfram actual output');
const exe=process.argv[2]||'build/math-native/omega-math';
const log=[];
// Batches avoid the legacy compiler's million-iteration streaming loop cap.
for(let start=0;start<cases.length;start+=100){
 const batch=cases.slice(start,start+100), input=batch.map(x=>x.input).join('\n')+'\n';
 const r=spawnSync(exe,[],{input,encoding:'utf8',timeout:60000});
 if(r.error||r.status!==0)throw Error(`runtime ${r.status} ${r.error} ${r.stderr}`);
 const out=r.stdout.trimEnd().split('\n');
 if(out.length!==batch.length)throw Error(`line count ${out.length} != ${batch.length}`);
 batch.forEach((c,i)=>log.push({...c,got:out[i],ok:c.want===out[i]}));
}
for(const row of log)console.log(JSON.stringify(row));
const failed=log.filter(c=>!c.ok);
console.log(JSON.stringify({requested:cases.length,executed:log.length,passed:log.length-failed.length,failed:failed.length,wolframRequested:9,wolframExecuted:log.filter(c=>c.kind==='Wolfram actual output').length,reference:'Node '+process.version+' arbitrary precision BigInt',sourceHash:createHash('sha256').update(fs.readFileSync(import.meta.filename)).digest('hex')}));
if(failed.length){console.error(failed.slice(0,10));process.exit(1);}
