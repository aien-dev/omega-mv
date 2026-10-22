/* Reviewer oracle: include exact candidate test translation unit without changing it. */
#define main original_driver_main
#include "../../tests/osh/host/test_osh_host.c"
#undef main
#include <sys/file.h>
static void must(int ok, const char *s) { if (!ok) { fprintf(stderr,"FAIL %s\n",s); exit(1); } puts(s); }
int main(int argc, char **argv) {
    if (argc > 1) return original_driver_main(argc, argv);
    char path[]="/tmp/L568-lock-XXXXXX";
    int fd=mkstemp(path); must(fd>=3,"opened independent lock descriptor");
    must(flock(fd,LOCK_EX|LOCK_NB)==0,"lock acquired");
    signal(SIGINT,SIG_IGN); signal(SIGQUIT,SIG_IGN);
    must(prepare_test_entry()==0,"entry setup succeeds");
    struct sigaction a,q; sigaction(SIGINT,NULL,&a); sigaction(SIGQUIT,NULL,&q);
    must(a.sa_handler==SIG_DFL&&q.sa_handler==SIG_DFL,"fixture defaults INT and QUIT");
    must(fcntl(fd,F_GETFD)&FD_CLOEXEC,"existing descriptor still open and CLOEXEC");
    pid_t p=fork(); must(p>=0,"fork child");
    if(!p) { close(fd); int other=open(path,O_RDWR); int r=flock(other,LOCK_EX|LOCK_NB); _exit(r<0&&(errno==EWOULDBLOCK||errno==EAGAIN)?0:1); }
    int st; waitpid(p,&st,0); must(WIFEXITED(st)&&WEXITSTATUS(st)==0,"independent opener excluded while driver holds descriptor");
    close(fd); int other=open(path,O_RDWR); must(flock(other,LOCK_EX|LOCK_NB)==0,"lock released after explicit driver close"); close(other); unlink(path);
    puts("ENTRY_PROBE_PASS"); return 0;
}
