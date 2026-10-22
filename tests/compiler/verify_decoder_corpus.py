#!/usr/bin/env python3
"""Fail closed on missing/changed inputs in the exact PR499 decoder corpus.
Host evidence tooling only. Does not modify the export or validate decoder semantics.
"""
import hashlib,json,pathlib,re,subprocess,sys
PIN='1e4cdbe91b444efe51af4e745f366e07d54de030'
LOADER='tests/compiler/test_osc_ir_decode.c'
def git(repo,*args):
    return subprocess.check_output(['git','-C',str(repo),*args],stderr=subprocess.PIPE)
def blob(data):
    return hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
def unlinked_path(export,name):
    if export.is_symlink() or not export.is_dir(): raise ValueError('missing or linked export root')
    current=export
    for part in pathlib.PurePosixPath(name).parts:
        current=current/part
        if current.is_symlink(): raise ValueError('linked source component: '+str(current))
    return current
def verify(export,repo):
    source=git(repo,'show',PIN+':'+LOADER)
    names=re.findall(rb'\bload_dir\("([^"\n]+)"\);',source)
    if len(names)!=4 or len(set(names))!=4: raise ValueError('unexpected pinned loader declarations')
    dirs=[n.decode('ascii') for n in names]
    expected={}
    for d in dirs:
        # load_dir enumerates immediate .osc files only, not recursive descendants.
        tree=git(repo,'ls-tree','-z',PIN+':'+d)
        for record in tree.split(b'\0'):
            if not record: continue
            meta,name=record.split(b'\t',1); mode,kind,oid=meta.decode().split()
            name=name.decode('ascii')
            if name.endswith('.osc'):
                if mode not in ('100644','100755') or kind!='blob': raise ValueError('non-regular pinned corpus entry')
                expected[d+'/'+name]=oid
        directory=unlinked_path(export,d)
        if directory.is_symlink() or not directory.is_dir(): raise ValueError('missing or linked corpus directory: '+d)
        actual={d+'/'+p.name for p in directory.iterdir() if p.name.endswith('.osc')}
        wanted={p for p in expected if p.startswith(d+'/')}
        if actual!=wanted: raise ValueError('missing/extra declared corpus input in '+d)
    expected[LOADER]=git(repo,'rev-parse',PIN+':'+LOADER).decode().strip()
    manifest=[]
    for name,oid in sorted(expected.items()):
        f=unlinked_path(export,name)
        if f.is_symlink() or not f.is_file() or f.stat().st_nlink!=1: raise ValueError('missing or linked source: '+name)
        # The caller owns the immutable test export. No concurrent-writer protection claimed.
        data=f.read_bytes()
        if blob(data)!=oid: raise ValueError('changed pinned source: '+name)
        manifest.append({'path':name,'git_blob':oid,'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data)})
    return {'schema':'omega.decoder.corpus-closure.v1','pin':PIN,'corpus_programs':len(expected)-1,'loader_verified':True,'directories':dirs,'files':manifest,'scope':'source/corpus materialization, not decoder correctness'}
def main():
    if len(sys.argv)!=3: raise ValueError('usage: verify_decoder_corpus.py EXPORT_DIR SOURCE_REPO')
    export=pathlib.Path(sys.argv[1]).absolute();repo=pathlib.Path(sys.argv[2]).resolve()
    print(json.dumps(verify(export,repo),sort_keys=True))
if __name__=='__main__':
    try: main()
    except (ValueError,OSError,subprocess.CalledProcessError) as e:
        print(json.dumps({'verdict':'REFUSED','error':str(e)}),file=sys.stderr);sys.exit(1)
