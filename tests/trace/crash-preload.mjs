// Synthetic child-process cut only. Inherited by its background worker.
import {isMainThread,parentPort} from 'node:worker_threads';
if(!isMainThread&&process.env.OMEGA_TRACE_TEST_CUT==='startup'){
 parentPort.postMessage({test_cut:'before_worker_open'});
 Atomics.wait(new Int32Array(new SharedArrayBuffer(4)),0,0);
}
if(!isMainThread&&process.env.OMEGA_TRACE_TEST_CUT==='queued'){
 const fs=(await import('node:fs')).default,sync=fs.fsyncSync;
 fs.fsyncSync=function(fd){sync(fd);if(fs.readlinkSync(`/proc/self/fd/${fd}`).endsWith('/.writer.lock')){
  parentPort.postMessage({test_cut:'after_writer_lock_fsync'});
  Atomics.wait(new Int32Array(new SharedArrayBuffer(4)),0,0);
 }};
}
if(!isMainThread&&process.env.OMEGA_TRACE_TEST_CUT==='startup-failure'){
 const fs=(await import('node:fs')).default,sync=fs.fsyncSync;
 fs.fsyncSync=function(fd){if(fs.readlinkSync(`/proc/self/fd/${fd}`).endsWith('/.writer.lock')){const e=new Error('synthetic fsync failure');e.code='EIO';throw e;}return sync(fd);};
}
