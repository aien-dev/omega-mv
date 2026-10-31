// Independent test oracle from the published layout; links no producer code.
import {hash,parent} from './fixtures.mjs';
const reject=(code,why)=>{throw Object.assign(Error(why),{code});};
export function inspect(b){
 try {
 const ck=(yes,code,why)=>{if(!yes)reject(code,why);};
 ck(b.length<=8192,67,'input bound');ck(b.length>=13,65,'short envelope');ck(b.subarray(0,4).equals(Buffer.from('OMSB')),65,'envelope magic');ck(b[4]===1,66,'envelope version');
 const sn=b.readUInt32LE(5),pn=b.readUInt32LE(9);ck(sn<=1324&&pn<=4682,67,'length bounds');ck(b.length===13+sn+pn,65,'length exact');ck(sn>=44,65,'short semantic');
 const s=b.subarray(13,13+sn);ck(s.subarray(0,4).equals(Buffer.from('OMSA')),65,'semantic magic');ck(s[4]===1&&s[7]===1,66,'semantic version');
 const [lang,profile,runtime,abi]=[s[5],s[6],s[8],s[9]],n=s.readUInt16LE(10);ck(n>0&&n<=64,67,'op bound');ck(sn===44+20*n,65,'op length');ck(s.subarray(12,44).equals(parent),78,'contract pin');
 const langs=[0,1,2,3,3,4,3];ck(profile>=1&&profile<=6&&lang===langs[profile],68,'profile language');
 ck([3,6].includes(profile)?[1,2].includes(runtime)&&abi===(profile===6?2:1):runtime===0&&abi===1,68,'runtime ABI');
 let ops=[],slots=new Set();
 for(let i=0;i<n;i++){
  let o=s.subarray(44+20*i,64+20*i);const [dialect,version,code,type,attr,effect,alias,ob]=o;const a=o.readUInt16LE(8),v=o.readUInt16LE(10),imm=o.readBigUInt64LE(12);
  ck(dialect===lang,68,'op dialect');ck(version===1,66,'op version');ck(type>=1&&type<=4,70,'type');ck(profile===6||type===(profile===3?2:1),68,'profile type');ck(code>=1&&code<=9,69,'op');
  const pure=()=>ck(effect===0&&alias===0,71,'pure effects'),ref=(idx,t)=>ck(idx<i&&ops[idx]?.type===t,70,'SSA typed reference'),als=x=>x===255||(x>=1&&x<=8);
  if(code===1){ck(type<=2&&attr===type&&a===65535&&v===65535,70,'constant shape');ck(ob===0,72,'constant obligations');pure();}
  else if(code<=5){ck(type<=2&&attr===type,70,'arithmetic type');ref(a,type);ref(v,type);ck(imm===0n,65,'arithmetic immediate');let need;
   if(type===1){pure();need=1+([4,6].includes(profile)?4:0);}else{ck(effect===20&&alias===255,71,'bigint effects');need=8;}
   if(code===5)need|=2;ck(ob===need,72,'arithmetic obligations');
  }else if(code===6){ck(profile===6,68,'dynamic profile');ck(type===3&&attr===3,70,'dynamic type');ref(a,3);ck(v===65535,70,'dynamic B');ck(imm===0n,65,'dynamic immediate');ck(effect===63&&alias===255,71,'dynamic effects');ck(ob===8,72,'dynamic obligations');
  }else if(code<=8){ck(profile===6,68,'memory profile');ck(type===1&&attr===4,70,'memory type');ref(a,4);ref(v,1);ck(als(alias)&&alias===ops[a].alias,71,'memory alias');
   if(code===8){ck(imm<64n,70,'store reference bound');ref(Number(imm),1);}else ck(imm===0n,65,'load immediate');
   ck(effect===(code===7?17:19),71,'memory effects');ck(ob===24,72,'memory obligations');
  }else{ck(attr===0&&a===65535&&v===65535,70,'input shape');ck(imm<=5n,67,'slot bound');ck(!slots.has(imm),70,'duplicate slot');slots.add(imm);ck(ob===1,72,'input obligation');ck(effect===0&&(type===4?als(alias):alias===0),71,'input alias/effects');}
  ops.push({code,type,attr,effect,alias,ob,a,b:v,imm});
 }
 let source,encoding,spans;
 if(pn){let p=b.subarray(13+sn);ck(p.length>=74,65,'short sidecar');ck(p.subarray(0,4).equals(Buffer.from('OMSP')),65,'sidecar magic');ck(p[4]===1,66,'sidecar version');encoding=p[5];ck(encoding<=1,76,'encoding');let size=p.readUInt32LE(6);ck(size<=4096,67,'source bound');ck(pn===74+8*n+size,65,'sidecar length');source=p.subarray(74+8*n);
  ck(hash(s).equals(p.subarray(10,42))&&hash(source).equals(p.subarray(42,74)),74,'association digest');
  if(encoding===1){try{new TextDecoder('utf-8',{fatal:true,ignoreBOM:true}).decode(source);}catch{reject(76,'UTF8');}}
  spans=[];for(let i=0;i<n;i++){let a=p.readUInt32LE(74+8*i),z=p.readUInt32LE(78+8*i);ck(a<=z&&z<=size,75,'span range');
   if(encoding===1)for(let v of [a,z])ck(v===size||(source[v]&192)!==128,75,'span boundary');spans.push([a,z]);}
 }
 return {code:0,lang,profile,runtime,abi,ops,source,encoding,spans};
 }catch(e){if(e.code)return {code:e.code,why:e.message};throw e;}
}
export function legality(u,caps){let [target,runtime,abi,effects]=caps;return Buffer.from(u.ops.flatMap(o=>{
 let status=abi!==u.abi||(o.effect&effects)!==o.effect?4:u.runtime!==0&&u.runtime!==runtime?2:target===1?1:[3,6].includes(u.profile)?4:o.type===1&&(o.code<=5||o.code===9)?3:4;return [status,o.ob];
}));}
