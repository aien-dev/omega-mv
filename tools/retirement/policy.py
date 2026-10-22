#!/usr/bin/env python3
"""Fail-closed retirement reporting. PASS for an isolated port is not retirement.
Exit 0: consistent manifest (possibly PARTIAL); 1: --require-retired unmet;
2: inconsistent/unsupported claim or malformed/missing evidence.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT=Path(__file__).resolve().parents[2]
STAGES=('executable_equivalence','api_abi_data','isolated_candidate','coordinator_model_train','dependency_closure','transactional_swap_rollback','main_archive_smoke')


def unique_object(pairs):
    obj={}
    for key,value in pairs:
        if key in obj:raise ValueError('duplicate JSON field '+key)
        obj[key]=value
    return obj


def invalid_constant(value):
    raise ValueError('non-JSON constant '+value)


def identity(a):
    """Keep original-source provenance separate from replacement Omega bytes."""
    fields={k:a.get(k) for k in ('source_commit','source_sha256','replacement_path',
                                 'replacement_source_sha256','seed_sha256')}
    fields.update(artifact_id=a.get('id'),native_sha256=a.get('candidate',{}).get('sha256'))
    for key in ('source_sha256','replacement_source_sha256','native_sha256','seed_sha256'):
        if not isinstance(fields[key],str) or not re.fullmatch('[0-9a-f]{64}',fields[key]):
            raise ValueError('missing or malformed identity '+key)
    if not isinstance(fields['source_commit'],str) or not re.fullmatch('[0-9a-f]{40}',fields['source_commit']):
        raise ValueError('missing or malformed original source commit')
    path=fields['replacement_path']
    if not isinstance(path,str) or not path or Path(path).is_absolute() or '..' in Path(path).parts:
        raise ValueError('invalid replacement source path')
    source=ROOT/path
    if not source.resolve().is_relative_to(ROOT) or not source.is_file():
        raise ValueError('replacement source outside repository or missing')
    if hashlib.sha256(source.read_bytes()).hexdigest()!=fields['replacement_source_sha256']:
        raise ValueError('replacement Omega source changed')
    return fields


def production_receipt(receipt,a,stage,value):
    if receipt.get('schema')!=1 or type(receipt.get('schema')) is not int or receipt.get('receipt_type')!='omega.retirement.stage':
        raise ValueError('missing or unknown production receipt type/schema')
    if receipt.get('stage')!=stage:raise ValueError('production receipt stage mismatch')
    if receipt.get('scope')!='production' or receipt.get('production_cutover') is not True:
        raise ValueError('production receipt contradicts scope/cutover')
    if receipt.get('status')!=value:raise ValueError('production receipt status mismatch')
    recorded=receipt.get('identity')
    if not isinstance(recorded,dict) or recorded!=identity(a):
        raise ValueError('production receipt artifact/source/binary identity mismatch')
    if value=='NOT_APPLICABLE':
        if not isinstance(receipt.get('reason'),str) or not receipt['reason'].strip():
            raise ValueError('nonapplicable production stage needs a reason')
        return
    detail=receipt.get('details')
    if not isinstance(detail,dict):raise ValueError('production stage details missing')
    flags={
        'api_abi_data':('api_compatible','abi_compatible','data_compatible'),
        'isolated_candidate':('candidate_smoke',),
        'coordinator_model_train':('coordinator','model','train'),
        'transactional_swap_rollback':('swap','rollback'),
        'main_archive_smoke':('archived','main_smoke'),
    }
    if stage=='executable_equivalence':
        n=detail.get('cases_run')
        if type(n) is not int or n<=0 or type(detail.get('cases_passed')) is not int or detail['cases_passed']!=n:
            raise ValueError('production equivalence cases missing or failed')
    elif stage=='dependency_closure':
        if detail.get('status')!='PRODUCTION_VERIFIED' or detail.get('unresolved')!=[]:
            raise ValueError('production closure unresolved')
    elif any(detail.get(k) is not True for k in flags[stage]):
        raise ValueError('production stage detail missing or failed')


def isolated_receipt(receipt,a,stage,value):
    """Compatibility with the two existing, explicitly isolated receipt formats.

    Neither format can qualify a production gate. A deployment receipt covers
    only its three documented stages, never equivalence or caller migration.
    """
    expected=identity(a)
    if value!='PASS' or receipt.get('status')!='PASS':raise ValueError('isolated receipt status mismatch')
    if receipt.get('source_sha256')!=expected['replacement_source_sha256'] or receipt.get('native_sha256')!=expected['native_sha256']:
        raise ValueError('isolated receipt native source/binary mismatch')
    if 'receipt_type' in receipt:raise ValueError('unknown isolated receipt type')
    if stage=='executable_equivalence':
        n=receipt.get('cases_run')
        if type(n) is not int or n<=0 or receipt.get('cases_written')!=n or receipt.get('original_corpus',0)<=0:
            raise ValueError('unknown or incomplete isolated equivalence receipt')
        if 'scope' in receipt or 'production_cutover' in receipt:
            raise ValueError('unknown isolated equivalence receipt fields')
    elif stage in ('isolated_candidate','dependency_closure','transactional_swap_rollback'):
        if receipt.get('scope')!='isolated only; production untouched' or receipt.get('production_cutover') is not False:
            raise ValueError('isolated deployment scope/cutover mismatch')
        if receipt.get('seed_sha256')!=expected['seed_sha256'] or receipt.get('candidate',{}).get('sha256')!=expected['native_sha256']:
            raise ValueError('isolated deployment candidate/seed mismatch')
        if receipt.get('closure',{}).get('status')!='PASS' or not receipt.get('cases'):
            raise ValueError('isolated deployment incomplete')
    else:raise ValueError('isolated receipt reused for wrong stage')


def check_evidence(a,stage,value,retired):
    ev=a.get('gate_evidence',{}).get(stage)
    if not isinstance(ev,dict):raise ValueError('evidence missing')
    relative=ev.get('path')
    if not isinstance(relative,str) or not relative or Path(relative).is_absolute() or '..' in Path(relative).parts:
        raise ValueError('invalid evidence path')
    path=ROOT/relative
    if not path.resolve().is_relative_to(ROOT/'evidence/seed-retirement'):
        raise ValueError('evidence outside task directory')
    raw=path.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=ev.get('sha256'):raise ValueError('missing or changed evidence')
    receipt=json.loads(raw,object_pairs_hook=unique_object,parse_constant=invalid_constant)
    if not isinstance(receipt,dict):raise ValueError('receipt must be a JSON object')
    scope=ev.get('scope')
    if retired and scope!='production':raise ValueError('isolated evidence cannot retire production')
    if scope=='production':production_receipt(receipt,a,stage,value)
    elif scope=='isolated':isolated_receipt(receipt,a,stage,value)
    else:raise ValueError('unknown evidence scope')


def assess(m):
    errors=[];blockers=[]
    if m.get('schema')!=1:errors.append('unsupported schema')
    if m.get('status') not in ('PARTIAL','RETIRED'):errors.append('unsupported status')
    if m.get('required_cutover_stages')!=list(STAGES):errors.append('cutover stages missing or changed')
    if not m.get('coverage',{}).get('complete'):blockers.append('inventory coverage incomplete')
    artifacts=m.get('artifacts',[])
    if not artifacts:errors.append('artifact inventory empty')
    ids=[a.get('id') for a in artifacts]
    if None in ids or len(set(ids))!=len(ids):errors.append('artifact ids missing or duplicated')
    for a in artifacts:
        name=a.get('id','?')
        retired=a.get('production_retired',False)
        if a.get('live_legacy',True):blockers.append(name+': legacy remains')
        gates=a.get('gate',{})
        for stage in STAGES:
            value=gates.get(stage)
            if value not in ('PASS','NOT_RUN','BLOCKED','NOT_APPLICABLE'):errors.append(name+': invalid gate '+stage)
            if value in ('PASS','NOT_APPLICABLE'):
                try:check_evidence(a,stage,value,retired)
                except (OSError,ValueError,KeyError,TypeError,AttributeError) as e:
                    errors.append(name+': '+stage+': '+str(e))
        if retired:
            if retired is not True or a.get('live_legacy') is not False or a.get('production_cutover') is not True:
                errors.append(name+': production flags must be explicit booleans')
            if a.get('live_legacy',True):errors.append(name+': retired while legacy remains')
            if not a.get('production_cutover',False):errors.append(name+': production cutover unproved')
            if not a.get('source_commit') or not a.get('source_sha256'):errors.append(name+': source provenance unresolved')
            if not a.get('replacement_path') or a.get('replacement_language')!='Omega':errors.append(name+': native Omega replacement absent')
            if a.get('replacement_birth')!='verified seed chain':errors.append(name+': replacement birth unproved')
            if a.get('dependency_closure',{}).get('status')!='PRODUCTION_VERIFIED':errors.append(name+': production dependency closure unproved')
            if any(gates.get(s) not in ('PASS','NOT_APPLICABLE') for s in STAGES):errors.append(name+': cutover gates unfinished')
        elif a.get('retirement_target',True):
            blockers.append(name+': production retirement unproved')
    counts=m.get('counts',{})
    computed={
        'observed_installed_or_running_elf':sum('observation' in a for a in artifacts),
        'observed_running_elf':sum(bool(a.get('observation',{}).get('pids',[])) for a in artifacts),
        'legacy_artifact_rows':sum(a.get('live_legacy',True) for a in artifacts),
        'verified_production_retirements':sum(a.get('production_retired',False) for a in artifacts),
    }
    for name,value in computed.items():
        if counts.get(name)!=value:errors.append('count mismatch: '+name)
    if m.get('status')=='RETIRED' and blockers:errors.append('RETIRED claim with remaining blockers')
    return {'status':'INVALID' if errors else ('PARTIAL' if blockers else 'RETIRED'),'errors':errors,'blockers':blockers,'computed_counts':computed}


def main():
    p=argparse.ArgumentParser();p.add_argument('--manifest',type=Path,default=ROOT/'evidence/seed-retirement/retirement-manifest.json');p.add_argument('--require-retired',action='store_true');args=p.parse_args()
    try:
        manifest=json.loads(args.manifest.read_text(),object_pairs_hook=unique_object,parse_constant=invalid_constant)
        if not isinstance(manifest,dict):raise ValueError('manifest must be a JSON object')
        report=assess(manifest)
    except (OSError,ValueError,KeyError,TypeError) as e:
        print(json.dumps({'status':'INVALID','error':str(e)}));return 2
    print(json.dumps(report,indent=2))
    if report['errors']:return 2
    return int(args.require_retired and report['status']!='RETIRED')

if __name__=='__main__':sys.exit(main())
