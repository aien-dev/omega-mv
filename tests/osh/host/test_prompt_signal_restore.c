/* #344: embed the actual shell core and check caller-owned signal actions.
 * No fake parser, tty, process signalling, external command or effect is used. */
#include "osh_shell.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const int signals[] = {SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU};
static unsigned passed, failed;
#ifdef PROMPT_SIGNAL_TEST_FAULTS
static int inject_at, inject_calls;
int __real_sigaction(int, const struct sigaction *, struct sigaction *);
int __wrap_sigaction(int sig, const struct sigaction *act, struct sigaction *old)
{
    if (inject_at && ++inject_calls == inject_at) { errno = EIO; return -1; }
    return __real_sigaction(sig, act, old);
}
#endif
static void caller_a(int sig) { (void)sig; }
static void caller_b(int sig) { (void)sig; }
static void check(int ok, const char *name, int native, int interactive, int kind, int payload, int sig)
{
    if (ok) passed++;
    else { failed++; printf("FAIL %s native=%d interactive=%d kind=%d payload=%d signal=%d\n",name,native,interactive,kind,payload,sig); }
}
static int same_action(const struct sigaction *a, const struct sigaction *b)
{
    if (a->sa_handler != b->sa_handler || a->sa_flags != b->sa_flags) return 0;
    for (int sig = 1; sig < NSIG; sig++)
        if (sigismember(&a->sa_mask, sig) != sigismember(&b->sa_mask, sig)) return 0;
    return 1;
}
int main(void)
{
    struct sigaction original[5];
    for (int i=0;i<5;i++) if(sigaction(signals[i],NULL,&original[i])) return 99;
    char *envp[] = {"PATH=/usr/bin:/bin", "HOME=/tmp", NULL};
    const char *texts[] = {"", "exit 7\n", ")\n"};
    const int expected[] = {0,7,2};
    for(int native=0;native<2;native++) for(int interactive=0;interactive<2;interactive++)
    for(int kind=0;kind<3;kind++) for(int payload=0;payload<3;payload++) {
        struct sigaction before[5], after[5];
        for(int i=0;i<5;i++) {
            struct sigaction sa={0};
            sa.sa_handler=i==1?SIG_IGN:(i%2?caller_a:caller_b);
            sa.sa_flags=i%2?SA_RESTART:0;
            sigemptyset(&sa.sa_mask);
            sigaddset(&sa.sa_mask,SIGUSR1);
            if(i%2) sigaddset(&sa.sa_mask,SIGUSR2);
            if(sigaction(signals[i],&sa,NULL)||sigaction(signals[i],NULL,&before[i])) return 99;
        }
        OshShell sh;
        if(osh_shell_init(&sh,envp,native,interactive,"embedder",NULL,0)) return 99;
        int rc;
        size_t n=strlen(texts[payload]);
        if(kind==0) rc=osh_shell_run_string(&sh,texts[payload],n);
        else if(kind==1) rc=osh_shell_run_script(&sh,texts[payload],n);
        else {
            int fds[2];
            if(pipe(fds)||write(fds[1],texts[payload],n)!=(ssize_t)n) return 99;
            close(fds[1]);
            rc=osh_shell_run_fd(&sh,fds[0]);
            close(fds[0]);
        }
        check(rc==expected[payload],"status",native,interactive,kind,payload,0);
        if(payload) check(native ? sh.core.calls_native>0&&sh.core.calls_interp==0 : sh.core.calls_interp>0&&sh.core.calls_native==0,
            "actual-core-backend",native,interactive,kind,payload,0);
        for(int i=0;i<5;i++) {
            if(sigaction(signals[i],NULL,&after[i])) return 99;
            check(same_action(&before[i],&after[i]),"caller-action-restored",native,interactive,kind,payload,signals[i]);
        }
        osh_shell_free(&sh);
    }
#ifdef PROMPT_SIGNAL_TEST_FAULTS
    for (int fault=3;fault<=6;fault+=3) {
        struct sigaction before[5], after[5];
        /* Reestablish caller actions independently of earlier tests, including
         * a broken restoration mutant's deliberately contaminated state. */
        for(int i=0;i<5;i++) {
            struct sigaction sa={0};
            sa.sa_handler=caller_a;
            sa.sa_flags=SA_RESTART;
            sigemptyset(&sa.sa_mask);
            sigaddset(&sa.sa_mask,SIGUSR1);
            if(sigaction(signals[i],&sa,NULL)||sigaction(signals[i],NULL,&before[i])) return 99;
        }
        OshShell sh;
        if(osh_shell_init(&sh,envp,1,1,"embedder",NULL,0)) return 99;
        inject_calls=0;
        inject_at=fault;
        /* Empty input in the restore-failure case avoids the execution
         * adapter's own nested sigaction calls: call 6 is exactly restore. */
        int rc=osh_shell_run_string(&sh,fault==3?"exit 7\n":"",fault==3?7:0);
        inject_at=0;
        check(rc==70&&sh.s.last_status==70,"signal-failure-status",1,1,0,fault,0);
        check(sh.core.calls_native==0,"no-core-execution-on-fault-control",1,1,0,fault,0);
        for(int i=0;i<5;i++) {
            if(sigaction(signals[i],NULL,&after[i])) return 99;
            int unchanged=same_action(&before[i],&after[i]);
            check(fault==6&&i==4 ? !unchanged : unchanged,"failure-action-boundary",1,1,0,fault,signals[i]);
            if(sigaction(signals[i],&before[i],NULL)) return 99;
        }
        osh_shell_free(&sh);
    }
#endif
    for(int i=0;i<5;i++) if(sigaction(signals[i],&original[i],NULL)) return 99;
    printf("PROMPT_SIGNAL_RESTORE: %u passed, %u failed\n",passed,failed);
    return failed?1:0;
}
