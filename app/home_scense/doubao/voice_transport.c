/****************************************************************************
 * app/home_scense/doubao/voice_transport.c
 * Curl TLS + manual WebSocket for Doubao RealtimeAPI.
 ****************************************************************************/

#include "voice_transport.h"
#include <curl/curl.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#define DOUBAO_ERR(fmt, ...) \
  do { FILE *_f=fopen("/tmp/doubao.log","a"); \
    if(_f){fprintf(_f,fmt"\n",##__VA_ARGS__);fclose(_f);} \
    fprintf(stderr,fmt"\n",##__VA_ARGS__); }while(0)

#define WS_FIN 0x80u
#define WS_OPCODE_BINARY 0x02u
#define WS_OPCODE_CLOSE 0x08u
#define WS_OPCODE_PING 0x09u
#define WS_OPCODE_PONG 0x0Au
#define WS_MASK 0x80u

struct voice_transport_s { CURL *curl; curl_socket_t fd; char logid[96]; uint8_t pbf[4096]; int pbf_len; };

static ssize_t curl_send(CURL *c, const void *d, size_t n) {
  size_t s=0; CURLcode r=curl_easy_send(c,d,n,&s);
  return (r==CURLE_OK)?(ssize_t)s:-EIO; }

static int curl_recv_to(CURL *c, curl_socket_t fd, void *b, size_t n, int ms) {
  struct timeval st,now,df; int el; size_t nr; CURLcode r;
  gettimeofday(&st,NULL);
  do { r=curl_easy_recv(c,b,n,&nr); if(r==CURLE_OK&&nr>0)return(int)nr;
    { fd_set f;struct timeval tv={0,20000};
      FD_ZERO(&f);FD_SET(fd,&f);select(FD_SETSIZE,&f,NULL,NULL,&tv); }
    gettimeofday(&now,NULL);timersub(&now,&st,&df);
    el=(int)(df.tv_sec*1000+df.tv_usec/1000);
  } while(el<ms);
  return 0; }

static ssize_t curl_send_to(CURL *c, curl_socket_t fd, const void *d, size_t n, int ms) {
  struct timeval st,now,df; int el; ssize_t s;
  gettimeofday(&st,NULL);
  do { s=curl_send(c,d,n); if(s>0)return s;
    /* send failed: drain mbedTLS pending server data, poll socket, retry */
    { char sink[256]; size_t nr=0;
      curl_easy_recv(c, sink, sizeof(sink), &nr);
}
    { fd_set f;struct timeval tv={0,20000};
      FD_ZERO(&f);FD_SET(fd,&f);select(FD_SETSIZE,&f,NULL,NULL,&tv); }
    gettimeofday(&now,NULL);timersub(&now,&st,&df);
    el=(int)(df.tv_sec*1000+df.tv_usec/1000);
  } while(el<ms);
  return -EIO; }

static int ws_upgrade(voice_transport_t *t, const voice_transport_config_t *cfg) {
  char req[1024],rsp[1024]; ssize_t tt=0,ln,ps;
  ln=snprintf(req,sizeof(req),
    "GET /api/v3/realtime/dialogue HTTP/1.1\r\n"
    "Host: openspeech.bytedance.com\r\n"
    "Upgrade: websocket\r\nConnection: Upgrade\r\n"
    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
    "Sec-WebSocket-Version: 13\r\n"
    "X-Api-App-ID: %s\r\nX-Api-Access-Key: %s\r\n"
    "X-Api-Resource-Id: %s\r\nX-Api-App-Key: %s\r\n"
    "%s%s%s\r\n",
    cfg->app_id,cfg->access_token,cfg->resource_id,cfg->app_key,
    cfg->connect_id?"X-Api-Connect-Id: ":"",
    cfg->connect_id?cfg->connect_id:"",
    cfg->connect_id?"\r\n":"");
  if(ln<0||(size_t)ln>=sizeof(req))return-EMSGSIZE;
  if(curl_send_to(t->curl,t->fd,req,(size_t)ln,5000)!=ln)return-EIO;
  while(tt<(ssize_t)(sizeof(rsp)-1)){
    ln=curl_recv_to(t->curl,t->fd,rsp+tt,sizeof(rsp)-1-(size_t)tt,8000);
    if(ln<=0){DOUBAO_ERR("doubao:WS timeout(%d)",(int)tt);return-EIO;}
    tt+=ln;rsp[tt]='\0';if(strstr(rsp,"\r\n\r\n"))break; }
  if(tt>=(ssize_t)(sizeof(rsp)-1))return-EMSGSIZE;
  { char*l=strcasestr(rsp,"X-Tt-Logid:");
    if(l){l+=10;while(*l==' '||*l=='\t')l++;
      for(ps=0;l[ps]&&l[ps]!='\r'&&l[ps]!='\n'&&ps<(ssize_t)(sizeof(t->logid)-1);ps++)
        t->logid[ps]=l[ps];t->logid[ps]='\0'; } }
  if(!strstr(rsp,"101")){ssize_t i;char s[256];
    size_t n=tt<(ssize_t)(sizeof(s)-1)?(size_t)tt:sizeof(s)-1;
    for(i=0;i<(ssize_t)n;i++)s[i]=rsp[i]>=0x20&&rsp[i]<=0x7e?rsp[i]:'.';
    s[n]='\0';DOUBAO_ERR("doubao:WS refused: %s",s);return-EIO;}
  return 0; }

static int ws_send(CURL *c, curl_socket_t fd, const uint8_t *p, size_t sz) {
  uint8_t hd[14],mk[4],*m;size_t ps=2,tt,i; int ret=-EIO;
  hd[0]=WS_FIN|WS_OPCODE_BINARY;hd[1]=WS_MASK;
  if(sz<=125)hd[1]|=(uint8_t)sz;
  else if(sz<=65535){hd[1]|=126;hd[2]=(uint8_t)(sz>>8);hd[3]=(uint8_t)sz;ps=4;}
  else return-EMSGSIZE;
  tt=ps+4+sz;m=malloc(tt);if(!m)return-ENOMEM;
  memcpy(m,hd,ps);
  mk[0]=(uint8_t)(rand()&0xff);mk[1]=(uint8_t)(rand()&0xff);
  mk[2]=(uint8_t)(rand()&0xff);mk[3]=(uint8_t)(rand()&0xff);
  memcpy(m+ps,mk,4);
  for(i=0;i<sz;i++)m[ps+4+i]=p[i]^mk[i&3];
  if(curl_send_to(c,fd,m,tt,10000)==(ssize_t)tt)ret=0;
  else DOUBAO_ERR("doubao:ws send fail %zu",sz);
  free(m);return ret; }

static int ws_recv(CURL *c, curl_socket_t fd, uint8_t *p, size_t cap, int ms) {
  uint8_t hd[2],op;uint64_t ln;size_t ps;int r;
  if(curl_recv_to(c,fd,hd,2,ms)!=2)return-EIO;
  op=hd[0]&0x0f;ln=hd[1]&0x7f;if(hd[1]&WS_MASK)return-EIO;
  if(ln==126){uint8_t e[2];if(curl_recv_to(c,fd,e,2,5000)!=2)return-EIO;
    ln=((uint64_t)e[0]<<8)|e[1];}
  else if(ln==127){uint8_t e[8];uint32_t hi,lo;
    if(curl_recv_to(c,fd,e,8,5000)!=8)return-EIO;
    hi=((uint32_t)e[0]<<24)|((uint32_t)e[1]<<16)|((uint32_t)e[2]<<8)|e[3];
    lo=((uint32_t)e[4]<<24)|((uint32_t)e[5]<<16)|((uint32_t)e[6]<<8)|e[7];
    ln=(uint64_t)hi*0x100000000u+lo;}
  if(ln>cap)return-EMSGSIZE;
  for(ps=0;ps<(size_t)ln;ps+=(size_t)r){
    r=curl_recv_to(c,fd,p+ps,(size_t)ln-ps,5000);if(r<=0)return-EIO;}
  switch(op){case WS_OPCODE_CLOSE:return-ECONNRESET;
  case WS_OPCODE_PING:{uint8_t pn[2]={WS_FIN|WS_OPCODE_PONG,0};
    curl_send(c,pn,2);return 0;}
  case WS_OPCODE_BINARY:return(int)ps;default:return(int)ps;}
}

int voice_transport_connect(voice_transport_t **o,const voice_transport_config_t *cfg,int ms){
  voice_transport_t*t;CURLcode r;
  if(!o||!cfg->url)return-EINVAL;
  t=calloc(1,sizeof(*t));if(!t)return-ENOMEM;t->fd=CURL_SOCKET_BAD;
  t->curl=curl_easy_init();if(!t->curl){free(t);return-ENOMEM;}
  curl_easy_setopt(t->curl,CURLOPT_URL,cfg->url);
  curl_easy_setopt(t->curl,CURLOPT_CONNECT_ONLY,1L);
  curl_easy_setopt(t->curl,CURLOPT_CONNECTTIMEOUT_MS,(long)ms);
  curl_easy_setopt(t->curl,CURLOPT_SSL_VERIFYPEER,0L);
  curl_easy_setopt(t->curl,CURLOPT_SSL_VERIFYHOST,0L);
  r=curl_easy_perform(t->curl);
  if(r!=CURLE_OK){DOUBAO_ERR("doubao:TLS fail:%s(%d)",curl_easy_strerror(r),(int)r);
    voice_transport_close(t);return-EIO;}
  r=curl_easy_getinfo(t->curl,CURLINFO_ACTIVESOCKET,&t->fd);
  if(r!=CURLE_OK||t->fd==CURL_SOCKET_BAD){
    DOUBAO_ERR("doubao:no sock");voice_transport_close(t);return-EIO;}
  /* Drain TLS post-handshake data (NewSessionTicket etc.) BEFORE ws_upgrade
   * so we don't accidentally consume WebSocket-level frames. */
  { int drained=0;
    while(drained<32768){char sink[256];size_t nr=0;
      if(curl_easy_recv(t->curl,sink,sizeof(sink),&nr)!=CURLE_OK||nr==0)break;
      drained+=(int)nr;}
}

  r=ws_upgrade(t,cfg);if(r<0){voice_transport_close(t);return r;}

  /* After ws_upgrade, drain leftover bytes (HTTP trailer garbage + first
   * real WS frame that were decrypted in the same TLS record). Save the
   * raw bytes — voice_transport_receive will skip the HTTP junk at front. */
  { size_t nr=0; t->pbf_len=0;
    while(t->pbf_len<4096&&curl_easy_recv(t->curl,t->pbf+t->pbf_len,
            sizeof(t->pbf)-t->pbf_len,&nr)==CURLE_OK&&nr>0)
      t->pbf_len+=(int)nr; }

  DOUBAO_ERR("doubao:WS OK logid=%s",t->logid);*o=t;return 0; }

int voice_transport_send(voice_transport_t*t,voice_ws_opcode_t op,
  const uint8_t*d,size_t n){
  (void)op;if(!t||!t->curl)return-EINVAL;
  return ws_send(t->curl,t->fd,d,n); }

int voice_transport_receive(voice_transport_t*t,voice_ws_opcode_t*op,
  uint8_t*d,size_t c,int ms){
  int r,start;if(!t||!t->curl||!op||!d||c==0)return-EINVAL;
  if(t->pbf_len>0){
    for(start=0;start<t->pbf_len-1;start++)
      if(t->pbf[start]==0x82) break;
    if(start<t->pbf_len-1){
      int n=t->pbf_len-start<(int)c?t->pbf_len-start:(int)c;
      memcpy(d,t->pbf+start,n);t->pbf_len=0;*op=VOICE_WS_BINARY;
      return n;}
    t->pbf_len=0;}
  r=ws_recv(t->curl,t->fd,d,c,ms);
  if(r==0)return 0;if(r==-ECONNRESET){*op=VOICE_WS_CLOSE;return r;}
  if(r<0)return 0;*op=VOICE_WS_BINARY;return r; }

const char* voice_transport_logid(const voice_transport_t*t){return t?t->logid:"";}

void voice_transport_close(voice_transport_t*t){
  uint8_t cf[6]={WS_FIN|WS_OPCODE_CLOSE,WS_MASK|2,0,0,0,0};
  if(!t)return;if(t->curl){
    if(t->fd!=CURL_SOCKET_BAD)curl_send(t->curl,cf,sizeof(cf));
    curl_easy_cleanup(t->curl);}free(t); }
