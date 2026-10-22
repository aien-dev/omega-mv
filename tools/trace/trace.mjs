#!/usr/bin/env node
import {OwnedRoot} from './paths.mjs';
import {searchtrace,argus,dual} from './observers.mjs';
import {Archive,readArchive,recover,defaultRoot} from './archive.mjs';
import {query,compare,exportTable} from './query.mjs';
import {canonical,fail,isDigest} from './schema.mjs';
import {renderViewer} from './viewer.mjs';
import {contribution} from './share.mjs';
import {writeOutput} from './output.mjs';
export async function main(args){
 const options={},pos=[];for(let i=0;i<args.length;i++){if(args[i].startsWith('--')){const k=args[i].slice(2);if(!['input-root','input','context','verifier','verifier-hash','calibration-state','root','source','session','semantic','realization','step','type','status','range','format','output','bytes','age-ms','fields','preset','other','run','destination','name','pinned'].includes(k)||k in options||i+1>=args.length)fail('invalid CLI option');options[k]=args[++i];}else pos.push(args[i]);}
 const readFilters=['source','session','semantic','realization','step','type','status','range'];
 const specs={list:[0,readFilters],stats:[0,readFilters],show:[1,readFilters],steps:[1,readFilters],events:[1,readFilters],verify:[1,readFilters],compare:[2,[]],export:[1,[...readFilters,'format','output']],viewer:[1,[...readFilters,'other','output']],'share-preview':[1,[...readFilters,'fields','preset']],'share-package':[1,[...readFilters,'fields','preset','output']],retain:[0,['bytes','age-ms']],purge:[0,[]],pin:[0,['name','pinned']],recover:[0,['destination']],'import-searchtrace':[0,['input-root','input','context','verifier','verifier-hash']],'import-argus':[0,['input-root','input','context']],'import-dual':[0,['input-root','input','context','calibration-state']]};
 const spec=specs[pos[0]];if(!spec||pos.length!==spec[0]+1||Object.keys(options).some(k=>k!=='root'&&!spec[1].includes(k)))fail('invalid command arguments or options');
 const root=options.root??defaultRoot(),[command,a,b]=pos;const filter={};if(a&&['show','steps','events','verify','export','viewer','share-preview','share-package'].includes(command))filter.run=a;
 for(const k of ['source','session','semantic','realization','type','status'])if(k in options)filter[k]=options[k];if(options.step!==undefined)filter.step=Number(options.step);
 if(options.range!==undefined){const r=options.range.split(':');if(r.length!==2)fail('range FROM:TO required');filter.from=r[0];filter.to=r[1];}
 let result;
 if(['import-searchtrace','import-argus','import-dual'].includes(command)){
 if(!options['input-root']||!options.input||!options.context)fail('owned input root, input filename and context required');
 const input=new OwnedRoot(options['input-root']);let bytes,context;try{bytes=input.read(options.input,8*1024*1024);context=JSON.parse(input.read(options.context,16384));}finally{input.close();}
 const events=command==='import-searchtrace'?searchtrace(bytes,context,{verifier:options.verifier,verifierHash:options['verifier-hash']}):command==='import-argus'?argus(bytes,context):dual(bytes,context,{state:options['calibration-state']??'uncalibrated'});
 const store=new Archive(root);try{store.append(events);}finally{store.close();}return canonical({schema:'omega.trace.import.v1',events:events.length,source:command.slice(7),evidence:'NOT_VERIFIED'})+'\n';}

 if(['retain','purge','pin'].includes(command)){const store=new Archive(root);try{if(command==='purge')result={removed:store.purge()};else if(command==='pin'){if(!options.name||!['true','false'].includes(options.pinned))fail('name/pinned required');store.pin(options.name,options.pinned==='true');result={pinned:options.name,value:options.pinned==='true'};}else result={removed:store.retain({bytes:options.bytes===undefined?Number.MAX_SAFE_INTEGER:Number(options.bytes),olderThanMs:options['age-ms']===undefined?Infinity:Number(options['age-ms'])})};}finally{store.close();}return canonical({schema:'omega.trace.control.v1',...result})+'\n';}
 if(command==='recover'){if(!options.destination)fail('new destination required');return canonical({schema:'omega.trace.recovery.v1',...recover(root,options.destination)})+'\n';}
 const archive=readArchive(root);
 switch(command){
 case 'viewer':{
 if(!isDigest(a)||!options.output)fail('explicit run and local HTML output required');const q=query(archive,filter),comparison=options.other?compare(archive,a,options.other):null;writeOutput(options.output,renderViewer(q,{comparison}),root);return canonical({schema:'omega.trace.viewer.v1',integrity:q.integrity,source_segments:q.source_segments,listener:false})+'\n';}
 case 'share-preview':case 'share-package':{
 if(!isDigest(a)||!options.fields)fail('explicit run and fields required');
 const packaged=contribution(query(archive,filter),{fields:options.fields.split(','),preset:options.preset??'summary'});
 if(command==='share-package'){if(!options.output)fail('explicit local output required');writeOutput(options.output,packaged.bytes,root);}return packaged.bytes;}
 case 'list':{const q=query(archive,filter);result={schema:'omega.trace.runs.v1',query:q.query,integrity:q.integrity,problems:q.problems,runs:[...new Set(q.events.map(e=>e.run))].sort()};break;}
 case 'steps':if(!isDigest(a))fail('explicit run required');filter.type='step';result=query(archive,filter);break;
 case 'show':case 'events':case 'verify':if(!isDigest(a))fail('explicit run required');result=query(archive,filter);if(command==='verify')result={schema:'omega.trace.verify.v1',run:a,integrity:result.integrity,evidence:'NOT_VERIFIED',problems:result.problems,source_segments:result.source_segments};break;
 case 'stats':result=query(archive,filter);break;
 case 'compare':result=compare(archive,a,b);break;
 case 'export':if(!isDigest(a))fail('explicit run required');result=query(archive,filter);{const bytes=exportTable(result,options.format??'json');if(options.output)writeOutput(options.output,bytes,root);return options.output?canonical({schema:'omega.trace.export.v1',source_segments:result.source_segments,query:result.query})+'\n':bytes;}
 default:fail('unknown command; use list/show/steps/events/compare/verify/stats/export/retain/purge/pin/recover');
 }
 return canonical(result)+'\n';
}
if(import.meta.url===new URL(process.argv[1],'file:').href){try{process.stdout.write(await main(process.argv.slice(2)));}catch(e){process.stderr.write('trace refused: '+(e.code??e.message)+'\n');process.exitCode=2;}}
