import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {event,metric} from '../../tools/trace/schema.mjs';
export const id=n=>n.toString(16).padStart(64,'0');
export function fixture(n=1,extra={}){return event({run:id(1),session:id(2),producer:1,sequence:String(n),parent:null,time:{domain:'logical',tick:String(n),wall_ns:null},contract:{machine:id(3),build:id(4),config:id(5),precision:'f32'},source:'train',category:'step',status:'ok',qualification:'synthetic',semantic:null,realization:null,step:n,metrics:[metric('duration',n,'ns','observed')],receipts:[],...extra});}
export function temp(t){const p=fs.mkdtempSync(path.join(os.tmpdir(),'omega-trace-'));t.after(()=>fs.rmSync(p,{recursive:true,force:true}));return p;}
