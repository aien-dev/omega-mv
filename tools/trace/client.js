(() => {
 'use strict';
 const data=JSON.parse(document.getElementById('trace-data').textContent),q=data.query;
 const get=id=>document.getElementById(id);
 const text=(tag,value)=>{const e=document.createElement(tag);e.textContent=String(value);return e;};
 const controls=['source','type','status','step','from','to'];
 for(const id of ['source','type','status','step']){const values=[...new Set(q.events.map(e=>e[id==='type'?'category':id]).filter(v=>v!==null))].sort((a,b)=>String(a).localeCompare(String(b),'en'));get(id).append(text('option','all'));for(const v of values){const e=text('option',v);e.value=String(v);get(id).append(e);}get(id).value='';}
 get('notice').textContent='Archive integrity '+q.integrity+'; evidence NOT_VERIFIED. '+q.summary.qualifications.join(', ')+'. Source segments: '+q.source_segments.join(', ')+'. '+q.coverage+'.';
 if(data.comparison)get('comparison').textContent=(data.comparison.directly_comparable?'Matching recorded contracts':'Comparison qualified: '+data.comparison.differences.join(', '))+'. Left/right units and missing measurements remain separate. '+JSON.stringify({left_run:data.comparison.left.query.run,right_run:data.comparison.right.query.run,left_contracts:data.comparison.left.events.map(e=>e.contract),right_contracts:data.comparison.right.events.map(e=>e.contract),left_receipts:[...new Set(data.comparison.left.events.flatMap(e=>e.receipts))],right_receipts:[...new Set(data.comparison.right.events.flatMap(e=>e.receipts))],left:data.comparison.left.summary,right:data.comparison.right.summary});
 else get('comparison').textContent='No second run selected.';
 function draw(){
  get('events').replaceChildren();get('charts').replaceChildren();
  try{
   const state=viewState(q.events,Object.fromEntries(controls.map(k=>[k,get(k).value])));
   get('warning').textContent=(q.problems.length?'INCONCLUSIVE: '+q.problems.join('; ')+'. ':'')+(state.events.length?'':'no events in selected range. ')+ 'Dropped events in selection: '+state.dropped+'. Full run dropped disclosures: '+JSON.stringify(q.summary.dropped)+'. Retained-out segments: '+q.retention.length+'. Crash loss may be unknown.';
   get('measurements').textContent='Memory: '+JSON.stringify(state.memory)+'. Energy: '+JSON.stringify(state.energy)+'. Estimated rcycles are model costs. No silicon time is inferred.';
   for(const e of state.events){const row=text('li',e.category+' step '+e.step+' '+e.status+' '+e.qualification+' time '+e.time.domain+':'+e.time.tick+' parent '+(e.parent??'unknown/root')+' '+JSON.stringify(e.metrics));row.setAttribute('id','event-'+e.event_id);if(e.parent){const parent=text('a',' causal parent '+e.parent.slice(0,12));parent.setAttribute('href','#event-'+e.parent);row.append(parent);}for(const r of e.receipts){const a=text('a',' receipt '+r);a.setAttribute('href','#receipt-'+r);row.append(a);const reference=text('span',' digest reference, independently unverified');reference.setAttribute('id','receipt-'+r);row.append(reference);}get('events').append(row);}
   for(const chart of state.charts){const box=document.createElement('section');box.append(text('h3',chart.name+' ('+chart.unit+', '+chart.provenance+', '+chart.qualification+')'));
    const svg=document.createElementNS('http://www.w3.org/2000/svg','svg');svg.setAttribute('viewBox','0 0 600 160');svg.setAttribute('role','img');svg.setAttribute('aria-label',chart.name+' by selected training step');
    const values=chart.points.map(p=>p.value),min=values.reduce((a,b)=>Math.min(a,b),Infinity),max=values.reduce((a,b)=>Math.max(a,b),-Infinity),span=max-min||1,line=document.createElementNS('http://www.w3.org/2000/svg','polyline');line.setAttribute('fill','none');line.setAttribute('stroke','currentColor');line.setAttribute('points',chart.points.map((p,i)=>((i/Math.max(1,values.length-1))*580+10)+','+(145-(p.value-min)/span*130)).join(' '));svg.append(line);box.append(svg,text('p','min '+min+'; max '+max+'; steps '+chart.points.map(p=>p.step).join(', ')));get('charts').append(box);
   }
  }catch(e){get('warning').textContent='Filter refused: '+e.message;}
 }
 for(const k of controls)get(k).addEventListener(['from','to'].includes(k)?'input':'change',draw);
 get('reset').addEventListener('click',()=>{for(const k of controls)get(k).value='';draw();});draw();
})();
