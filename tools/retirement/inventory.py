#!/usr/bin/env python3
"""Read-only retirement audit. Python is audit/test infrastructure, not the native path.
Counts git blobs rather than working-copy guesses. No service calls, environment reads,
GPU access, process signalling, or changes outside the requested report directory.
"""
import collections
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
HOME=Path.home()
WS=HOME/'workspace'
OUT=ROOT/'evidence/seed-retirement'
B=ROOT/'build/retirement'

def cmd(*args,cwd=ROOT):
    return subprocess.check_output(list(map(str,args)),cwd=cwd,stderr=subprocess.DEVNULL)

def git(repo,*args):return cmd('git','-C',repo,*args)

def text(repo,*args):return git(repo,*args).decode().strip()

def digest(path):
    h=hashlib.sha256()
    with open(path,'rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()

def classify(path):
    parts=Path(path).parts
    if any(p in ('third_party','vendor','external','node_modules') for p in parts):return 'third_party'
    if any(p in ('evidence','evidence-archive','snapshots','baseline') for p in parts):return 'historical_or_oracle'
    if any(p in ('tests','test','fixtures') for p in parts):return 'first_party_test'
    return 'first_party_source'

def source_inventory(repo,ref):
    commit=text(repo,'rev-parse',ref)
    tree=git(repo,'ls-tree','-rz',commit).split(b'\0')
    files=[]
    for row in tree:
        if not row:continue
        meta,path=row.split(b'\t',1);mode,kind,oid=meta.split()
        path=os.fsdecode(path)
        if kind!=b'blob' or Path(path).suffix not in ('.c','.h','.rs'):continue
        files.append((path,oid.decode()))
    # One cat-file process, no per-line count estimates or dirty-working-copy reads.
    cat=subprocess.Popen(['git','-C',str(repo),'cat-file','--batch'],stdin=subprocess.PIPE,stdout=subprocess.PIPE)
    rows=[];counts=collections.defaultdict(lambda:{'files':0,'lines':0,'bytes':0})
    for path,oid in files:
        cat.stdin.write((oid+'\n').encode());cat.stdin.flush()
        header=cat.stdout.readline().split();size=int(header[2]);blob=cat.stdout.read(size);assert cat.stdout.read(1)==b'\n'
        language='Rust' if path.endswith('.rs') else 'C'
        category=classify(path);lines=blob.count(b'\n')+int(bool(blob) and not blob.endswith(b'\n'))
        counts[category+'/'+language]['files']+=1;counts[category+'/'+language]['lines']+=lines;counts[category+'/'+language]['bytes']+=size
        rows.append({'path':path,'git_blob':oid,'sha256':hashlib.sha256(blob).hexdigest(),'language':language,'category':category,'lines':lines,'bytes':size})
    cat.stdin.close();assert cat.wait()==0
    return {'commit':commit,'ref':ref,'counts':dict(counts),'files':rows}


def elf(path):
    raw=cmd('readelf','-h','-l','-d',path).decode()
    return {'needed':re.findall(r'Shared library: \[([^]]+)\]',raw),
            'interpreter':re.findall(r'Requesting program interpreter: ([^]]+)',raw),
            'aarch64':'AArch64' in raw,'sha256':digest(path),'bytes':path.stat().st_size}


def main():
    OUT.mkdir(exist_ok=True,parents=True)
    cpin=text(ROOT,'rev-parse','origin/seed/c2om')
    catalog=text(ROOT,'show',cpin+':tools/c2om/repos.tsv')
    (OUT/'c-migration-repos.tsv').write_text(catalog+'\n')
    repos={}
    for row in catalog.splitlines():
        if not row or row.startswith('#'):continue
        name,path,wave=row.split('\t');repos[name]=(Path(path).expanduser(),'L418' if wave=='0' else ('L419' if wave=='3' else 'L420'))
    repos.update({
        'sovereign-core':(WS/'aien-sovereign-core','L495'),
        'protocols':(WS/'aien-protocols','L495'),
        'aegis-runtime':(WS/'aegis-runtime','L495'),
        'spark-rsi':(WS/'spark-rsi','L495'),
        'omega-bridge':(WS/'omega-bridge','L483'),
    })
    sources={};snapshots=[]
    for name,(repo,owner) in repos.items():
        try:
            default=text(repo,'symbolic-ref','refs/remotes/origin/HEAD')
        except subprocess.CalledProcessError:
            default='origin/main' if name!='atlas' else 'origin/master'
        try:
            inv=source_inventory(repo,default)
            sources[name]={'repo_path':str(repo),'remote':text(repo,'remote','get-url','origin'),'owner':owner,'default':inv['commit'],'head':text(repo,'rev-parse','HEAD'),'branch':text(repo,'branch','--show-current'),'working_status_sha256':hashlib.sha256(git(repo,'status','--porcelain')).hexdigest(),'counts':inv['counts']}
            (OUT/f'sources-{name}.json').write_text(json.dumps(inv,indent=2)+'\n')
            snapshots.append({'repo':name,'ref':default,'commit':inv['commit']})
        except (subprocess.CalledProcessError,FileNotFoundError) as e:
            sources[name]={'repo_path':str(repo),'owner':owner,'status':'UNRESOLVED','reason':type(e).__name__}
    # Relevant proposed source snapshots remain separate from main and from installed artifacts.
    for name,repo,ref,owner in [
        ('seed',ROOT,'29b12760','L408'),('c-migration',ROOT,cpin,'L418'),
        ('motor-rust',WS/'sc-motor-ship','f0230a59','L387'),
        ('motor-omega',WS/'omega-motor-ship','6de999da','L387'),
        ('bridge',WS/'omega-bridge','77376b1','L483'),
        ('attachments',WS/'omega-bridge-attach','7916bcc','L484')]:
        try:
            snapshots.append({'component':name,'owner':owner,'repo_path':str(repo),'source_commit':text(repo,'rev-parse',ref),'main_contains':subprocess.run(['git','-C',str(repo),'merge-base','--is-ancestor',ref,'origin/main'],capture_output=True).returncode==0})
        except subprocess.CalledProcessError:
            snapshots.append({'component':name,'owner':owner,'status':'UNRESOLVED'})
    known=('aien','atlas','cortex','crumb','spark','rad-id-sync','openclaw','mobhub')
    observed={}
    # Installed first-party-named ELF programs, including dormant commands. Shell launchers
    # have separate entries, since a shell file alone says nothing about the executable it starts.
    for p in sorted((HOME/'.local/bin').iterdir()):
        if not p.name.startswith(known) or any(s in p.name for s in ('.bak','before','pre-')):continue
        try:
            with p.open('rb') as f:magic=f.read(4)
            if magic==b'\x7fELF':
                observed[str(p.resolve())]={'paths':[str(p)],'pids':[],'observed':'installed','elf':elf(p.resolve())}
        except (OSError,subprocess.CalledProcessError):pass
    for proc in Path('/proc').iterdir():
        if not proc.name.isdigit():continue
        try:
            p=(proc/'exe').resolve(strict=True)
            if not str(p).startswith(str(HOME)):continue
            if not p.name.startswith(known):continue
            if any(x in p.parts for x in ('external','node_modules','registry','third_party')):continue
            if str(ROOT/'build') in str(p):continue
            entry=observed.setdefault(str(p),{'paths':[str(p)],'pids':[],'observed':'running','elf':elf(p)})
            entry['pids'].append(int(proc.name));entry['observed']='running'
            maps=(proc/'maps').read_text()
            libs=sorted(set(line.split()[-1] for line in maps.splitlines() if len(line.split())>=6 and '.so' in line.split()[-1] and line.split()[-1].startswith('/')))
            entry['mapped_shared_libraries']=libs
        except (OSError,subprocess.CalledProcessError):continue
    stages=['executable_equivalence','api_abi_data','isolated_candidate','coordinator_model_train','dependency_closure','transactional_swap_rollback','main_archive_smoke']
    artifacts=[]
    for path,obs in sorted(observed.items()):
        name=Path(path).name
        a={'id':name+'@'+obs['elf']['sha256'][:16],'name':name,'live_legacy':True,'scope':'first_party_named_observed_artifact','source_language':'UNRESOLVED','source_commit':None,'birth_chain':'UNRESOLVED; installed ELF does not prove its build compiler','dependency_closure':{'static':'UNRESOLVED','dynamic':obs['elf']['needed'],'os_floor':obs.get('mapped_shared_libraries',[])},'replacement_path':None,'replacement_owner':'L495','gate':{s:'NOT_RUN' for s in stages},'observation':obs,'cutover':'BLOCKED; preserve current executable and require exact build/source provenance'}
        if name=='aien-serve-http' and 'bin-19173f9e' in path:
            a.update(source_language=['Rust','C'],source_commit=text(WS/'sc-motor-ship','rev-parse','f0230a59'),birth_chain='Cargo/rustc links the C-built Omega archive; BUILD.txt records both source branches',replacement_owner='L495; compiler L408; C runtime L419; model train L384; deployment L387')
            a['omega_source_commit']=text(WS/'omega-motor-ship','rev-parse','6de999da')
            a['dependency_closure']['static']='First-party Rust workspace + libomega_gpu.a (C); Cargo.lock and build.rs pin runtime and native-library edges; exact complete static closure NOT_PROVEN'
            a['blockers']=['FR-1','FR-4','FR-5','FR-6','FR-7','FR-13','FR-14','FR-15']
            build=Path(path).parent/'BUILD.txt'
            a['build_record_sha256']=digest(build)
            (OUT/'motor-BUILD.txt').write_bytes(build.read_bytes())
            for file in ['Cargo.lock','crates/aien-serve-http/Cargo.toml','crates/aien-omega-gpu/build.rs','omega.lock','native-artifacts.lock']:
                try:
                    blob=git(WS/'sc-motor-ship','show',a['source_commit']+':'+file)
                    a.setdefault('source_build_files',[]).append({'path':file,'sha256':hashlib.sha256(blob).hexdigest()})
                except subprocess.CalledProcessError:pass
        artifacts.append(a)
    trn1={'id':'aien-replay-trn1-schema1','name':'TRN1 verifier utility','live_legacy':True,'scope':'library-source-on-main; not observed as installed standalone CLI','source_language':'Rust','source_commit':'754c07b118f41ad899607b60329bd851b1ccb13e','source_files':['crates/aien-replay/src/lib.rs'],'source_sha256':'8824370f30573dea68a011ec457e55470eb4d688483a1de0fec96ffcb5757aeb','birth_chain':'Cargo -> rustc -> sha2 Rust dependency -> host linker','dependency_closure':{'current':['Rust std','sha2','Cargo.lock transitive dependencies'],'replacement':['Linux read/write/exit syscall ABI'],'status':'native executable closure checked; original library callers not migrated'},'replacement_path':'seed/retirement/trn1/trn1.om','replacement_owner':'L496','gate':{s:'NOT_RUN' for s in stages},'cutover':'isolated executable only; ABI/consumer migration and main archiving remain pending','blockers':['FR-7 for ABI replacement','FR-1/FR-13 for file/argv callers','review of library utility selection versus requested existing CLI']}
    summary_path=OUT/'trn1-summary.json'
    if summary_path.exists():
        summary=json.loads(summary_path.read_text());trn1['gate']['executable_equivalence']=summary['status'];trn1['equivalence_evidence']='evidence/seed-retirement/trn1-summary.json'
    if (B/'trn1').exists():trn1['candidate']=elf(B/'trn1')
    artifacts.append(trn1)
    artifacts += [
        {'id':'omega-bridge-target-compiler','live_legacy':False,'scope':'feature artifact; not a native replacement for emitted C/Python programs','source_language':['Omega','shell'],'source_commit':text(WS/'omega-bridge','rev-parse','77376b1'),'birth_chain':'seed-born Omega target translator; emitted C/Python still requires external compiler/runtime','dependency_closure':{'status':'NOT_A_NATIVE_MIGRATION'},'replacement_path':None,'replacement_owner':'L483','gate':{s:'NOT_RUN' for s in stages}},
        {'id':'omega-attachments','live_legacy':True,'scope':'feature artifact; installation not inferred','source_language':['Omega','C'],'source_commit':text(WS/'omega-bridge-attach','rev-parse','7916bcc'),'birth_chain':'seed-born attachment policy plus cc-built C host boundary','dependency_closure':{'current':['cli/omega.c','cli/host.c','cli/discover.c','cli/sandbox.c','libc','native Omega policy'],'status':'C_HOST_REMAINS'},'replacement_path':None,'replacement_owner':'L484','gate':{s:'NOT_RUN' for s in stages},'blockers':['FR-1','FR-13','FR-14']}]
    org=json.loads((B/'org-repos.json').read_text()) if (B/'org-repos.json').exists() else []
    manifest={'schema':1,'observed_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'status':'PARTIAL','parent':'L495','task':'L496','c_migration_catalog_commit':cpin,'sources':sources,'source_snapshots':snapshots,'artifacts':artifacts,'counts':{'observed_installed_or_running_elf':len(observed),'observed_running_elf':sum(bool(x['pids']) for x in observed.values()),'legacy_artifact_rows':sum(x['live_legacy'] for x in artifacts),'verified_production_retirements':0},'coverage':{'catalog_repos':len(repos),'github_org_nonfork_repos':sum(not r['isFork'] for r in org),'complete':False,'limitations':['named first-party executable scan, not an exhaustive host software inventory','repo catalog reused from L418 plus relevant Rust repositories; other org repositories remain unqualified','unknown installed build provenance is a blocker, never assumed seeded','path-based source classification does not prove production linkage','source inventory is not a live-artifact count']},'external_floor':['Linux kernel and system shared libraries','system compiler/build tools used only for audit and oracles','third-party programs, drivers and firmware outside first-party migration scope'],'required_cutover_stages':stages}
    (OUT/'retirement-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps(manifest['counts'],sort_keys=True))

if __name__=='__main__':main()
