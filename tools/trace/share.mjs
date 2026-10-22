import {canonical,digest,hash,METRICS,validate,isDigest,fail,keys} from './schema.mjs';
import {query,summarize,checkFilter} from './query.mjs';
const FIELDS=new Set([...METRICS,'receipts']);
const pseudonym=(kind,value)=>hash('omega.trace.contribution.v1:'+kind+':'+value);
export function contribution(result,{fields,preset='summary'}={}){
 if(result?.schema!=='omega.trace.query.v1'||result.integrity!=='PASS'||result.evidence!=='NOT_VERIFIED')fail('conclusive known query required');
 keys(result,['schema','query','integrity','evidence','problems','coverage','retention','source_segments','events','summary','timeline','loss_disclosures']);
 if(!['recorded_segments_only','retained_segments_only'].includes(result.coverage)||!Array.isArray(result.retention))fail('unknown disclosure metadata');
 checkFilter(result.query);if(!isDigest(result.query.run))fail('explicit run selection required');
 if(!Array.isArray(result.source_segments)||!result.source_segments.length||result.source_segments.length>4096||result.source_segments.some(x=>!isDigest(x)))fail('source segment provenance required');
 if(!Array.isArray(fields)||!fields.length||fields.length>METRICS.length+1||fields.some(f=>!FIELDS.has(f))||new Set(fields).size!==fields.length)fail('explicit unique field allowlist required');
 if(!['summary','spans'].includes(preset))fail('unknown contribution preset');
 const events=result.events.map(validate);if(events.some(e=>e.run!==result.query.run))fail('selection crosses run boundary');if(!events.length)fail('empty selection refused');
 if(!Array.isArray(result.loss_disclosures))fail('missing run loss disclosure');
 const losses=result.loss_disclosures.map(validate);
 if(losses.some(e=>e.category!=='loss'||e.run!==result.query.run))fail('invalid run loss disclosure');
 const visible=new Set(events.map(e=>e.event_id));if(events.some(e=>e.parent!==null&&!visible.has(e.parent)))fail('selection has unresolved causal references');
 const selected=[...fields].sort(),metricNames=new Set(selected.filter(f=>f!=='receipts'));
 const contracts=[...new Map(events.map(e=>{const c={machine:pseudonym('machine',e.contract.machine),build:pseudonym('build',e.contract.build),config:pseudonym('config',e.contract.config),precision:e.contract.precision};return [canonical(c),c];})).values()].sort((a,b)=>canonical(a).localeCompare(canonical(b),'en'));
 const filtered=events.map(e=>({...e,metrics:e.metrics.filter(m=>metricNames.has(m.name))})),summary=summarize(filtered),full=summarize(events);
 full.dropped=summarize(losses).dropped;
 const safeQuery=Object.fromEntries(Object.entries(result.query).map(([k,v])=>[k,['run','session','semantic','realization'].includes(k)?pseudonym(k,v):v]));
 const payload={schema:'omega.trace.contribution.v1',classification:'sanitized_numeric_research_candidate',local_only:true,transfer_permission:false,training_permission:false,consent_receipt:null,preset,fields:selected,query:safeQuery,source_segments:[...new Set(result.source_segments)].sort(),contracts,
 evidence_qualification:'archive_integrity_only; independent_receipt_verification_NOT_RUN',
 disclosures:{qualifications:full.qualifications,coverage:result.coverage,retained_out_segments:result.retention.length,dropped:full.dropped,loss_extent:'explicit_counts_only; crash_loss_unknown',privacy_surface:{numeric_fields:selected,receipts:selected.includes('receipts')?'selected_digest_references':'excluded',raw_content:'excluded',identities:'domain_hashed'}},
 summary:{events:summary.events,steps:summary.steps,metrics:summary.metrics,energy:summary.energy}};
 if(selected.includes('receipts'))payload.receipts=[...new Set(events.flatMap(e=>e.receipts))].sort();
 if(preset==='spans')payload.spans=filtered.map(e=>({id:pseudonym('event',e.event_id),parent:e.parent===null?null:pseudonym('event',e.parent),run:pseudonym('run',e.run),session:pseudonym('session',e.session),producer:e.producer,sequence:e.sequence,step:e.step,time:{domain:e.time.domain,tick:e.time.tick,wall_ns:null},source:e.source,category:e.category,status:e.status,qualification:e.qualification,semantic:e.semantic===null?null:pseudonym('semantic',e.semantic),realization:e.realization===null?null:pseudonym('realization',e.realization),metrics:e.metrics}));
 const content_hash=digest(payload),bytes=canonical({...payload,content_hash})+'\n';return {bytes,digest:hash(bytes)};
}
