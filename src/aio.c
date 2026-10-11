/* TRMOfficial file browser and optional FTP service. Default: FTP disabled. */
#include "aio.h"
#include "pldmgr.h"
#include "json_helpers.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>

static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t lifecycle=PTHREAD_MUTEX_INITIALIZER;
static pthread_t worker;
static int enabled=0, listener=-1, control=-1, passive=-1, datafd=-1, transfers=0;
/* Resolve symlinks before permitting writes. PS5 system partitions are browse-only. */
static int writable(const char *p) {
    return !strncmp(p,"/data/",6) || (strlen(p)>9 && !strncmp(p,"/mnt/usb",8) && p[8]>='0' && p[8]<='7' && p[9]=='/');
}
static int path_resolve(const char *cwd,const char *input,char *out,int create) {
    char joined[PATH_MAX],parent[PATH_MAX];
    if(!input || !*input) input=cwd;
    if(snprintf(joined,sizeof joined,"%s%s%s",input[0]=='/'?"":cwd,input[0]=='/'?"":"/",input)>=(int)sizeof joined) return -1;
    if(realpath(joined,out)) return 0;
    if(!create) return -1;
    char *slash=strrchr(joined,'/'); if(!slash || !slash[1] || !strcmp(slash+1,".") || !strcmp(slash+1,"..")) return -1;
    char name[NAME_MAX+1]; if(strlen(slash+1)>NAME_MAX) return -1; strcpy(name,slash+1);
    *slash=0; if(!realpath(*joined?joined:"/",parent)) return -1;
    return snprintf(out,PATH_MAX,"%s%s%s",parent,strcmp(parent,"/")?"/":"",name)>=PATH_MAX?-1:0;
}
static int sendall(int fd,const void *buf,size_t n) {
    const char *p=buf; while(n) {ssize_t s=send(fd,p,n,0);if(s<=0)return -1;p+=s;n-=s;}return 0;
}
static void reply(int fd,const char *s) {sendall(fd,s,strlen(s));}
static int alive(void) {pthread_mutex_lock(&lock);int v=enabled;pthread_mutex_unlock(&lock);return v;}
static int waitfd(int fd,int seconds) {fd_set f;FD_ZERO(&f);FD_SET(fd,&f);struct timeval t={seconds,0};return select(fd+1,&f,0,0,&t)>0;}
static void close_slot(int *slot) {pthread_mutex_lock(&lock);if(*slot>=0)close(*slot);*slot=-1;pthread_mutex_unlock(&lock);}
static int publish(int *slot,int fd) {pthread_mutex_lock(&lock);if(!enabled){pthread_mutex_unlock(&lock);close(fd);return -1;}*slot=fd;pthread_mutex_unlock(&lock);return fd;}
static int open_data(void) {
    if(passive<0 || !waitfd(passive,15)) return -1;
    struct sockaddr_in remote; socklen_t len=sizeof remote;
    int fd=accept(passive,(struct sockaddr*)&remote,&len);
    struct sockaddr_in peer;len=sizeof peer;
    if(fd>=0 && (!getpeername(control,(struct sockaddr*)&peer,&len)) && peer.sin_addr.s_addr!=remote.sin_addr.s_addr){close(fd);fd=-1;}
    close_slot(&passive);if(fd<0)return -1;
    struct timeval timeout={15,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);
    return publish(&datafd,fd);
}
static void session(int fd) {
    char cwd[PATH_MAX]="/",rename_from[PATH_MAX]="",line[4096];size_t used=0;int logged=0,idle=0;
    reply(fd,"220 Payload Manager FTP ready\r\n");
    while(alive()) {
        if(!waitfd(fd,1)){if(++idle>120)break;continue;}idle=0;
        char c;if(recv(fd,&c,1,0)!=1)break;if(c=='\r')continue;
        if(c!='\n'){if(used+1>=sizeof line)break;line[used++]=c;continue;}
        line[used]=0;used=0;char *arg=strchr(line,' ');if(arg)*arg++=0;else arg="";
        for(char *p=line;*p;p++)if(*p>='a'&&*p<='z')*p-=32;
        if(!strcmp(line,"QUIT")){reply(fd,"221 Bye\r\n");break;}
        if(!strcmp(line,"USER")){reply(fd,"331 Send password\r\n");continue;}
        if(!strcmp(line,"PASS")){logged=1;reply(fd,"230 Logged in\r\n");continue;}
        if(!strcmp(line,"SYST")){reply(fd,"215 UNIX Type: L8\r\n");continue;}
        if(!strcmp(line,"FEAT")){reply(fd,"211-Features\r\n EPSV\r\n SIZE\r\n UTF8\r\n211 End\r\n");continue;}
        if(!logged){reply(fd,"530 Login required\r\n");continue;}
        if(!strcmp(line,"TYPE")||!strcmp(line,"OPTS")||!strcmp(line,"NOOP")){reply(fd,"200 OK\r\n");continue;}
        if(!strcmp(line,"PWD")||!strcmp(line,"XPWD")){char msg[PATH_MAX+30];snprintf(msg,sizeof msg,"257 \"%s\"\r\n",cwd);reply(fd,msg);continue;}
        if(!strcmp(line,"PASV")||!strcmp(line,"EPSV")) {
            close_slot(&passive);int s=socket(AF_INET,SOCK_STREAM,0);struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_ANY);
            if(s<0 || bind(s,(struct sockaddr*)&a,sizeof a)<0 || listen(s,1)<0){if(s>=0)close(s);reply(fd,"425 Cannot open passive listener\r\n");continue;}
            socklen_t len=sizeof a;getsockname(s,(struct sockaddr*)&a,&len);int port=ntohs(a.sin_port);publish(&passive,s);
            char msg[120];if(!strcmp(line,"EPSV"))snprintf(msg,sizeof msg,"229 Entering Extended Passive Mode (|||%d|)\r\n",port);
            else {getsockname(fd,(struct sockaddr*)&a,&len);unsigned ip=ntohl(a.sin_addr.s_addr);snprintf(msg,sizeof msg,"227 Entering Passive Mode (%u,%u,%u,%u,%d,%d)\r\n",ip>>24,(ip>>16)&255,(ip>>8)&255,ip&255,port/256,port%256);}reply(fd,msg);continue;
        }
        char path[PATH_MAX];int create=!strcmp(line,"STOR")||!strcmp(line,"MKD")||!strcmp(line,"RNTO");
        if(!strcmp(line,"CDUP"))arg="..";
        if(!strcmp(line,"LIST") && arg[0]=='-')arg="";
        if(path_resolve(cwd,arg,path,create)){reply(fd,"550 Path unavailable\r\n");continue;}
        struct stat st;
        if(!strcmp(line,"CWD")||!strcmp(line,"CDUP")){if(stat(path,&st)||!S_ISDIR(st.st_mode))reply(fd,"550 Not a directory\r\n");else{strcpy(cwd,path);reply(fd,"250 Directory changed\r\n");}continue;}
        if(!strcmp(line,"SIZE")){char msg[80];if(stat(path,&st)||!S_ISREG(st.st_mode))reply(fd,"550 File unavailable\r\n");else{snprintf(msg,sizeof msg,"213 %lld\r\n",(long long)st.st_size);reply(fd,msg);}continue;}
        if(!strcmp(line,"RNFR")){if(!writable(path))reply(fd,"550 Read-only location\r\n");else{strcpy(rename_from,path);reply(fd,"350 Send RNTO\r\n");}continue;}
        if(!strcmp(line,"RNTO")||!strcmp(line,"DELE")||!strcmp(line,"RMD")||!strcmp(line,"MKD")) {
            int rc=-1;if(writable(path)){if(!strcmp(line,"RNTO")){if(*rename_from)rc=rename(rename_from,path);*rename_from=0;}else if(!strcmp(line,"DELE"))rc=unlink(path);else if(!strcmp(line,"RMD"))rc=rmdir(path);else rc=mkdir(path,0755);}reply(fd,rc?"550 Operation failed or read-only\r\n":"250 Complete\r\n");continue;
        }
        if(!strcmp(line,"LIST")||!strcmp(line,"NLST")||!strcmp(line,"RETR")||!strcmp(line,"STOR")) {
            int store=!strcmp(line,"STOR"),list=!strcmp(line,"LIST")||!strcmp(line,"NLST");
            if(store && !writable(path)){reply(fd,"550 Read-only location\r\n");continue;}
            reply(fd,"150 Opening data connection\r\n");int d=open_data();if(d<0){reply(fd,"425 Passive connection unavailable\r\n");continue;}
            pthread_mutex_lock(&lock);transfers=1;pthread_mutex_unlock(&lock);int ok=1;
            if(list){DIR *dir=opendir(path);if(!dir)ok=0;else{struct dirent *e;while(ok && alive() && (e=readdir(dir))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;char item[PATH_MAX],msg[PATH_MAX+100];snprintf(item,sizeof item,"%s/%s",path,e->d_name);if(lstat(item,&st))continue;
                if(!strcmp(line,"NLST"))snprintf(msg,sizeof msg,"%s\r\n",e->d_name);else snprintf(msg,sizeof msg,"%crw-r--r-- 1 ps5 ps5 %lld Jan 01 00:00 %s\r\n",S_ISDIR(st.st_mode)?'d':'-',(long long)st.st_size,e->d_name);if(sendall(d,msg,strlen(msg)))ok=0;}closedir(dir);}}
            else {char temp[PATH_MAX];int f=-1;if(store){if(snprintf(temp,sizeof temp,"%s.trm-XXXXXX",path)<(int)sizeof temp)f=mkstemp(temp);}else f=open(path,O_RDONLY);
                if(f<0)ok=0;else{char buf[65536];ssize_t n;while(alive()) {n=store?recv(d,buf,sizeof buf,0):read(f,buf,sizeof buf);if(!n)break;if(n<0){ok=0;break;}if(store){size_t pos=0;while(pos<(size_t)n){ssize_t w=write(f,buf+pos,n-pos);if(w<=0){ok=0;break;}pos+=w;}if(!ok)break;}else if(sendall(d,buf,n)){ok=0;break;}}
                if(!alive())ok=0;if(close(f))ok=0;if(store){if(ok){if(rename(temp,path))ok=0;}if(!ok)unlink(temp);}}}
            close_slot(&datafd);pthread_mutex_lock(&lock);transfers=0;pthread_mutex_unlock(&lock);reply(fd,ok?"226 Transfer complete\r\n":"451 Transfer failed\r\n");continue;
        }
        reply(fd,"502 Command not supported\r\n");
    }
}
static void *ftp_worker(void *unused) {
    (void)unused;while(alive()){if(!waitfd(listener,1))continue;int fd=accept(listener,0,0);if(fd<0)continue;if(publish(&control,fd)<0)break;struct timeval timeout={15,0};setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);session(fd);close_slot(&control);close_slot(&passive);close_slot(&datafd);}return NULL;
}
void aio_shutdown(void) {
    pthread_mutex_lock(&lock);int running=enabled;enabled=0;int slots[]={listener,control,passive,datafd};for(unsigned i=0;i<4;i++)if(slots[i]>=0)shutdown(slots[i],SHUT_RDWR);pthread_mutex_unlock(&lock);
    if(running)pthread_join(worker,0);close_slot(&listener);
}
static int ftp_start(void) {
    pthread_mutex_lock(&lock);if(enabled){pthread_mutex_unlock(&lock);return 0;}
    int fd=socket(AF_INET,SOCK_STREAM,0),one=1;struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_port=htons(2121);a.sin_addr.s_addr=htonl(INADDR_ANY);
    if(fd>=0)setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
    if(fd<0 || bind(fd,(struct sockaddr*)&a,sizeof a)<0 || listen(fd,4)<0){if(fd>=0)close(fd);pthread_mutex_unlock(&lock);return -1;}
    listener=fd;enabled=1;if(pthread_create(&worker,0,ftp_worker,0)){enabled=0;close(fd);listener=-1;pthread_mutex_unlock(&lock);return -1;}pthread_mutex_unlock(&lock);return 0;
}
static enum MHD_Result json_reply(struct MHD_Connection *c,int code,const char *text) {
    struct MHD_Response *r=MHD_create_response_from_buffer(strlen(text),(void*)text,MHD_RESPMEM_MUST_COPY);if(!r)return MHD_NO;
    MHD_add_response_header(r,"Content-Type","application/json");MHD_add_response_header(r,"Cache-Control","no-store");MHD_add_response_header(r,"Access-Control-Allow-Origin","*");enum MHD_Result rc=MHD_queue_response(c,code,r);MHD_destroy_response(r);return rc;
}
enum MHD_Result aio_request(struct MHD_Connection *c,const char *url,const char *method) {
    if(!strcmp(url,"/aio/ftp")) {
        if(!strcmp(method,"POST")){const char *on=MHD_lookup_connection_value(c,MHD_GET_ARGUMENT_KIND,"enabled");if(!on || (strcmp(on,"1")&&strcmp(on,"0")))return json_reply(c,400,"{\"error\":\"Invalid toggle\"}");pthread_mutex_lock(&lifecycle);int failed=0;if(!strcmp(on,"1"))failed=ftp_start();else aio_shutdown();pthread_mutex_unlock(&lifecycle);if(failed)return json_reply(c,409,"{\"error\":\"FTP port 2121 unavailable\"}");}
        else if(strcmp(method,"GET"))return json_reply(c,405,"{\"error\":\"Method not allowed\"}");
        char b[160];pthread_mutex_lock(&lock);snprintf(b,sizeof b,"{\"enabled\":%s,\"port\":2121,\"transfers\":%d}",enabled?"true":"false",transfers);pthread_mutex_unlock(&lock);return json_reply(c,200,b);
    }
    const char *arg=MHD_lookup_connection_value(c,MHD_GET_ARGUMENT_KIND,"path");char path[PATH_MAX];
    if(path_resolve("/",arg?arg:"/",path,!strcmp(url,"/aio/mkdir")))return json_reply(c,404,"{\"error\":\"Path unavailable\"}");
    if(!strcmp(url,"/aio/files") && !strcmp(method,"GET")) {
        DIR *dir=opendir(path);if(!dir)return json_reply(c,403,"{\"error\":\"Cannot read directory\"}");
        size_t cap=1024*1024;char *buf=malloc(cap);if(!buf){closedir(dir);return json_reply(c,500,"{\"error\":\"Out of memory\"}");}
        JsonListBuilder jb={buf,cap,0,1};char escaped[PATH_MAX*2];pldmgr_json_escape(path,escaped,sizeof escaped);json_append(&jb,"{\"path\":\"%s\",\"writable\":%s,\"entries\":[",escaped,writable(path)?"true":"false");
        struct dirent *e;int first=1,overflow=0;while((e=readdir(dir))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;char item[PATH_MAX];struct stat st;if(snprintf(item,sizeof item,"%s/%s",path,e->d_name)>=(int)sizeof item || lstat(item,&st))continue;char name[NAME_MAX*6+1];pldmgr_json_escape(e->d_name,name,sizeof name);if(json_append(&jb,"%s{\"name\":\"%s\",\"directory\":%s,\"size\":%lld,\"symlink\":%s}",first?"":",",name,S_ISDIR(st.st_mode)?"true":"false",(long long)st.st_size,S_ISLNK(st.st_mode)?"true":"false")){overflow=1;break;}first=0;}
        closedir(dir);if(overflow || json_append(&jb,"]}")){free(buf);return json_reply(c,413,"{\"error\":\"Directory too large\"}");}enum MHD_Result rc=json_reply(c,200,buf);free(buf);return rc;
    }
    if(!strcmp(url,"/aio/download") && !strcmp(method,"GET")) {
        int fd=open(path,O_RDONLY);struct stat st;if(fd<0)return json_reply(c,404,"{\"error\":\"File unavailable\"}");if(fstat(fd,&st)||!S_ISREG(st.st_mode)){close(fd);return json_reply(c,400,"{\"error\":\"Not a regular file\"}");}
        struct MHD_Response *r=MHD_create_response_from_fd64(st.st_size,fd);if(!r){close(fd);return MHD_NO;}MHD_add_response_header(r,"Content-Type","application/octet-stream");MHD_add_response_header(r,"Content-Disposition","attachment");enum MHD_Result rc=MHD_queue_response(c,200,r);MHD_destroy_response(r);return rc;
    }
    if(strcmp(method,"POST"))return json_reply(c,405,"{\"error\":\"Method not allowed\"}");
    if(!writable(path))return json_reply(c,403,"{\"error\":\"System locations are read-only\"}");
    int rc=-1;
    if(!strcmp(url,"/aio/mkdir"))rc=mkdir(path,0755);
    else if(!strcmp(url,"/aio/delete")){struct stat st;if(!lstat(path,&st))rc=S_ISDIR(st.st_mode)?rmdir(path):unlink(path);}
    else if(!strcmp(url,"/aio/rename")){char dest[PATH_MAX];const char *to=MHD_lookup_connection_value(c,MHD_GET_ARGUMENT_KIND,"to");if(!to || path_resolve("/",to,dest,1)||!writable(dest))return json_reply(c,400,"{\"error\":\"Invalid destination\"}");struct stat st;if(!lstat(dest,&st))return json_reply(c,409,"{\"error\":\"Destination already exists\"}");rc=rename(path,dest);}
    else return json_reply(c,404,"{\"error\":\"Unknown operation\"}");
    return json_reply(c,rc?409:200,rc?"{\"error\":\"Operation failed (directories must be empty to delete)\"}":"{\"ok\":true}");
}
