/* SPDX-License-Identifier: MIT */
/* Deterministic account client tests. The transport is a fixture, never the live service. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/cloud.h"
#include "common/http.h"
#include "common/client.h"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
static int tls=1,status=200,calls;
static char response[512],sent[1800];
int http_has_tls(void){return tls;}
int http_request(const char *url,const char *body,char *out,size_t cap,int timeout){
    CHECK(!strcmp(url,CLOUD_API));CHECK(timeout>0&&timeout<=6000);calls++;
    snprintf(sent,sizeof sent,"%s",body);snprintf(out,cap,"%s",response);return status;
}
void http_form(char *body,size_t cap,const char *key,const char *value){
    size_t n=strlen(body);const unsigned char *p=(const unsigned char *)value;
    n+=(size_t)snprintf(body+n,cap-n,"%s%s=",n?"&":"",key);
    for(;*p&&n+4<cap;p++) if((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_') body[n++]=(char)*p;
        else n+=(size_t)snprintf(body+n,cap-n,"%%%02X",*p);
    body[n]=0;
}

static Server public_server;
static NetClient clients[2];
static int checks, deny;
static int verify_account(void *ctx,const char *ticket,const uint8_t sid[16],uint64_t nonce,const uint8_t kid[16],char account[33],char name[NET_NAME]) {
    (void)ctx;(void)sid;(void)kid;
    CHECK(strlen(ticket)==64&&nonce);checks++;
    if(deny)return -1;
    if(checks%2)return 0; /* an asynchronous verification is still pending */
    strcpy(account,"0123456789abcdef0123456789abcdef");strcpy(name,"CanonicalPilot");return 1;
}
static void pump_account(unsigned ms) {
    uint32_t start=net_ms();
    do {server_step(&public_server);for(int i=0;i<2;i++)if(clients[i].socket_open)netc_step(&clients[i]);net_sleep(2);} while(net_ms()-start<ms);
}
static void admission_test(void) {
    ServerConfig cfg;ServerHooks hooks={0};uint8_t key[32]={7};char token[65];int before;
    server_default_config(&cfg);cfg.manual_start=1;strcpy(cfg.cloud_server,"test");hooks.cloud_verify=verify_account;
    CHECK(server_open(&public_server,0,&cfg,&hooks)==0);
    before=calls;
    CHECK(!netc_open(&clients[0],"127.0.0.1",net_port(public_server.sock),"Local","",key,TANK_MEDIUM,0));
    pump_account(200);CHECK(clients[0].state==NC_FAILED&&clients[0].reason==NR_CLOUD&&checks==0&&calls==before);
    CHECK(public_server.nmem_keys==0);netc_close(&clients[0]);
    snprintf(token,sizeof token,"%064d",7);cloud_register(key,token);
    snprintf(response,sizeof response,"ticket=%064d\n",8);status=200;
    CHECK(!netc_open(&clients[0],"127.0.0.1",net_port(public_server.sock),"SpoofName","",key,TANK_MEDIUM,0));
    pump_account(1500);CHECK(clients[0].state==NC_LOBBY&&checks>=2);
    int found=0;for(int i=0;i<NET_LOBBY_MAX;i++)if(public_server.c[i].used&&public_server.c[i].authed){
        CHECK(!strcmp(public_server.c[i].name,"CanonicalPilot"));CHECK(!strcmp(public_server.c[i].account_id,"0123456789abcdef0123456789abcdef"));found++;
    }CHECK(found==1);
    CHECK(!netc_open(&clients[1],"127.0.0.1",net_port(public_server.sock),"Duplicate","",key,TANK_MEDIUM,0));
    pump_account(1500);CHECK(clients[1].state==NC_FAILED&&clients[1].reason==NR_CLOUD);netc_close(&clients[1]);
    netc_close(&clients[0]);pump_account(150);
    CHECK(!netc_open(&clients[0],"127.0.0.1",net_port(public_server.sock),"Rejoined","",key,TANK_MEDIUM,0));
    pump_account(1500);CHECK(clients[0].state==NC_LOBBY);netc_close(&clients[0]);pump_account(100);
    deny=1;CHECK(!netc_open(&clients[0],"127.0.0.1",net_port(public_server.sock),"Refused","",key,TANK_MEDIUM,0));
    pump_account(300);CHECK(clients[0].state==NC_FAILED&&clients[0].reason==NR_CLOUD);netc_close(&clients[0]);server_close(&public_server);
    cloud_forget(key);server_default_config(&cfg);cfg.manual_start=1;
    CHECK(!server_open(&public_server,0,&cfg,NULL));before=calls;
    CHECK(!netc_open(&clients[0],"127.0.0.1",net_port(public_server.sock),"OfflinePilot","",key,TANK_MEDIUM,0));
    pump_account(400);CHECK(clients[0].state==NC_LOBBY&&calls==before);netc_close(&clients[0]);server_close(&public_server);
    puts("cloud admission: public rejects local, verifies asynchronously, canonical names, duplicate rejection, reconnect and private local login passed");
}
static void password_admission_test(void) {
    uint8_t key[32]={19}; char token[65];
    snprintf(token,sizeof token,"%064d",7);
    for(int public=0;public<2;public++) {
        ServerConfig cfg;ServerHooks hooks={0};server_default_config(&cfg);cfg.manual_start=1;
        strcpy(cfg.password,"server-secret");if(public)strcpy(cfg.cloud_server,"test");
        hooks.cloud_verify=verify_account;deny=0;
        CHECK(!server_open(&public_server,0,&cfg,&hooks));
        for(int cloud=0;cloud<2;cloud++) {
            if(cloud)cloud_register(key,token);else cloud_forget(key);
            for(int correct=0;correct<2;correct++) {
                int verified=checks;snprintf(response,sizeof response,"ticket=%064d\n",8);status=200;
                CHECK(!netc_open(&clients[0],"127.0.0.1",net_port(public_server.sock),"PasswordPilot",
                    correct?"server-secret":"wrong",key,TANK_MEDIUM,0));
                pump_account(1100);
                if(public&&!cloud) CHECK(clients[0].state==NC_FAILED&&clients[0].reason==NR_CLOUD&&checks==verified);
                else if(!correct) CHECK(clients[0].state==NC_FAILED&&clients[0].reason==NR_PASSWORD&&checks==verified);
                else CHECK(clients[0].state==NC_LOBBY);
                netc_close(&clients[0]);pump_account(80);
            }
        }
        cloud_forget(key);server_close(&public_server);
    }
    puts("server passwords: private local/cloud and public cloud admission enforce the same password");
}
int main(void){
    CloudAccount a;CloudDevice d;char error[96],hex[65],ticket[65],out[16];uint8_t key[32]={1},id[16]={2};int before;
    cloud_hex(key,32,hex);CHECK(!cloud_unhex(hex,key,32));CHECK(cloud_unhex("g0",key,1));CHECK(cloud_unhex("0",key,1));
    CHECK(cloud_field("username=Pilot\nsession=x\n","username",out,sizeof out)&&!strcmp(out,"Pilot"));
    CHECK(!cloud_field("key=12345\n","key",out,4));
    snprintf(response,sizeof response,"username=Pilot\nkey=%s\nsession=%064d\n",hex,1);
    CHECK(!cloud_login(&a,"Pilot","pilot@example.invalid","test&password=123",0,error));
    CHECK(!strcmp(a.name,"Pilot")&&!memcmp(key,a.key,32));
    CHECK(strstr(sent,"action=login")&&strstr(sent,"test%26password%3D123"));
    CHECK(!a.has_preferences);
    {
        char mask[6];cloud_password_mask("",mask,sizeof mask);CHECK(!mask[0]);
        cloud_password_mask("a",mask,sizeof mask);CHECK(!strcmp(mask,"*"));
        cloud_password_mask("\xc3\xa9",mask,sizeof mask);CHECK(!strcmp(mask,"*"));
        cloud_password_mask("abcdefgh",mask,sizeof mask);CHECK(!strcmp(mask,"*****"));
        CloudPreferences preferences={3,9,1};char session[65];strcpy(session,a.session);
        snprintf(response,sizeof response,"username=Pilot\nkey=%s\nsession=%s\npreferences=1\nfavorite_tank=3\navatar=9\nsteering=1\n",hex,session);
        CHECK(!cloud_profile_load(session,&a,error)&&a.has_preferences&&a.preferences.tank==3&&a.preferences.avatar==9&&a.preferences.steer==1);
        CHECK(strstr(sent,"action=profile&session=")&&strstr(sent,"client=" CLOUD_CLIENT_TYPE));
        strcpy(response,"status=saved\n");CHECK(!cloud_profile_save(session,&preferences,error));
        CHECK(strstr(sent,"favorite_tank=3")&&strstr(sent,"avatar=9")&&strstr(sent,"steering=1")&&strstr(sent,"client=" CLOUD_CLIENT_TYPE));
        snprintf(response,sizeof response,"username=Pilot\nkey=%s\nsession=%s\npreferences=1\nfavorite_tank=3\navatar=9\nsteering=0\nsteering_saved=0\n",hex,session);
        CHECK(!cloud_profile_load(session,&a,error)&&a.has_preferences&&!a.has_steering);
        snprintf(response,sizeof response,"username=Pilot\nkey=%s\nsession=%s\npreferences=1\nfavorite_tank=3\navatar=9\nsteering=1\nsteering_saved=1\n",hex,session);
        CHECK(!cloud_profile_load(session,&a,error)&&a.has_steering&&a.preferences.steer==1);
        before=calls;preferences.tank=4;CHECK(cloud_profile_save(session,&preferences,error)==-1&&calls==before);
        snprintf(response,sizeof response,"username=Pilot\nkey=%s\nsession=%s\npreferences=1\nfavorite_tank=3\navatar=10\nsteering=1\n",hex,session);
        CHECK(cloud_profile_load(session,&a,error)==-1);
    }
    cloud_register(a.key,a.session);
    snprintf(response,sizeof response,"ticket=%064d\n",2);
    CHECK(!cloud_join_ticket(a.key,"test",id,0x123456789abcdef0ULL,id,ticket,error));
    CHECK(strstr(sent,"nonce=123456789abcdef0")&&strstr(sent,"session="));
    cloud_forget(a.key);before=calls;CHECK(cloud_join_ticket(a.key,"test",id,1,id,ticket,error)==-1&&calls==before);
    tls=0;before=calls;CHECK(cloud_login(&a,"Pilot","","testpassword123",0,error)==-1&&calls==before);tls=1;
    strcpy(response,"error=Username or password is incorrect.\n");status=401;
    CHECK(cloud_login(&a,"Pilot","","wrong",0,error)==-1&&strstr(error,"incorrect"));
    status=202;strcpy(response,"status=verification_required\nmessage=Check your email to activate your account, then sign in.\n");
    memset(&a,0,sizeof a);error[0]=0;
    CHECK(cloud_login(&a,"Pilot","pilot@example.invalid","testpassword123",1,error)==-1&&strstr(error,"Check your email")&&!a.session[0]);
    status=200;strcpy(response,"username=Pilot\nkey=broken\nsession=broken\n");CHECK(cloud_login(&a,"Pilot","","x",0,error)==-1);
    snprintf(response,sizeof response,"device=%064d\npin=ABCD2345\n",3);CHECK(!cloud_device_start(&d,error));
    CHECK(!strcmp(d.pin,"ABCD2345"));status=202;strcpy(response,"status=pending\n");CHECK(cloud_device_poll(&d,&a,error)==0);
    status=200;snprintf(response,sizeof response,"username=Pilot\nkey=%s\nsession=%064d\n",hex,4);
    CHECK(cloud_device_poll(&d,&a,error)==1);
    admission_test();password_admission_test();
    puts("cloud: passwords encoded, HTTPS required, tokens validated, Apple TV pairing and ticket registry passed");return 0;
}
