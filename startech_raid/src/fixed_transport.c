/* Original fixed-read monitor transport. No vendor source or firmware included. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define _FILE_OFFSET_BITS 64
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <scsi/sg.h>
enum kind { ATA_ID, SMART_DATA, SMART_THRESH, INQUIRY, FW_VERSION, RAID_STATE };
struct request { const char *name; unsigned char cdb[16]; unsigned len, bytes; enum kind kind; int port; };
#define S16(n,p,f,m,h,c,k) {n,{0x85,p,0x0e,0,f,0,1,0,0,0,m,0,h,0xa0,c,0},16,512,k,(p==0x1a?0:1)}
static const struct request requests[] = {
 S16("identify0",0x1a,0,0,0,0xec,ATA_ID), S16("identify1",0x1c,0,0,0,0xec,ATA_ID),
 S16("smart0",0x1a,0xd0,0x4f,0xc2,0xb0,SMART_DATA), S16("smart1",0x1c,0xd0,0x4f,0xc2,0xb0,SMART_DATA),
 S16("thresholds0",0x1a,0xd1,0x4f,0xc2,0xb0,SMART_THRESH), S16("thresholds1",0x1c,0xd1,0x4f,0xc2,0xb0,SMART_THRESH),
};
static const struct request inquiry={"inquiry",{0x12,0,0,0,36,0},6,36,INQUIRY,-1};
/* Separate immutable request: never accepts an address or length from the CLI. */
static const struct request fw_version={"fw-version",{0xe4,6,0,7,0xf0,0},6,6,FW_VERSION,-1};
static const struct request raid_state={"raid-state",{0xe4,1,0,0x0a,0xed,0},6,1,RAID_STATE,-1};
static void js(const char *s) {
 putchar('"'); for (;*s;s++) { unsigned char c=(unsigned char)*s;
  if(c=='"'||c=='\\') {putchar('\\');putchar(c);} else if(c<32||c>126) printf("\\u%04x",c); else putchar(c);
 } putchar('"');
}
static void hex(const unsigned char *p,size_t n) { putchar('"');for(size_t i=0;i<n;i++)printf("%02x",p[i]);putchar('"'); }
static const struct request *lookup(const char *s) { for(size_t i=0;i<sizeof(requests)/sizeof(*requests);i++)if(!strcmp(s,requests[i].name))return &requests[i];return NULL; }
static bool uniform(const unsigned char *b) { for(unsigned i=1;i<512;i++)if(b[i]!=b[0])return false;return true; }
static unsigned word(const unsigned char *b,unsigned i) { return (unsigned)b[2*i]|((unsigned)b[2*i+1]<<8); }
static bool smart_valid(const unsigned char *b) { unsigned sum=0;for(unsigned i=0;i<512;i++)sum+=b[i];return !uniform(b)&&word(b,0)!=0&&word(b,0)!=65535&&!(sum&255); }
static bool attrs_valid(const unsigned char *b) {
 bool seen[256]={false};for(unsigned i=2;i<362;i+=12)if(b[i]) {if(seen[b[i]])return false;seen[b[i]]=true;}return true;
}
static void ata_string(const unsigned char *b,unsigned w,unsigned n,char *out) {
 for(unsigned i=0;i<n;i++){unsigned char c=b[2*w+(i^1)];out[i]=(c>=32&&c<127)?(char)c:'?';}out[n]=0;
 while(n&&out[n-1]==' ')out[--n]=0;
}
static bool textfile(const char *p,char *out,size_t n) {
 int fd=open(p,O_RDONLY|O_CLOEXEC);if(fd<0)return false;ssize_t z=read(fd,out,n-1);close(fd);if(z<0||(size_t)z>=n-1)return false;
 out[z]=0;while(z>0&&isspace((unsigned char)out[z-1]))out[--z]=0;return true;
}
static bool usb_matches(const char *sys,const char *expected,const char *serial) {
 char parent[PATH_MAX],path[PATH_MAX],vid[16],pid[16],sn[256],pair[64];
 if(strlen(sys)>=sizeof(parent))return false;
 strcpy(parent,sys);
 for(unsigned depth=0;depth<24;depth++) {
  if(snprintf(path,sizeof(path),"%s/idVendor",parent)>=(int)sizeof(path))return false;
  if(textfile(path,vid,sizeof(vid))) {
   if(snprintf(path,sizeof(path),"%s/idProduct",parent)>=(int)sizeof(path)||!textfile(path,pid,sizeof(pid)))return false;
   if(snprintf(path,sizeof(path),"%s/serial",parent)>=(int)sizeof(path)||!textfile(path,sn,sizeof(sn)))return false;
   snprintf(pair,sizeof(pair),"%s:%s",vid,pid);return !strcmp(pair,expected)&&!strcmp(sn,serial);
  }
  char *slash=strrchr(parent,'/');if(!slash||slash==parent)return false;*slash=0;
 }return false;
}
struct transfer { unsigned char sense[64]; unsigned sense_n; unsigned status,host,driver; int resid,os_error; };
static bool transfer_ok(const struct transfer *t) { return !t->os_error&&!t->status&&!t->host&&!t->driver&&t->resid==0; }
static bool send_read(int fd,const struct request *r,unsigned char *data,struct transfer *t) {
 memset(t,0,sizeof(*t));memset(data,0,r->bytes);sg_io_hdr_t io;memset(&io,0,sizeof(io));
 io.interface_id='S';io.dxfer_direction=SG_DXFER_FROM_DEV;io.cmd_len=(unsigned char)r->len;io.mx_sb_len=sizeof(t->sense);
 /* Kernel SG_IO does not modify the CDB; a local copy avoids casting away const. */
 unsigned char cdb[16];memcpy(cdb,r->cdb,sizeof(cdb));io.cmdp=cdb;io.sbp=t->sense;io.dxferp=data;io.dxfer_len=r->bytes;io.timeout=3000;
 if(ioctl(fd,SG_IO,&io)<0)t->os_error=errno;
 t->sense_n=io.sb_len_wr<=sizeof(t->sense)?io.sb_len_wr:sizeof(t->sense);t->status=io.status;t->host=io.host_status;t->driver=io.driver_status;t->resid=io.resid;
 return transfer_ok(t);
}
static void trimfield(const unsigned char *p,size_t n,char *out) { memcpy(out,p,n);out[n]=0;while(n&&out[n-1]==' ')out[--n]=0; }
static bool live_sysfs_identity(const char *sys) {
 const char *names[]={"vendor","model","rev"};
 const char *values[]={"StarTech","RAID 1","0"};
 char p[PATH_MAX],value[128];
 for(unsigned i=0;i<3;i++) {
  if(snprintf(p,sizeof(p),"%s/device/%s",sys,names[i])>=(int)sizeof(p)||!textfile(p,value,sizeof(value))||strcmp(value,values[i]))return false;
 }
 return usb_matches(sys,"174c:55aa","00000000000000000001");
}
static bool live_inquiry_identity(const unsigned char *b) {
 char vendor[9],product[17],revision[5];
 trimfield(b+8,8,vendor);trimfield(b+16,16,product);trimfield(b+32,4,revision);
 return b[0]==0&&b[4]>=31&&!strcmp(vendor,"StarTech")&&!strcmp(product,"RAID 1")&&!strcmp(revision,"0");
}
