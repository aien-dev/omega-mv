#!/usr/bin/env python3
"""Real controlling-PTY continuation status; all children are owned fixtures."""
import os,pty,pathlib,select,time,signal,re,json,sys
binary=pathlib.Path(sys.argv[1]).resolve();out=pathlib.Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=False)
rows=[]
for prior in (0,37):
 for label,exe,interp in [('bash','/bin/bash','0'),('native',str(binary),'0'),('interp',str(binary),'1')]:
  home=out/(label+'-'+str(prior));home.mkdir(mode=0o700);pid,fd=pty.fork();trace=[]
  if pid==0:
   env={'PATH':'/usr/bin:/bin','HOME':str(home),'XDG_STATE_HOME':str(home),'LC_ALL':'C','TZ':'UTC','TERM':'dumb','PS1':'osh$ ','PS2':'> ','OSH_INTERP':interp,'HISTFILE':str(home/'history')}
   args=[exe,'--noprofile','--norc','-i'] if label=='bash' else [exe]
   os.execve(exe,args,env)
  alive=True
  try:
   def prompt():
    data=b'';end=time.monotonic()+10
    while not data.endswith(b'osh$ '):
     remaining=end-time.monotonic()
     if remaining<=0:raise RuntimeError('PTY prompt deadline: never evidence of correct behavior')
     if not select.select([fd],[],[],remaining)[0]:continue
     block=os.read(fd,65536)
     if not block:raise RuntimeError('unexpected PTY EOF')
     data+=block
    trace.append(data.decode(errors='replace'));return data
   prompt();os.write(fd,('printf "" | exit '+str(prior)+'\n').encode());prompt();os.write(fd,b'exit 7 9\n');error=prompt();assert b'too many arguments' in error
   os.write(fd,b'printf "L611=%s\\n" "$?"\n');answer=prompt();values=re.findall(rb'(?:^|[\r\n])L611=([0-9]+)(?:\r?\n)',answer);assert len(values)==1,answer
   rows.append({'prior':prior,'version':label,'observed':int(values[0]),'trace':trace})
   os.write(fd,b'exit 0\n');end=time.monotonic()+10
   while time.monotonic()<end:
    got,status=os.waitpid(pid,os.WNOHANG)
    if got:alive=False;assert os.waitstatus_to_exitcode(status)==0;break
    time.sleep(0.01)
   if alive:raise RuntimeError('PTY exit deadline: not a behavioral pass')
  finally:
   os.close(fd)
   if alive:os.kill(pid,signal.SIGKILL);os.waitpid(pid,0)
for row in rows:
 expected=next(x['observed'] for x in rows if x['prior']==row['prior'] and x['version']=='bash');row['expected_bash']=expected;row['pass']=row['observed']==expected
(out/'results.json').write_text(json.dumps(rows,indent=2)+'\n');print(json.dumps({'sessions':len(rows),'passed':sum(r['pass'] for r in rows),'rows':[{k:v for k,v in r.items() if k!='trace'} for r in rows]}));sys.exit(not all(r['pass'] for r in rows))
