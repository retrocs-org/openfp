/* SPDX-License-Identifier: MIT */
/* One bounded HTTPS worker; the match loop never waits for account verification. */
#include <stdio.h>
#include <string.h>
#include "server/cloud_auth.h"
#include "common/cloud.h"
#include "common/http.h"
#ifndef _WIN32
#include <pthread.h>
static pthread_t worker;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready=PTHREAD_COND_INITIALIZER;
static int running,started;
static char server_name[33],server_secret[65];
static struct {int state;uint32_t at;char ticket[65],account[33],name[NET_NAME];uint8_t sid[16],kid[16];uint64_t nonce;} jobs[16];
static void *work(void *unused) {
    (void)unused;pthread_mutex_lock(&lock);
    while(running){int i;char body[640],reply[256],hex[65],account[33]="",name[NET_NAME]="";int valid;uint8_t decoded[16];
        for(i=0;i<16;i++)if(jobs[i].state==1)break;
        if(i==16){pthread_cond_wait(&ready,&lock);continue;}
        jobs[i].state=2;snprintf(body,sizeof body,"action=verify");
        http_form(body,sizeof body,"server",server_name);http_form(body,sizeof body,"secret",server_secret);
        http_form(body,sizeof body,"ticket",jobs[i].ticket);cloud_hex(jobs[i].sid,16,hex);http_form(body,sizeof body,"server_id",hex);
        cloud_hex(jobs[i].kid,16,hex);http_form(body,sizeof body,"key_id",hex);
        snprintf(hex,sizeof hex,"%08x%08x",(unsigned)(jobs[i].nonce>>32),(unsigned)jobs[i].nonce);http_form(body,sizeof body,"nonce",hex);
        pthread_mutex_unlock(&lock);
        valid=http_has_tls()&&http_request(CLOUD_API,body,reply,sizeof reply,3000)==200&&
            cloud_field(reply,"id",account,sizeof account)&&!cloud_unhex(account,decoded,16)&&cloud_field(reply,"username",name,sizeof name);
        memset(body,0,sizeof body);pthread_mutex_lock(&lock);
        snprintf(jobs[i].account,sizeof jobs[i].account,"%s",account);snprintf(jobs[i].name,sizeof jobs[i].name,"%s",name);
        jobs[i].state=valid?3:-1;jobs[i].at=net_ms();
    }
    pthread_mutex_unlock(&lock);return NULL;
}
int server_cloud_start(const char *server,const char *secret){
    if(started)return 0;
    if(!http_has_tls()||!server[0]||strlen(secret)!=64)return -1;
    http_init();memset(jobs,0,sizeof jobs);snprintf(server_name,sizeof server_name,"%s",server);snprintf(server_secret,sizeof server_secret,"%s",secret);running=1;
    if(pthread_create(&worker,NULL,work,NULL)){running=0;return -1;}started=1;return 0;
}
void server_cloud_stop(void){if(!started)return;pthread_mutex_lock(&lock);running=0;pthread_cond_signal(&ready);pthread_mutex_unlock(&lock);pthread_join(worker,NULL);started=0;memset(server_secret,0,sizeof server_secret);memset(jobs,0,sizeof jobs);}
int server_cloud_verify(void *ctx,const char *ticket,const uint8_t sid[16],uint64_t nonce,const uint8_t kid[16],char account[33],char name[NET_NAME]){
    int i,result=0,free_slot=-1;uint8_t decoded[32];(void)ctx;if(!started||cloud_unhex(ticket,decoded,32))return -1;pthread_mutex_lock(&lock);
    for(i=0;i<16;i++){
        if(jobs[i].state&&!strcmp(jobs[i].ticket,ticket)&&jobs[i].nonce==nonce&&!memcmp(jobs[i].sid,sid,16)&&!memcmp(jobs[i].kid,kid,16)){
            result=jobs[i].state==3?1:jobs[i].state<0?-1:0;
            if(result==1){snprintf(account,33,"%s",jobs[i].account);snprintf(name,NET_NAME,"%s",jobs[i].name);}
            /* Admission consumes the result. Retransmits from admitted clients are
             * handled before this hook; finished jobs must not block reconnects. */
            if(result) memset(&jobs[i],0,sizeof jobs[i]);
            pthread_mutex_unlock(&lock);return result;
        }
        if(!jobs[i].state||((jobs[i].state==3||jobs[i].state<0)&&net_ms()-jobs[i].at>65000))free_slot=i;
    }
    if(free_slot>=0){i=free_slot;memset(&jobs[i],0,sizeof jobs[i]);snprintf(jobs[i].ticket,65,"%s",ticket);memcpy(jobs[i].sid,sid,16);memcpy(jobs[i].kid,kid,16);jobs[i].nonce=nonce;jobs[i].state=1;pthread_cond_signal(&ready);}
    else result=-1;
    pthread_mutex_unlock(&lock);return result;
}
#else
int server_cloud_start(const char *s,const char *k){(void)s;(void)k;return -1;}
void server_cloud_stop(void){}
int server_cloud_verify(void *c,const char *t,const uint8_t s[16],uint64_t n,const uint8_t k[16],char a[33],char u[NET_NAME]){(void)c;(void)t;(void)s;(void)n;(void)k;(void)a;(void)u;return -1;}
#endif
