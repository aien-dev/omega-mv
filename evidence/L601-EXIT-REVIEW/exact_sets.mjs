import {readFileSync as read,writeFileSync as write}from'node:fs';
const original=['bad-extra','empty-extra','plus-only-extra','minus-only-extra','overflow-positive-extra','overflow-negative-extra','signed-bad-extra','many-invalid-extra'];
const lexical={
 'reject-blanks':['space-trailing','space-both','space-extra','tab-trailing','tab-both','tab-extra','delimiter-space-extra','pipeline-first-space','pipeline-last-space'],
 'accept-all-whitespace':['newline','return','vertical','formfeed'].flatMap(n=>['trailing','both','extra'].map(x=>n+'-'+x)).concat(['mixed-space','mixed-space-extra']),
 'ignore-delimiter':['delimiter-no-operand','delimiter-valid','delimiter-negative','delimiter-valid-extra','delimiter-space-extra','prior-delimiter','pipeline-first-delimiter','pipeline-last-delimiter','pipeline-first-extra','pipeline-last-extra'],
 'repeat-delimiter':['delimiter-again','delimiter-again-extra']};
const records=[];for(const leg of ['plain','sanitizer'])for(const name of ['count-first','invalid-extra-continues',...Object.keys(lexical)]){
 const group=lexical[name]?'new-mutants':'numeric-mutants';const names=lexical[name]??original.concat(name==='count-first'?['pipeline-first-invalid','pipeline-last-invalid']:[]);
 const path='targets/L601/author592/raw/'+group+'/'+name+'-'+leg+'-cases/results.json';const rows=JSON.parse(read(path)).rows;
 if(rows.some(x=>x.ok===names.includes(x.name)))throw Error('incorrect exact set '+name+' '+leg);
 records.push({name,leg,failures:rows.filter(x=>!x.ok).length,names});}
write('targets/L601/exact-sets.json',JSON.stringify(records,null,2));console.log('EXACT_SETS_PASS 12 archived compiled controls, each exact named assertion set');
