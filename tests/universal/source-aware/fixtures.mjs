// Independent test-only binary fixture construction, not a production serializer.
import {createHash} from 'node:crypto';
import fs from 'node:fs';
export const hash=b=>createHash('sha256').update(b).digest();
export const parent=hash(fs.readFileSync('spec/universal/semantics.md'));
export const u32=n=>{let b=Buffer.alloc(4); b.writeUInt32LE(n);return b;};
export function op({lang=1,code=1,type=1,attr=1,effect=0,alias=0,ob=0,a=65535,b=65535,imm=0n}={}){
 let v=Buffer.alloc(20);v.set([lang,1,code,type,attr,effect,alias,ob]);v.writeUInt16LE(a,8);v.writeUInt16LE(b,10);v.writeBigUInt64LE(BigInt(imm),12);return v;
}
export function unit({lang=1,profile=1,runtime=0,abi=1,ops=[op()],source=Buffer.from('a\r\n\0\xff','latin1'),encoding=0,spans,missing=false}={}){
 let h=Buffer.alloc(44);h.write('OMSA');h.set([1,lang,profile,1,runtime,abi],4);h.writeUInt16LE(ops.length,10);parent.copy(h,12);
 let sem=Buffer.concat([h,...ops]), p=Buffer.alloc(74+8*ops.length);p.write('OMSP');p.set([1,encoding],4);p.writeUInt32LE(source.length,6);hash(sem).copy(p,10);hash(source).copy(p,42);
 ops.forEach((_,i)=>{let [a,b]=spans?.[i]??[0,source.length];p.writeUInt32LE(a,74+i*8);p.writeUInt32LE(b,78+i*8);});
 let side=missing?Buffer.alloc(0):Buffer.concat([p,source]);return Buffer.concat([Buffer.from('OMSB\x01','latin1'),u32(sem.length),u32(side.length),sem,side]);
}
export function expected(mode,b){let s=b.subarray(13,13+b.readUInt32LE(5));if(mode===1||mode===2)return b;if(mode===3)return hash(Buffer.concat([Buffer.from('omega.source.semantic.v1'),s]));if(mode===4)return hash(Buffer.concat([Buffer.from('omega.source.provenance.v1'),b]));if(mode===5)return b.subarray(13+s.length+74+8*s.readUInt16LE(10));throw Error('mode');}
