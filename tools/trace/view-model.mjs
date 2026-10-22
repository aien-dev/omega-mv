// Pure selection/presentation functions shared by the generated offline artifact.
export function viewState(events,{source='',type='',status='',step='',from='',to=''}={}){
 for(const n of [from,to,step])if(n!==''&&!/^(0|[1-9][0-9]{0,19})$/.test(n))throw new Error('invalid range/step');
 if(from!==''&&to!==''&&BigInt(from)>BigInt(to))throw new Error('reversed range');
 const selected=events.filter(e=>(!source||e.source===source)&&(!type||e.category===type)&&(!status||e.status===status)&&(step===''||String(e.step)===step)&&(from===''||BigInt(e.time.tick)>=BigInt(from))&&(to===''||BigInt(e.time.tick)<=BigInt(to)));
 if((from!==''||to!=='')&&new Set(selected.map(e=>e.time.domain)).size>1)throw new Error('range spans unmapped clock domains');
 const charts=[];for(const name of ['loss','rate','gradient','duration']){
  const groups=new Map();for(const e of selected.filter(e=>e.category==='step'))for(const m of e.metrics.filter(m=>m.name===name&&m.value!==null)){const key=m.unit+':'+m.provenance+':'+e.qualification;if(!groups.has(key))groups.set(key,{name,unit:m.unit,provenance:m.provenance,qualification:e.qualification,points:[]});groups.get(key).points.push({step:e.step,value:m.value});}charts.push(...groups.values());
 }
 const loss=selected.flatMap(e=>e.metrics).filter(m=>m.name==='dropped');
 const dropped=loss.some(m=>m.value===null)?'unknown':loss.reduce((n,m)=>n+m.value,0);
 const memory=selected.flatMap(e=>e.metrics).filter(m=>m.name==='memory'&&m.value!==null),energy=selected.flatMap(e=>e.metrics).filter(m=>m.name==='energy'&&m.provenance==='observed'&&m.value!==null);
 return {events:selected,charts,dropped,memory:memory.length?memory:'unavailable',energy:energy.length?energy:'unavailable'};
}
