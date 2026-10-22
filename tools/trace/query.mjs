import {canonical,digest,validate,integer,isDigest,uint,fail,SOURCES,CATEGORIES} from './schema.mjs';
const FILTERS=['run','session','source','semantic','realization','step','from','to','status','type'];
export function checkFilter(f){
 if(!f||Object.getPrototypeOf(f)!==Object.prototype||Object.keys(f).some(k=>!FILTERS.includes(k)))fail('unknown query field');
 for(const k of ['run','session','semantic','realization'])if(k in f&&!isDigest(f[k]))fail('invalid query identity');
 for(const [k,allowed] of [['source',SOURCES],['type',CATEGORIES],['status',['ok','refused','pruned','solved','rejected','unknown','uncalibrated','stale']]])if(k in f&&!allowed.includes(f[k]))fail('invalid query enum');
 if('step'in f)integer(f.step);for(const k of ['from','to'])if(k in f)uint(f[k]);if('from'in f&&'to'in f&&BigInt(f.from)>BigInt(f.to))fail('reversed range');
 return f;
}
function order(a,b){for(const [x,y]of [[a.run,b.run],[a.time.domain,b.time.domain]]){if(x<y)return -1;if(x>y)return 1;}for(const [x,y]of [[BigInt(a.time.tick),BigInt(b.time.tick)],[a.producer,b.producer],[BigInt(a.sequence),BigInt(b.sequence)],[a.event_id,b.event_id]]){if(x<y)return -1;if(x>y)return 1;}return 0;}
function distribution(values){if(!values.length)return null;const v=[...values].sort((a,b)=>a-b),sum=v.reduce((a,b)=>a+b,0);if(!Number.isFinite(sum))return {available:false,reason:'numeric_overflow',count:v.length};return {available:true,count:v.length,total:sum,min:v[0],max:v.at(-1),mean:sum/v.length,p50:v[Math.ceil(v.length*.5)-1],p95:v[Math.ceil(v.length*.95)-1]};}
export function summarize(events){
 const groups=new Map();for(const e of events)for(const m of e.metrics){const k=[m.name,m.unit,m.provenance].join(':');if(!groups.has(k))groups.set(k,{name:m.name,unit:m.unit,provenance:m.provenance,values:[],missing:0});const g=groups.get(k);if(m.value===null)g.missing++;else g.values.push(m.value);}
 const metrics=[...groups.entries()].sort(([a],[b])=>a.localeCompare(b,'en')).map(([,g])=>({name:g.name,unit:g.unit,provenance:g.provenance,missing:g.missing,distribution:distribution(g.values)}));
 const energy=metrics.filter(m=>m.name==='energy'&&m.provenance==='observed'&&m.distribution?.available);
 return {events:events.length,steps:events.filter(e=>e.category==='step').length,qualifications:[...new Set(events.map(e=>e.qualification))].sort(),metrics,
 energy:energy.length?{available:true,measurements:energy}:{available:false,reason:'not_measured'},
 dropped:events.flatMap(e=>e.metrics.filter(m=>['dropped','critical_loss'].includes(m.name)).map(m=>({producer:e.producer,source:e.source,category:e.category,...(e.metrics.some(x=>x.name==='lost_source')?{lost_source:SOURCES[e.metrics.find(x=>x.name==='lost_source').value],lost_category:CATEGORIES[e.metrics.find(x=>x.name==='lost_category')?.value]}:{}),metric:m}))),loss_extent:'only_explicitly_recorded; crash_loss_may_be_unknown'};
}
export function query(archive,filter={}){
 checkFilter(filter);const all=archive.events.map(validate),known=new Map(all.map(e=>[e.event_id,e]));const events=all.filter(e=>{
  for(const [k,v]of Object.entries(filter)){if(k==='from'){if(BigInt(e.time.tick)<BigInt(v))return false;}else if(k==='to'){if(BigInt(e.time.tick)>BigInt(v))return false;}else if(e[k==='type'?'category':k]!==v)return false;}return true;
 }).sort(order),visible=new Set(events.map(e=>e.event_id));
 const problems=[...archive.problems];if(filter.run&&!events.length)problems.push('run absent or outside selection');if(('from'in filter||'to'in filter)&&new Set(events.map(e=>e.time.domain)).size>1)problems.push('mixed clock domains; no common time mapping');for(const e of events)if(e.parent!==null){const p=known.get(e.parent);if(!p)problems.push('missing causal parent: '+e.event_id);else if(p.run!==e.run)problems.push('cross-run causal parent: '+e.event_id);}
 const loss_disclosures=[...all.filter(e=>e.category==='loss'),
  ...archive.retention.flatMap(r=>r.losses??[])].filter(e=>!filter.run||e.run===filter.run);
 const summary=summarize(events);
 summary.dropped=summarize(loss_disclosures).dropped;
 return {schema:'omega.trace.query.v1',query:{...filter},integrity:problems.length?'INCONCLUSIVE':archive.integrity,evidence:'NOT_VERIFIED',problems,coverage:archive.retention.length?'retained_segments_only':'recorded_segments_only',retention:archive.retention,
 source_segments:[...archive.segments.map(s=>s.actual_digest),...archive.retention.map(s=>s.digest)].sort(),events,summary,loss_disclosures,
 timeline:events.map(e=>({event_id:e.event_id,parent:e.parent,parent_known:e.parent===null||known.has(e.parent),parent_visible:e.parent===null||visible.has(e.parent),run:e.run,step:e.step,category:e.category,time:e.time,semantic:e.semantic,realization:e.realization,receipts:e.receipts,qualification:e.qualification}))};
}
export function compare(archive,a,b){if(a===b)fail('choose two different runs');const left=query(archive,{run:a}),right=query(archive,{run:b}),differences=[];
 for(const field of ['machine','build','config','precision']){const l=[...new Set(left.events.map(e=>e.contract[field]))].sort(),r=[...new Set(right.events.map(e=>e.contract[field]))].sort();if(l.length!==1||r.length!==1||canonical(l)!==canonical(r)||l[0]==='unknown')differences.push(field);}
 if(left.events.length===0||right.events.length===0)differences.push('missing_run');if(left.integrity!=='PASS'||right.integrity!=='PASS')differences.push('incomplete_archive');
 return {schema:'omega.trace.comparison.v1',directly_comparable:differences.length===0,differences,evidence:'NOT_VERIFIED',left,right};
}
const csvCell=v=>'"'+String(v??'').replaceAll('"','""')+'"';
export function exportTable(result,format){if(format==='json')return canonical(result)+'\n';if(format!=='csv')fail('unknown export format');
 const lines=[['schema','source_segments','query','integrity','evidence','name','unit','provenance','missing','count','total','mean','p50','p95']];
 for(const m of result.summary.metrics)lines.push(['omega.trace.table.v1',canonical(result.source_segments),canonical(result.query),result.integrity,result.evidence,m.name,m.unit,m.provenance,m.missing,m.distribution?.count,m.distribution?.total,m.distribution?.mean,m.distribution?.p50,m.distribution?.p95]);
 if(lines.length===1)lines.push(['omega.trace.table.v1',canonical(result.source_segments),canonical(result.query),result.integrity,result.evidence]);return lines.map(r=>r.map(csvCell).join(',')).join('\n')+'\n';
}
