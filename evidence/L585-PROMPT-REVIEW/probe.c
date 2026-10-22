/* Independent direct tests of exact static helper; linker discards unrelated
 * driver functions, so this does not substitute a fake parser/core. */
#ifndef REVIEW_SHELL
#define REVIEW_SHELL "src/osh/host/osh_shell.c"
#endif
#include REVIEW_SHELL
#include <fcntl.h>
#include <stdarg.h>
static int mode, once, seen_nonblock, peer;
ssize_t __real_read(int,void *,size_t);
int __real_pselect(int,fd_set *,fd_set *,fd_set *,const struct timespec *,const sigset_t *);
ssize_t __wrap_read(int fd,void *p,size_t n) {
    int flags=fcntl(peer,F_GETFL); if(flags&O_NONBLOCK) seen_nonblock++;
    if(!once++) {
        if(mode==1) { errno=EAGAIN; return -1; }
        if(mode==2) { errno=EINTR; return -1; }
        if(mode==3) { errno=EIO; return -1; }
        ssize_t r=__real_read(fd,p,n);
        if(mode==4||mode==5) raise(SIGINT);
        return r;
    }
    return __real_read(fd,p,n);
}
int __wrap_pselect(int n,fd_set *r,fd_set *w,fd_set *e,const struct timespec *t,const sigset_t *m) {
    if(mode==6&&!once++) raise(SIGINT); /* pending while blocked, atomic wait must deliver */
    return __real_pselect(n,r,w,e,t,m);
}
int main(void) {
    struct sigaction sa={0};sa.sa_handler=on_prompt_int;sigemptyset(&sa.sa_mask);sigaction(SIGINT,&sa,NULL);
    int failed=0,total=0;
    for(int nb=0;nb<2;nb++)for(mode=0;mode<=7;mode++) {
        int p[2]; if(pipe(p))return 99;peer=dup(p[0]);if(peer<0)return 99;
        /* An unrelated open-file flag must survive the temporary update too. */
        if(fcntl(p[0],F_SETFL,O_APPEND|(nb?O_NONBLOCK:0)))return 99;
        int flags=fcntl(peer,F_GETFL); if(mode!=5) {if(write(p[1],"Z",1)!=1)return 99;} close(p[1]);
        sigset_t mask,after;sigemptyset(&mask);sigaddset(&mask,SIGUSR1);if(mode==7)sigaddset(&mask,SIGINT);
        if(sigprocmask(SIG_SETMASK,&mask,NULL))return 99;
        if(mode==7)raise(SIGINT);
        once=seen_nonblock=0;g_prompt_int=0;unsigned char c=0;int rc=prompt_byte(p[0],&c);int saved=errno;
        sigprocmask(SIG_SETMASK,NULL,&after);
        int expected=mode==3?-1:(mode==4||mode==5||mode==6)?-2:1;
        int ok=rc==expected&&fcntl(peer,F_GETFL)==flags&&sigismember(&after,SIGUSR1)==1&&sigismember(&after,SIGINT)==(mode==7);
        if(mode==3)ok=ok&&saved==EIO;
        if(expected==1)ok=ok&&c=='Z';
        if(mode!=6)ok=ok&&seen_nonblock>0;
        if(mode==7){sigset_t pending;sigpending(&pending);ok=ok&&sigismember(&pending,SIGINT)==1;}
        printf("%s mode=%d nonblocking=%d result=%d expected=%d read_nonblock=%d flags_restored=%d\n",ok?"PASS":"FAIL",mode,nb,rc,expected,seen_nonblock,fcntl(peer,F_GETFL)==flags);
        failed+=!ok;total++;close(peer);close(p[0]);
        sigemptyset(&mask);sigprocmask(SIG_SETMASK,&mask,NULL);g_prompt_int=0;
    }
    printf("INDEPENDENT_HELPER: %d cases %d failed\n",total,failed);return failed?1:0;
}
