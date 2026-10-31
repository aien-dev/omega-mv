import {op,unit,hash} from './fixtures.mjs';
export const seed=0x6210574;
let state=seed;
const rnd=()=>{state^=state<<13;state^=state>>>17;state^=state<<5;return state>>>0;};
const int64=()=>BigInt(rnd())<<32n|BigInt(rnd());
export function valid(i){
 let profile=i%6+1,lang=[0,1,2,3,3,4,3][profile],runtime=[3,6].includes(profile)?1+i%2:0,abi=profile===6?2:1;
 let type=profile===3?2:1,n=i%43===0?64:3+rnd()%12,ops=[];
 const add=x=>ops.push(op({lang,...x}));
 if(profile===6){
  add({code:9,type:3,attr:0,ob:1,imm:0n});add({code:6,type:3,attr:3,effect:63,alias:255,ob:8,a:0});
  const alias=1+i%8;
  add({code:9,type:4,attr:0,alias,ob:1,imm:1n});add({code:1,imm:int64()});
  add({code:7,attr:4,effect:17,alias,ob:24,a:2,b:3});
  add({code:8,attr:4,effect:19,alias,ob:24,a:2,b:3,imm:4n});
 }else{
  add({code:9,type,attr:0,ob:1,imm:0n});add({code:1,type,attr:type,imm:int64()});
  while(ops.length<n){let code=2+rnd()%4,j=ops.length,ob=type===2?8:1+(profile===4?4:0);if(code===5)ob|=2;
   add({code,type,attr:type,effect:type===2?20:0,alias:type===2?255:0,ob,a:rnd()%j,b:rnd()%j});}
 }
 let encoding=i%3===0?1:0,source;
 if(encoding===1)source=Buffer.from(`\0μ UTF8 😀\r\n${i} # ${rnd()}`);
 else {source=Buffer.alloc(i%101===0?4096:rnd()%128);for(let j=0;j<source.length;j++)source[j]=rnd()&255;}
 let spans=ops.map((_,j)=>encoding===1?[0,source.length]:[j%Math.max(1,source.length),source.length]);
 return unit({lang,profile,runtime,abi,ops,source,encoding,spans});
}
const offset=b=>13+b.readUInt32LE(5);
const rehash=b=>{let off=offset(b);hash(b.subarray(13,off)).copy(b,off+10);return b;};
const cat=(name,code,mutate)=>({name,code,mutate});
export const categories=[
 cat('envelope-magic',65,b=>{b[0]^=128;return b;}),
 cat('semantic-magic',65,b=>{b[13]^=128;return b;}),
 cat('sidecar-magic',65,b=>{b[offset(b)]^=128;return b;}),
 cat('envelope-version',66,b=>{b[4]=2;return b;}),
 cat('semantic-version',66,b=>{b[17]=2;return b;}),
 cat('dialect-version',66,b=>{b[20]=2;return b;}),
 cat('op-version',66,b=>{b[58]=2;return b;}),
 cat('sidecar-version',66,b=>{b[offset(b)+4]=2;return b;}),
 cat('unknown-operation',69,b=>{b[59]=255;return b;}),
 cat('unknown-type',70,b=>{b[60]=255;return b;}),
 cat('profile-language',68,b=>{b[18]=255;return b;}),
 cat('unknown-profile',68,b=>{b[19]=255;return b;}),
 cat('runtime-profile',68,b=>{b[21]=255;return b;}),
 cat('ABI-profile',68,b=>{b[22]=255;return b;}),
 cat('effect-set',71,b=>{b[62]=255;return b;}),
 cat('alias-set',71,b=>{b[63]=255;return b;}),
 cat('obligation',72,b=>{b[64]=255;return b;}),
 cat('forward-operand',70,b=>{b.writeUInt16LE(0,65);return b;}),
 cat('contract-pin',78,b=>{b[25]^=1;return b;}),
 cat('semantic-association',74,b=>{b[offset(b)+10]^=1;return b;}),
 cat('source-association',74,b=>{b[offset(b)+42]^=1;return b;}),
 cat('source-span-order',75,b=>{b.writeUInt32LE(1,offset(b)+74);b.writeUInt32LE(0,offset(b)+78);return b;}),
 cat('source-span-bound',75,b=>{b.writeUInt32LE(4097,offset(b)+78);return b;}),
 cat('source-oversized',67,b=>{b.writeUInt32LE(4097,offset(b)+6);return b;}),
 cat('unknown-encoding',76,b=>{b[offset(b)+5]=2;return b;}),
 cat('length-oversized',67,b=>{b.writeUInt32LE(0xffffffff,5);return b;}),
 cat('length-mismatch',65,b=>{b.writeUInt32LE(b.readUInt32LE(9)+1,9);return b;}),
 cat('trailing-byte',65,b=>Buffer.concat([b,Buffer.of(0)])),
 cat('truncated-source',65,b=>b.subarray(0,b.length-1)),
 cat('zero-operations',67,b=>{b.writeUInt16LE(0,23);return b;}),
 cat('oversized-operations',67,b=>{b.writeUInt16LE(65,23);return b;}),
 cat('malformed-UTF8',76,b=>{b[offset(b)+5]=1;let n=b.readUInt16LE(23),start=offset(b)+74+8*n;b[start]=0xff;hash(b.subarray(start)).copy(b,offset(b)+42);return b;}),
 cat('UTF8-span-boundary',75,_=>unit({source:Buffer.from('μ'),encoding:1,spans:[[1,2]]})),
 cat('UTF8-overlong',76,_=>unit({source:Buffer.from([0xe0,0x80,0x80]),encoding:1})),
 cat('UTF8-surrogate',76,_=>unit({source:Buffer.from([0xed,0xa0,0x80]),encoding:1})),
 cat('UTF8-too-large',76,_=>unit({source:Buffer.from([0xf4,0x90,0x80,0x80]),encoding:1})),
 cat('rawbyte-corruption',74,b=>{b[b.length-1]^=128;return b;}),
];
// Base variation makes even malformed fixed UTF8 shapes carry a unique semantic constant.
export function damaged(i){let c=categories[i%categories.length],base=unit({ops:[op({imm:int64()})],source:Buffer.from([0,13,10,255,rnd()%256])});let b=c.mutate(Buffer.from(base));
 if(c.name.startsWith('UTF8-')){b.writeBigUInt64LE(int64(),69);rehash(b);}return {category:c.name,code:c.code,input:b};}
