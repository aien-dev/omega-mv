/* Independent actual bash/osh terminal behavior, with no driver hooks. */
#include <errno.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
static char transcript[32768]; static size_t used;
static void fail(const char *s) { fprintf(stderr,"PTY infrastructure failure: %s errno=%d transcript=[%s]\n",s,errno,transcript); exit(99); }
static void send_text(int fd,const char *s) { size_t n=strlen(s); if(write(fd,s,n)!=(ssize_t)n) fail("write"); }
static void until(int fd,const char *needle)
{
    size_t start=used;
    for(;;) {
        struct pollfd p={fd,POLLIN,0};
        if(poll(&p,1,10000)!=1) fail("prompt wait (not mutant kill)");
        ssize_t n=read(fd,transcript+used,sizeof transcript-1-used);
        if(n<=0) fail("read");
        used+=(size_t)n; transcript[used]=0;
        if(strstr(transcript+start,needle)) return;
        if(used+1==sizeof transcript) fail("transcript capacity");
    }
}
static int run(const char *binary,int interp)
{
    int master,slave; struct termios term;
    if(openpty(&master,&slave,NULL,NULL,NULL)||tcgetattr(slave,&term)) fail("openpty");
    term.c_lflag &= ~(ECHO|ECHONL);
    if(tcsetattr(slave,TCSANOW,&term)) fail("termios");
    used=0; transcript[0]=0; fflush(NULL);
    pid_t pid=fork(); if(pid<0) fail("fork");
    if(!pid) {
        close(master); if(setsid()<0||ioctl(slave,TIOCSCTTY,0)<0) _exit(98);
        for(int i=0;i<3;i++) if(dup2(slave,i)<0) _exit(98);
        if(slave>2) close(slave);
        signal(SIGINT,SIG_DFL); signal(SIGQUIT,SIG_DFL);
        sigset_t empty; sigemptyset(&empty); sigprocmask(SIG_SETMASK,&empty,NULL);
        char *env[]={"PATH=/usr/bin:/bin","LC_ALL=C","TZ=UTC","HOME=/tmp","TERM=dumb","PS1=osh$ ","PS2=> ",interp?"OSH_INTERP=1":"OSH_INTERP=0",NULL};
        if(!strcmp(binary,"/bin/bash")) execle(binary,binary,"--noprofile","--norc","-i",NULL,env);
        else execle(binary,binary,NULL,env);
        _exit(98);
    }
    close(slave); until(master,"osh$ ");
    const char *partial[]={"printf DISCARD","printf DISCARD |\n",""};
    for(int i=0;i<3;i++) {
        send_text(master,partial[i]);
        if(i==1) until(master,"> ");
        send_text(master,"\003"); until(master,"osh$ ");
        send_text(master,"printf 'STATUS:%s\\n' \"$?\"\n"); until(master,"osh$ ");
    }
    send_text(master,"printf 'DONE\\n'\n"); until(master,"osh$ ");
    send_text(master,"\004");
    int st; if(waitpid(pid,&st,0)!=pid) fail("waitpid"); close(master);
    int count=0; for(char *p=transcript;(p=strstr(p,"STATUS:130\r\n"));p++) count++;
    int ok=WIFEXITED(st)&&WEXITSTATUS(st)==0&&count==3&&!strstr(transcript,"DISCARD")&&strstr(transcript,"DONE\r\n");
    printf("%s PTY binary=%s interp=%d status=%d interrupts=%d transcript=[%s]\n",ok?"PASS":"FAIL",binary,interp,st,count,transcript);
    return !ok;
}
int main(int argc,char **argv)
{
    if(argc!=2) return 99;
    int failures=run("/bin/bash",0)+run(argv[1],0)+run(argv[1],1);
    printf("PROMPT_PTY: 3 cases, %d failed\n",failures); return failures?1:0;
}
