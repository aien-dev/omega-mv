/* Independent controlling-terminal review probe. Child processes are owned fixtures. */
#define _GNU_SOURCE
#include <pty.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
static char buf[65536];
static int prompt(int fd){size_t n=0;for(int tries=0;tries<200;tries++){struct pollfd p={fd,POLLIN,0};int r=poll(&p,1,50);if(r<0&&errno==EINTR)continue;if(r<0)return -1;if(!r)continue;ssize_t z=read(fd,buf+n,sizeof buf-1-n);if(z<=0)return -1;n+=(size_t)z;buf[n]=0;if(n>=5&&!memcmp(buf+n-5,"osh$ ",5))return 0;if(n==sizeof buf-1)return -1;}return -1;}
static int sendcmd(int fd,const char *s){return write(fd,s,strlen(s))==(ssize_t)strlen(s)?prompt(fd):-1;}
int main(int argc,char **argv){if(argc!=5)return 64;int fd;pid_t pid=forkpty(&fd,NULL,NULL,NULL);if(pid<0)return 70;if(pid==0){setenv("HOME",argv[2],1);setenv("XDG_STATE_HOME",argv[2],1);setenv("LC_ALL","C",1);setenv("PATH","/usr/bin:/bin",1);setenv("PS1","osh$ ",1);setenv("PS2","> ",1);setenv("TERM","dumb",1);setenv("HISTFILE","/dev/null",1);setenv("OSH_INTERP",argv[3],1);if(!strcmp(argv[1],"/bin/bash"))execl(argv[1],argv[1],"--noprofile","--norc","-i",NULL);else execl(argv[1],argv[1],NULL);_exit(70);}
int ok=prompt(fd)==0;const char *body=atoi(argv[4])?"printf \"\" | exit 37; exit 7 9; printf \"LEAK\\n\"\n":"printf \"\" | exit 37; printf \"\"; exit 7 9; printf \"LEAK\\n\"\n";
if(ok)ok=sendcmd(fd,body)==0&&strstr(buf,"too many arguments")!=NULL;
if(ok)ok=sendcmd(fd,"printf \"L705=%s\\n\" \"$?\"\n")==0;
int observed=-1;if(ok){char *s=strstr(buf,"\nL705=");if(s)observed=atoi(s+6);ok=observed==(atoi(argv[4])?37:1);}
printf("{\"same_line_nonzero\":%s,\"observed\":%d,\"pass\":%s,\"trace\":\"status checked after prompt and count diagnostic\"}\n",atoi(argv[4])?"true":"false",observed,ok?"true":"false");
if(ok){const char *s="exit 0\n";if(write(fd,s,strlen(s))!=(ssize_t)strlen(s))ok=0;}
int status=0,done=0;for(int i=0;i<200;i++){pid_t p=waitpid(pid,&status,WNOHANG);if(p==pid){done=1;break;}usleep(50000);}if(!done){kill(pid,SIGKILL);waitpid(pid,&status,0);ok=0;}if(!WIFEXITED(status)||WEXITSTATUS(status)!=0)ok=0;close(fd);return ok?0:1;}
