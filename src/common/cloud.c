/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>
#include "common/cloud.h"
#ifndef __amigaos__
#include "common/http.h"
#endif
#ifdef __amigaos__
/* The native port does not link the generic HTTP transport. */
static void http_form(char *out,size_t cap,const char *key,const char *value) {
    static const char hex[]="0123456789ABCDEF";size_t n=strlen(out);const unsigned char *p;
    if(n&&n+1<cap)out[n++]='&';
    for(p=(const unsigned char *)key;*p&&n+1<cap;p++)out[n++]=(char)*p;
    if(n+1<cap)out[n++]='=';
    for(p=(const unsigned char *)value;*p;p++){
        if((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='-'||*p=='_'||*p=='.') {if(n+1>=cap)break;out[n++]=(char)*p;}
        else {if(n+3>=cap)break;out[n++]='%';out[n++]=hex[*p>>4];out[n++]=hex[*p&15];}
    }
    out[n]=0;
}
#endif
#ifdef __amigaos__
int amiga_cloud_request(const char *body, char *out, size_t cap);
#endif
static struct { uint8_t key[32]; char session[65]; } credentials[16];
void cloud_hex(const void *data, size_t n, char *out) {
    static const char h[]="0123456789abcdef"; const uint8_t *b=(const uint8_t *)data; size_t i;
    for(i=0;i<n;i++){out[i*2]=h[b[i]>>4];out[i*2+1]=h[b[i]&15];}out[n*2]=0;
}
int cloud_unhex(const char *hex, void *out, size_t n) {
    static const char digits[]="0123456789abcdef";
    uint8_t *b=(uint8_t *)out; size_t i; if(strlen(hex)!=n*2)return -1;
    for(i=0;i<n;i++){const char *a=strchr(digits,hex[i*2]),*c=strchr(digits,hex[i*2+1]);
        if(!a||!c||!*a||!*c)return -1;
        b[i]=(uint8_t)((a-digits)*16+c-digits);} return 0;
}
int cloud_field(const char *text,const char *name,char *out,size_t cap) {
    size_t n=strlen(name); const char *p=text; if(!cap)return 0; out[0]=0;
    while(*p){const char *e=strchr(p,'\n');size_t len=e?(size_t)(e-p):strlen(p);
        if(len>n&&!strncmp(p,name,n)&&p[n]=='='){size_t z=len-n-1;if(!z||z>=cap)return 0;memcpy(out,p+n+1,z);out[z]=0;return 1;}
        if(!e)break;
        p=e+1;}return 0;
}
static int request(const char *body,char *out,size_t cap,char error[96]) {
    int status;out[0]=0;
#ifdef __amigaos__
    status=amiga_cloud_request(body,out,cap);
#else
    if(!http_has_tls()){snprintf(error,96,"Cloud accounts need HTTPS support in this build.");return -1;}
    status=http_request(CLOUD_API,body,out,cap,6000);
#endif
    if(status==200||status==202)return status;
    if(!cloud_field(out,"error",error,96))snprintf(error,96,"Cloud account service unavailable. Try again.");
    return -1;
}
static int account_reply(const char *reply,CloudAccount *a,char error[96]) {
    CloudAccount next;char hex[65];memset(&next,0,sizeof next);
    if(!cloud_field(reply,"username",next.name,sizeof next.name)||!cloud_field(reply,"session",next.session,sizeof next.session)||
       !cloud_field(reply,"key",hex,sizeof hex)||cloud_unhex(hex,next.key,32)||cloud_unhex(next.session,hex,32)){
        snprintf(error,96,"Invalid account reply.");return -1;}
    *a=next;return 0;
}
int cloud_login(CloudAccount *a,const char *name,const char *email,const char *password,int create,char error[96]) {
    char body[1600]="",reply[512];int status;
    http_form(body,sizeof body,"action",create?"register":"login");http_form(body,sizeof body,"username",name);
    http_form(body,sizeof body,"email",email);http_form(body,sizeof body,"password",password);
    status=request(body,reply,sizeof reply,error);memset(body,0,sizeof body);
    if(status==202) {
        if(!cloud_field(reply,"message",error,96))snprintf(error,96,"Check your email to activate your account, then sign in.");
        return -1;
    }
    return status==200?account_reply(reply,a,error):-1;
}
int cloud_device_start(CloudDevice *d,char error[96]) {
    char reply[512],scratch[32];memset(d,0,sizeof *d);
    if(request("action=device_start",reply,sizeof reply,error)!=200)return -1;
    if (!cloud_field(reply,"device",d->secret,sizeof d->secret) || cloud_unhex(d->secret,scratch,32) ||
        !cloud_field(reply,"pin",d->pin,sizeof d->pin) || strlen(d->pin)!=8 ||
        strspn(d->pin,"ABCDEFGHJKLMNPQRSTUVWXYZ23456789")!=8) {
        snprintf(error,96,"Invalid pairing reply."); return -1;
    }
    return 0;
}
int cloud_device_poll(const CloudDevice *d,CloudAccount *a,char error[96]) {
    char body[160]="action=device_poll",reply[512];int status;
    http_form(body,sizeof body,"device",d->secret);status=request(body,reply,sizeof reply,error);
    if(status==202)return 0;
    if(status!=200)return -1;
    return account_reply(reply,a,error)?-1:1;
}
void cloud_register(const uint8_t key[32],const char *session) {
    int i,free_slot=-1;if(!key||!session||strlen(session)!=64)return;
    for(i=0;i<16;i++){if(!memcmp(credentials[i].key,key,32)){free_slot=i;break;}if(!credentials[i].session[0])free_slot=i;}
    if(free_slot>=0){memcpy(credentials[free_slot].key,key,32);snprintf(credentials[free_slot].session,65,"%s",session);}
}
void cloud_forget(const uint8_t key[32]) {int i;for(i=0;i<16;i++)if(!memcmp(credentials[i].key,key,32))memset(&credentials[i],0,sizeof credentials[i]);}
int cloud_join_ticket(const uint8_t key[32],const char *server,const uint8_t sid[16],uint64_t nonce,const uint8_t kid[16],char ticket[65],char error[96]) {
    int i;char body[640]="action=ticket",reply[256],hex[65];const char *token=NULL;
    for(i=0;i<16;i++)if(credentials[i].session[0]&&!memcmp(credentials[i].key,key,32)){token=credentials[i].session;break;}
    if(!token){snprintf(error,96,"Public servers require a cloud profile. Sign in first.");return -1;}
    http_form(body,sizeof body,"session",token);http_form(body,sizeof body,"server",server);
    cloud_hex(sid,16,hex);http_form(body,sizeof body,"server_id",hex);
    snprintf(hex,sizeof hex,"%08x%08x",(unsigned)(nonce>>32),(unsigned)nonce);http_form(body,sizeof body,"nonce",hex);
    cloud_hex(kid,16,hex);http_form(body,sizeof body,"key_id",hex);
    if(request(body,reply,sizeof reply,error)!=200)return -1;
    if(!cloud_field(reply,"ticket",ticket,65)||cloud_unhex(ticket,hex,32)){snprintf(error,96,"Invalid login ticket.");return -1;}return 0;
}
