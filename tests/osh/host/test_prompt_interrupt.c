/* Actual driver/core, actual signals. Link wrappers provide deterministic
 * scheduling only; no hook is compiled into the shipped shell. */
#include "osh_shell.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

static int input_fd=-1, notify_fd=-1, release_fd=-1, scenario, fired, reads, flushes;
static int fault_select, fault_mask;
ssize_t __real_read(int,void *,size_t);
int __real_fflush(FILE *);
int __real_pselect(int,fd_set *,fd_set *,fd_set *,const struct timespec *,const sigset_t *);
int __real_sigprocmask(int,const sigset_t *,sigset_t *);
static void barrier(void)
{
    char c='I';
    fired=1;
    if(raise(SIGINT)||write(notify_fd,&c,1)!=1||__real_read(release_fd,&c,1)!=1) _exit(98);
}
static int before_read(int fd)
{
    if(fd==input_fd && scenario==13 && !fired) { fired=1; errno=EAGAIN; return 1; }
    return 0;
}
int __wrap_fflush(FILE *f)
{
    int rc=__real_fflush(f);
    if(f==stderr && scenario>0 && scenario<4 && !fired && ++flushes==(scenario==2?2:1) && scenario!=3) barrier();
    return rc;
}
static ssize_t after_read(int fd,ssize_t rc)
{
    if(fd==input_fd && (scenario==3||scenario==8) && !fired && rc==1 && ++reads==(scenario==3?14:15)) barrier();
    return rc;
}
ssize_t __wrap_read(int fd,void *p,size_t n)
{
    if(before_read(fd)) return -1;
    return after_read(fd,__real_read(fd,p,n));
}
ssize_t __real___read_chk(int,void *,size_t,size_t);
ssize_t __wrap___read_chk(int fd,void *p,size_t n,size_t cap)
{
    if(before_read(fd)) return -1;
    return after_read(fd,__real___read_chk(fd,p,n,cap));
}
int __wrap_pselect(int n,fd_set *r,fd_set *w,fd_set *e,const struct timespec *t,const sigset_t *m)
{
    if(scenario==7 && !fired) barrier();
    if(fault_select) { int x=fault_select; fault_select=0; errno=x; return -1; }
    int rc=__real_pselect(n,r,w,e,t,m);
    if(scenario==14 && !fired && rc>0) {
        char c='I'; fired=1;
        if(write(notify_fd,&c,1)!=1) _exit(98);
        ssize_t got;
        do { got=__real_read(release_fd,&c,1); } while(got<0 && errno==EINTR);
        if(got!=1) _exit(98);
    }
    return rc;
}
int __wrap_sigprocmask(int how,const sigset_t *set,sigset_t *old)
{
    if(fault_mask && set) { fault_mask=0; errno=EIO; return -1; }
    return __real_sigprocmask(how,set,old);
}
static void die(const char *s) { perror(s); exit(99); }
static void put(int fd,const char *s) { size_t n=strlen(s); if(write(fd,s,n)!=(ssize_t)n) die("write"); }
static void drain(int fd,char *s,size_t cap)
{
    size_t n=0; ssize_t r;
    while(n+1<cap && (r=read(fd,s+n,cap-1-n))>0) n+=(size_t)r;
    s[n]=0;
}
static int equal_mask(const sigset_t *a,const sigset_t *b)
{
    for(int i=1;i<NSIG;i++) if(sigismember(a,i)!=sigismember(b,i)) return 0;
    return 1;
}
static void noop(int s) { (void)s; }
static int run_case(int native,int which,int blocked)
{
    int in[2],out[2],err[2],note[2],ack[2];
    if(which==14) {
        if(openpty(&in[1],&in[0],NULL,NULL,NULL)) die("openpty");
    } else if(pipe(in)) die("pipe");
    if(pipe(out)||pipe(err)||pipe(note)||pipe(ack)) die("pipe");
    fflush(NULL);
    pid_t pid=fork(); if(pid<0) die("fork");
    if(!pid) {
        close(in[1]); close(out[0]); close(err[0]); close(note[0]); close(ack[1]);
        if(which==14 && (setsid()<0 || ioctl(in[0],TIOCSCTTY,0)<0)) _exit(98);
        if(dup2(out[1],1)<0||dup2(err[1],2)<0) _exit(98);
        close(out[1]); close(err[1]);
        struct sigaction sa={0},before,after; sa.sa_handler=noop; sigemptyset(&sa.sa_mask);
        sigaction(SIGINT,&sa,NULL); sigaction(SIGINT,NULL,&before);
        sigset_t mask,after_mask; sigemptyset(&mask); sigaddset(&mask,SIGUSR1);
        if(blocked) sigaddset(&mask,SIGINT);
        __real_sigprocmask(SIG_SETMASK,&mask,NULL);
        input_fd=in[0]; notify_fd=note[1]; release_fd=ack[0]; scenario=which;
        if(which==4) { scenario=0; fault_select=EIO; }
        if(which==5) { scenario=0; fault_mask=1; }
        if(which>=6 && which!=7 && which!=8 && which!=13 && which!=14) scenario=0;
        if(which==6) fault_select=EINTR;
        if(which==9 && raise(SIGINT)) _exit(98);
        if(which==10) close(input_fd);
        if(which==11) {
            int high=fcntl(input_fd,F_DUPFD_CLOEXEC,FD_SETSIZE);
            if(high<0) _exit(98);
            close(input_fd); input_fd=high;
        }
        if(which==15 && fcntl(input_fd,F_SETFL,O_NONBLOCK)) _exit(98);
        int before_flags=fcntl(input_fd,F_GETFL);
        char *env[]={"PATH=/usr/bin:/bin","LC_ALL=C",NULL};
        OshShell sh;
        if(osh_shell_init(&sh,env,native,1,"embed",NULL,0)) _exit(98);
        int rc=osh_shell_run_fd(&sh,input_fd);
        __real_sigprocmask(SIG_SETMASK,NULL,&after_mask);
        sigaction(SIGINT,NULL,&after);
        int expected_rc=(which==4||which==5||which==10||which==11)?2:which==12?0:7;
        int ok=rc==expected_rc && equal_mask(&mask,&after_mask) && before.sa_handler==after.sa_handler;
        if(which!=10) ok=ok&&fcntl(input_fd,F_GETFL)==before_flags;
        if(which==9) { sigset_t pending; sigpending(&pending); ok=ok&&sigismember(&pending,SIGINT)==1; }
        if(expected_rc==7) ok=ok && (native?sh.core.calls_native>0:sh.core.calls_interp>0);
        osh_shell_free(&sh);
        /* Second actual interactive invocation in the same process must not
         * inherit an interrupt flag or modified caller signal mask. */
        scenario=0; if(which!=10) close(input_fd); int again[2]; if(pipe(again)) _exit(98);
        put(again[1],"printf 'AGAIN\\n'\nexit 9\n"); close(again[1]);
        if(osh_shell_init(&sh,env,native,1,"embed",NULL,0)) _exit(98);
        rc=osh_shell_run_fd(&sh,again[0]); close(again[0]);
        ok=ok&&rc==9; osh_shell_free(&sh);
        fflush(stdout); _exit(ok?0:1);
    }
    if(which!=14) close(in[0]);
    close(out[1]); close(err[1]); close(note[1]); close(ack[0]);
    if(which==2) put(in[1],"printf DISCARD |\n");
    if(which==3) put(in[1],"printf DISCARD");
    if(which==8) put(in[1],"printf DISCARD\n");
    if(which==14) put(in[1],"X\n");
    int progress=1; char prefix[4096]="", wait_state[256]="";
    if(which<=3 || which==7 || which==8 || which==14) {
        struct pollfd p={note[0],POLLIN,0}; char c;
        if(poll(&p,1,10000)!=1||read(note[0],&c,1)!=1||c!='I') {
            close(in[1]); close(ack[1]); char diagnostic[8192];
            drain(err[0],diagnostic,sizeof diagnostic);
            fprintf(stderr,"barrier case=%d backend=%d (not a behavioral verdict): %s",which,native,diagnostic);
            exit(99);
        }
        /* Reproduce terminal flush plus SIGINT synchronously, while the child
         * is held after readiness and still masks SIGINT. Actual terminal ^C
         * is checked independently by the hook-free PTY oracle. */
        if(which==14 && (tcflush(in[0],TCIFLUSH)||kill(pid,SIGINT))) die("terminal flush/signal");
        put(ack[1],"G");
        /* Require a fresh prompt before supplying any next input. Old code
         * gets rescue input only after recording the wait and wrong state. */
        size_t used=0; progress=0;
        for(;;) {
            struct pollfd q={err[0],POLLIN,0};
            if(poll(&q,1,500)!=1) break;
            ssize_t n=read(err[0],prefix+used,sizeof prefix-1-used);
            if(n<=0) break;
            used+=(size_t)n; prefix[used]=0;
            if(strstr(prefix,"\nosh$ ")) { progress=1; break; }
            if(used+1==sizeof prefix) break;
        }
        if(!progress) {
            char path[80]; snprintf(path,sizeof path,"/proc/%ld/syscall",(long)pid);
            FILE *f=fopen(path,"r");
            if(f) { if(!fgets(wait_state,sizeof wait_state,f)) wait_state[0]=0; fclose(f); }
        }
        put(in[1],"printf '%s\\n' \"$?\"\nexit 7\n");
    } else if(which!=10&&which!=11&&which!=12) put(in[1],"exit 7\n");
    if(which!=14) close(in[1]);
    close(ack[1]); close(note[0]);
    char output[4096],errors[4096]; drain(out[0],output,sizeof output); drain(err[0],errors,sizeof errors);
    close(out[0]); close(err[0]); int st; if(waitpid(pid,&st,0)!=pid) die("waitpid");
    if(which==14) { close(in[1]); close(in[0]); }
    const char *expected=(which<=3||which==7||which==8||which==14)?"130\nAGAIN\n":"AGAIN\n";
    int ok=progress&&WIFEXITED(st)&&WEXITSTATUS(st)==0&&!strcmp(output,expected);
    printf("%s window=%d native=%d blocked=%d progress=%d status=%d stdout=[%s] before=[%s] prompts=[%s] wait=[%s]\n",ok?"PASS":"FAIL",which,native,blocked,progress,st,output,prefix,errors,wait_state);
    return !ok;
}
int main(void)
{
    signal(SIGPIPE,SIG_IGN);
    unsigned failed=0,total=0;
    for(int native=0;native<2;native++) for(int which=1;which<=8;which++) {
        /* Old source has no pselect seam; other windows use its real reads. */
        if(which==7 && getenv("PROMPT_LEGACY")) continue;
        failed+=run_case(native,which,0); total++;
    }
    /* A caller-blocked SIGINT stays blocked; ready input still makes progress. */
    for(int native=0;native<2;native++) { failed+=run_case(native,6,1); total++; }
    for(int native=0;native<2;native++) for(int which=9;which<=12;which++) {
        failed+=run_case(native,which,which==9); total++;
    }
    if(!getenv("PROMPT_LEGACY")) for(int native=0;native<2;native++) for(int which=13;which<=15;which++) {
        failed+=run_case(native,which,0); total++;
    }
    printf("PROMPT_INTERRUPT: %u cases, %u failed\n",total,failed);
    return failed?1:0;
}
