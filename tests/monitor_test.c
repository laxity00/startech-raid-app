/* All hardware access mocked. Synthetic data only; no physical devices. */
#include <assert.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <scsi/sg.h>
static int fake_open(const char *,int,...),fake_close(int),fake_fstat(int,struct stat *),fake_flock(int,int),fake_ioctl(int,unsigned long,...);
static uid_t fake_geteuid(void);
static char *fake_realpath(const char *,char *);
#define open fake_open
#define close fake_close
#define fstat fake_fstat
#define flock fake_flock
#define ioctl fake_ioctl
#define geteuid fake_geteuid
#define realpath fake_realpath
#define STATUS_ENTRY monitor_main
#include "../startech_raid/src/startech_status.c"
#undef open
#undef close
#undef fstat
#undef flock
#undef ioctl
#undef geteuid
#undef realpath
static char fixture[PATH_MAX];
static int scenario,calls,opens,closes;
static unsigned next_port;
static void put(const char *suffix,const char *value) {
 char p[PATH_MAX];assert(snprintf(p,sizeof(p),"%s/%s",fixture,suffix)<(int)sizeof(p));
 FILE *f=fopen(p,"w");assert(f);assert(fputs(value,f)>=0);assert(!fclose(f));
}
static int fake_open(const char *path,int flags,...) {
 if(!strcmp(path,"/dev/sda")) {
  assert((flags&O_ACCMODE)==O_RDONLY);assert(!(flags&(O_CREAT|O_TRUNC|O_EXCL)));
  assert((flags&(O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK))==(O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));
  opens++;
  if(scenario==12){errno=EPERM;return -1;}return 123;
 }
 assert(!strncmp(path,fixture,strlen(fixture)));return open(path,flags);
}
static int fake_close(int fd){if(fd==123){closes++;return 0;}return close(fd);}
static int fake_fstat(int fd,struct stat *s){assert(fd==123);memset(s,0,sizeof(*s));s->st_mode=S_IFBLK;s->st_rdev=makedev(8,scenario==11?1:0);return 0;}
static int fake_flock(int fd,int flags){assert(fd==123&&flags==(LOCK_EX|LOCK_NB));return scenario==20?-1:0;}
static uid_t fake_geteuid(void){return scenario==10?1000:0;}
static char *fake_realpath(const char *p,char *out){assert(!strcmp(p,"/sys/class/block/sda"));strcpy(out,fixture);return out;}
static void checksum(unsigned char *b){unsigned sum=0;for(unsigned i=0;i<511;i++)sum+=b[i];b[511]=(unsigned char)(0u-sum);}
static void aword(unsigned char *b,unsigned w,unsigned v){b[w*2]=(unsigned char)v;b[w*2+1]=(unsigned char)(v>>8);}
static void astr(unsigned char *b,unsigned w,unsigned n,const char *s){memset(b+w*2,' ',n);for(unsigned i=0;s[i]&&i<n;i++)b[w*2+(i^1)]=(unsigned char)s[i];}
static int fake_ioctl(int fd,unsigned long op,...) {
 assert(fd==123&&op==SG_IO);va_list ap;va_start(ap,op);sg_io_hdr_t *io=va_arg(ap,sg_io_hdr_t *);va_end(ap);
 calls++;assert(calls<=9&&io->dxfer_direction==SG_DXFER_FROM_DEV&&io->timeout==3000);
 unsigned char *b=io->dxferp;for(unsigned i=0;i<io->dxfer_len;i++)assert(b[i]==0);
 if(calls==1){
  const unsigned char cdb[]={0x12,0,0,0,36,0};assert(io->cmd_len==6&&io->dxfer_len==36&&!memcmp(io->cmdp,cdb,6));
  b[4]=31;memcpy(b+8,"StarTech",8);memcpy(b+16,"RAID 1          ",16);memcpy(b+32,"0   ",4);
  if(scenario==16)b[8]='X';
  if(scenario==17)put("serial","changed");
 }else if(calls==2){
  const unsigned char cdb[]={0xe4,6,0,7,0xf0,0};assert(io->cmd_len==6&&io->dxfer_len==6&&!memcmp(io->cmdp,cdb,6));
  const unsigned char version[]={0x16,0x10,0x28,0xb5,0x30,0x81};memcpy(b,version,6);
  if(scenario==1)b[5]=0x82;
  if(scenario==18)io->resid=1;
 }else if(calls==3){
  const unsigned char cdb[]={0xe4,1,0,0x0a,0xed,0};assert(io->cmd_len==6&&io->dxfer_len==1&&!memcmp(io->cmdp,cdb,6));b[0]=1;
  if(scenario==2)io->resid=1;
  if(scenario==8)b[0]=255;
  if(scenario==9)b[0]=2;
  if(scenario==15)io->resid=-1;
  if(scenario==19)io->driver_status=8;
  if(scenario==7)put("serial","changed");
  if(scenario==21)b[0]=3;
  if(scenario==22)b[0]=4;
  if(scenario==23)b[0]=0;
 }else{
  assert(io->cmd_len==16&&io->dxfer_len==512);unsigned char *c=io->cmdp;
  unsigned port=c[1]==0x1a?0:1;assert(c[1]==0x1a||c[1]==0x1c);
  assert(port>=next_port);next_port=port;
  unsigned char expected[16]={0x85,(unsigned char)(port?0x1c:0x1a),0x0e,0,0,0,1,0,0,0,0,0,0,0xa0,0,0};
  if(c[14]==0xec){
   expected[14]=0xec;assert(!memcmp(c,expected,16));
   if(scenario==3&&port==0){io->status=2;io->resid=512;return 0;}
   aword(b,0,0x40);aword(b,82,1);aword(b,85,scenario==14?0:1);
   aword(b,83,0x4400);aword(b,100,0x1000);aword(b,101,1);
   astr(b,27,40,"Synthetic SSD");astr(b,10,20,port&&scenario!=6?"TEST-SERIAL-1":"TEST-SERIAL-0");astr(b,23,8,"T001");
   b[510]=0xa5;checksum(b);
   if(scenario==24&&port==0)memset(b,0,512);
   if(scenario==25&&port==0)b[511]++;
  }else{
   assert(c[14]==0xb0&&(c[4]==0xd0||c[4]==0xd1));expected[14]=0xb0;expected[4]=c[4];expected[10]=0x4f;expected[12]=0xc2;assert(!memcmp(c,expected,16));
   if(c[4]==0xd1&&scenario==5&&port==0){errno=EIO;return -1;}
   b[0]=1;b[2]=5;
   if(c[4]==0xd0){b[3]=1;b[5]=(unsigned char)(scenario==13?5:100);b[6]=100;b[7]=0;b[14]=194;b[17]=100;b[18]=100;b[19]=35;}
   else b[3]=10;
   checksum(b);
   if(c[4]==0xd0&&scenario==4&&port==0)b[511]++;
   if(c[4]==0xd0&&scenario==26&&port==0)io->host_status=1;
   if(c[4]==0xd1&&scenario==27&&port==0)io->resid=512;
   if(c[4]==0xd0&&scenario==28&&port==0){b[26]=5;checksum(b);}
   if(c[4]==0xd1&&scenario==29&&port==0)b[511]++;
   if(c[4]==0xd1&&scenario==30&&port==1)put("serial","changed");
  }
 }
 return 0;
}
int main(void){
 strcpy(fixture,"/tmp/asm-monitor-test-XXXXXX");assert(mkdtemp(fixture));char p[PATH_MAX];
 assert(snprintf(p,sizeof(p),"%s/device",fixture)<(int)sizeof(p));assert(!mkdir(p,0700));
 put("device/vendor","StarTech\n");put("device/model","RAID 1\n");put("device/rev","0\n");put("idVendor","174c\n");put("idProduct","55aa\n");
 for(scenario=0;scenario<=30;scenario++){
  put("serial","00000000000000000001\n");calls=opens=closes=0;next_port=0;
  struct status_report result;int rc=monitor_collect(true,&result);
  int expected_rc=scenario==1||scenario==7||scenario==10||scenario==11||scenario==12||scenario==16||scenario==17||scenario==18||scenario==20||scenario==30?3:scenario==2||scenario==8||scenario==15||scenario==19||scenario==23?4:0;
  assert(rc==expected_rc);assert(result.count==(unsigned)calls);
  assert(opens==(scenario==10?0:1));assert(closes==(scenario==10||scenario==12?0:1));
  if(scenario==0){assert(calls==9&&smart_complete(&result));assert(capacity(&result.disks[0])==35651584);}
  if(scenario==3){assert(calls==7&&!result.disks[0].identified&&result.disks[1].thresholds_ok);}
  if(scenario==4){assert(calls==8&&!result.disks[0].data_ok&&result.disks[1].thresholds_ok);}
  if(scenario==6)assert(result.duplicate_identity&&!smart_complete(&result));
  if(scenario==8||scenario==23)assert(calls==3);
  if(scenario==14)assert(calls==5&&!result.disks[0].data_ok);
  if(scenario==13){unsigned compared,failed;assert(!strcmp(attribute_check(&result.disks[0],&compared,&failed),"Failed")&&failed==1);}
  if(scenario==7||scenario==30)assert(!result.state_ok);
  report_json(&result);
 }
 scenario=0;calls=opens=closes=0;put("serial","00000000000000000001\n");struct status_report s;
 assert(!monitor_collect(false,&s)&&calls==3&&!s.smart_requested);report_json(&s);
 /* Invalid command options must not open any device. */
 opens=calls=0;char *bad[]={"status","status","--device","/dev/sdb"};assert(monitor_main(4,bad)==2);assert(!opens&&!calls);
 char *duplicate_args[]={"status","status","--smart","--smart"};assert(monitor_main(4,duplicate_args)==2);assert(!opens&&!calls);
 /* Decoder edge cases: no temperature, missing threshold, corrupt old-age value. */
 memset(&s,0,sizeof(s));unsigned compared,failed,id;assert(temperature_hint(&s.disks[0],&id)==-1);
 assert(!strcmp(attribute_check(&s.disks[0],&compared,&failed),"Unknown"));
 FILE *sink=tmpfile();assert(sink);assert(!fflush(stdout));int saved=dup(STDOUT_FILENO);assert(saved>=0);assert(dup2(fileno(sink),STDOUT_FILENO)>=0);
 const int cli_scenarios[]={0,3,6,9,21,22,8,12};
 const int cli_codes[]={0,7,7,0,0,0,4,3};
 char *cli[]={"startech-status","status","--smart","--json"};
 for(unsigned i=0;i<sizeof(cli_scenarios)/sizeof(*cli_scenarios);i++){
  scenario=cli_scenarios[i];put("serial","00000000000000000001\n");calls=opens=closes=0;next_port=0;
  assert(monitor_main(4,cli)==cli_codes[i]);
 }
 assert(!fflush(stdout));assert(!ftruncate(fileno(sink),0));rewind(sink);
 scenario=0;put("serial","00000000000000000001\n");calls=opens=closes=0;next_port=0;
 char *human[]={"startech-status","status","--smart"};assert(!monitor_main(3,human));assert(!fflush(stdout));rewind(sink);
 char human_text[4096];size_t human_n=fread(human_text,1,sizeof(human_text)-1,sink);human_text[human_n]=0;
 assert(strstr(human_text,"1 - Healthy")&&strstr(human_text,"SSD port 0")&&strstr(human_text,"SSD port 1")&&strstr(human_text,"35 C"));
 assert(dup2(saved,STDOUT_FILENO)>=0);assert(!close(saved));assert(!fclose(sink));
 const char *files[]={"device/vendor","device/model","device/rev","idVendor","idProduct","serial"};
 for(unsigned i=0;i<sizeof(files)/sizeof(*files);i++){assert(snprintf(p,sizeof(p),"%s/%s",fixture,files[i])<(int)sizeof(p));assert(!unlink(p));}
 assert(snprintf(p,sizeof(p),"%s/device",fixture)<(int)sizeof(p));assert(!rmdir(p));assert(!rmdir(fixture));
 return 0;
}
