import {parentPort,workerData} from 'node:worker_threads';
import {Archive} from './archive.mjs';
let a;
try{a=new Archive(workerData.root,{segmentBytes:workerData.segmentBytes});}catch(e){parentPort.postMessage({error:`archive writer: ${e.code??e.message}`});}
parentPort.on('message',m=>{
 try{if(m.event){if(a)a.append([m.event]);parentPort.postMessage({ack:true});}
 if(m.close){if(a)a.append(m.losses);a?.close();parentPort.close();}}
 catch(e){parentPort.postMessage({error:`archive persistence: ${e.code??e.message}`});a?.close();a=undefined;if(m.close)parentPort.close();else parentPort.postMessage({ack:true});}
});
