/* Review-only host probe; uses real compiled OSC cores and existing observer. */
#include "osh_shell.h"
#include "osh_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
typedef struct { OshShell *sh; int counts, errors; int expected, expected_staged; } Check;
static void pipeline(void *ctx, const uint64_t *r, size_t nr, const uint64_t *o, size_t no, const OshResult *res) {
    (void)r;(void)nr;(void)o;(void)no;
    Check *c=ctx;
    if(c->sh->s.abort_list){
        c->counts++;
        if(res->status!=1 || c->sh->s.last_status!=c->expected || c->sh->s.exit_requested || c->sh->w[OSH_S_LAST_STATUS]!=(uint64_t)c->expected_staged) c->errors++;
        fprintf(stdout,"{\"event\":\"abort-observer\",\"request_status\":%d,\"session_status\":%d,\"staged_before_update\":%llu}\n",res->status,c->sh->s.last_status,(unsigned long long)c->sh->w[OSH_S_LAST_STATUS]);
    }
}
static void handler(int s){(void)s;}
int main(int argc, char **argv){
    if(argc!=3)return 64;
    char home[4096],state[4096];snprintf(home,sizeof home,"HOME=%s",argv[2]);snprintf(state,sizeof state,"XDG_STATE_HOME=%s",argv[2]);
    char *env[]={"PATH=/usr/bin:/bin","LC_ALL=C","TZ=UTC",home,state,NULL};
    OshShell sh;if(osh_shell_init(&sh,env,atoi(argv[1]),0,"L705",NULL,0))return 70;
    Check c={.sh=&sh};sh.diff.ctx=&c;sh.diff.on_pipeline=pipeline;
    struct {const char *name,*text;int command,interactive,initial,want,counts;} tests[]={
      {"script-F1","printf \"\" | exit 37\nexit 7 9; exit 93\n",0,0,0,37,1},
      {"command-after-script","exit 7 9\nprintf bad\n",1,0,37,1,1},
      {"script-after-command","exit 7 9\n",0,0,1,1,1},
      {"same-line-reset","printf \"\"; exit 7 9\n",0,0,37,1,1},
      {"same-line-update","printf \"\" | exit 12; exit 7 9\n",0,0,0,12,1},
      {"consecutive","printf \"\" | exit 37\nexit 7 9\nexit 7 9\n",0,0,0,37,2},
      {"interactive-script","exit 7 9\n",0,1,37,37,1},
      {"interactive-command","exit 7 9\n",1,1,37,1,1},
      {"clean-after-abort","printf \"\"\n",0,0,37,0,0},
    };
    int fail=0;for(size_t i=0;i<sizeof tests/sizeof tests[0];i++){
        sh.s.last_status=tests[i].initial;sh.interactive=tests[i].interactive;c.expected=tests[i].want;c.expected_staged=tests[i].command?tests[i].initial:tests[i].want; if(!strcmp(tests[i].name,"same-line-reset"))c.expected_staged=0;c.counts=0;c.errors=0;
        struct sigaction a={0},before,after;a.sa_handler=handler;sigemptyset(&a.sa_mask);sigaction(SIGINT,&a,&before);
        int rc=tests[i].command?osh_shell_run_string(&sh,tests[i].text,strlen(tests[i].text)):osh_shell_run_script(&sh,tests[i].text,strlen(tests[i].text));
        sigaction(SIGINT,NULL,&after);sigaction(SIGINT,&before,NULL);
        int ok=rc==tests[i].want&&sh.s.last_status==tests[i].want&&sh.s.abort_list==0&&sh.s.exit_requested==0&&c.counts==tests[i].counts&&c.errors==0&&after.sa_handler==handler;
        printf("{\"case\":\"%s\",\"rc\":%d,\"last\":%d,\"abort\":%d,\"exit_requested\":%d,\"observed_count_requests\":%d,\"pass\":%s}\n",tests[i].name,rc,sh.s.last_status,sh.s.abort_list,sh.s.exit_requested,c.counts,ok?"true":"false");if(!ok)fail++;
    }
    osh_shell_free(&sh);return fail?1:0;
}
