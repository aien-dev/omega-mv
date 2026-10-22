import {parentPort,workerData} from 'node:worker_threads';
import {Archive} from './archive.mjs';
let a;
try{if(workerData.writerSession===null)throw new Error(workerData.startupError??'writer startup refused');a=new Archive(workerData.root,{segmentBytes:workerData.segmentBytes,writerSession:workerData.writerSession});}catch(e){parentPort.postMessage({error:`archive writer: ${e.code??e.message}`});}
parentPort.on('message',m=>{
 try{if(m.event){if(a)a.append([m.event]);parentPort.postMessage({ack:true,persisted:!!a});}
 if(m.close){if(a)a.append(m.losses);a?.close();parentPort.close();}}
 catch(e){parentPort.postMessage({error:`archive persistence: ${e.code??e.message}`});a?.handoff();a=undefined;if(m.close)parentPort.close();else parentPort.postMessage({ack:true});}
});
