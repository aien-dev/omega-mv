/* Independent all-position setup/restore oracle, actual compiled shell core. */
#include "osh_shell.h"
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static int at,calls,bad,checks;
static const int sigs[]={SIGINT,SIGQUIT,SIGTSTP,SIGTTIN,SIGTTOU};
int __real_sigaction(int,const struct sigaction *,struct sigaction *);
int __wrap_sigaction(int s,const struct sigaction *a,struct sigaction *o) { if(at&&++calls==at){errno=EIO;return -1;} return __real_sigaction(s,a,o); }
static void handler(int s,siginfo_t *i,void *v){(void)s;(void)i;(void)v;}
static int same(const struct sigaction *a,const struct sigaction *b){if(a->sa_sigaction!=b->sa_sigaction||a->sa_flags!=b->sa_flags)return 0;for(int s=1;s<NSIG;s++)if(sigismember(&a->sa_mask,s)!=sigismember(&b->sa_mask,s))return 0;return 1;}
#define CHECK(x) do{checks++;if(!(x)){bad++;printf("FAIL fault=%d engine=%d api=%d line=%d\n",fault,engine,api,__LINE__);}}while(0)
int main(void){
 char *env[]={"PATH=/usr/bin:/bin",NULL};
 for(int engine=0;engine<2;engine++)for(int api=0;api<3;api++) {
 OshShell sh;if(osh_shell_init(&sh,env,engine,1,"review",NULL,0))return 99;
 for(int fault=0;fault<=10;fault++) {
 struct sigaction before[5],after[5];
 for(int i=0;i<5;i++){struct sigaction a={0};a.sa_sigaction=handler;a.sa_flags=SA_SIGINFO|SA_RESTART|SA_NODEFER;sigemptyset(&a.sa_mask);sigaddset(&a.sa_mask,SIGUSR1);sigaddset(&a.sa_mask,SIGUSR2);if(sigaction(sigs[i],&a,NULL)||sigaction(sigs[i],NULL,&before[i]))return 99;}
 sigset_t mask0,mask1;sigprocmask(SIG_BLOCK,NULL,&mask0);
 at=fault;calls=0;int rc;
 if(api==0)rc=osh_shell_run_string(&sh,"",0);else if(api==1)rc=osh_shell_run_script(&sh,"",0);else{int f[2];if(pipe(f))return 99;close(f[1]);rc=osh_shell_run_fd(&sh,f[0]);close(f[0]);}
 at=0; CHECK(rc==(fault?70:(sh.s.last_status))); if(fault)CHECK(sh.s.last_status==70);
 CHECK(sh.core.calls_native==0&&sh.core.calls_interp==0);
 for(int i=0;i<5;i++){if(sigaction(sigs[i],NULL,&after[i]))return 99;CHECK((fault>=6&&i==10-fault)?!same(&before[i],&after[i]):same(&before[i],&after[i]));if(sigaction(sigs[i],&before[i],NULL))return 99;}
 sigprocmask(SIG_BLOCK,NULL,&mask1);for(int s=1;s<NSIG;s++)CHECK(sigismember(&mask0,s)==sigismember(&mask1,s));
 }
 osh_shell_free(&sh);
 }
 printf("SIGNAL_PROBE %d checks %d failures\n",checks,bad);return bad?1:0;
}
