/* Original end-user monitor. Reuses our fixed-read transport and identity checks.
 * No vendor code, smartctl executable, shell or arbitrary request is invoked. */
#include "fixed_transport.c"
#include <inttypes.h>
#include <time.h>
#define STATUS_VERSION "0.3.2"
struct disk_info {
 bool identified, data_ok, thresholds_ok;
 unsigned char id[512], data[512], thresholds[512];
 char model[41], serial[21], firmware[9];
 const char *error;
};
struct audit_entry {const struct request *r;struct transfer t;unsigned char bytes[512];};
struct status_report {
 bool state_ok, smart_requested, duplicate_identity;
 unsigned raw, count;
 const char *error;
 struct disk_info disks[2];
 struct audit_entry audit[9];
};
static const char *state_name(unsigned raw) {
 switch(raw){case 1:return "Healthy";case 2:return "Degraded";case 3:return "Failed";case 4:return "Rebuilding";default:return "Unknown";}
}
static bool monitor_read(int fd,const struct request *r,unsigned char *b,struct status_report *out) {
 if(out->count>=9)return false;
 struct audit_entry *a=&out->audit[out->count++];a->r=r;
 bool ok=send_read(fd,r,b,&a->t);memcpy(a->bytes,b,r->bytes);return ok;
}
static bool identify_valid(const unsigned char *b) {
 if(uniform(b)||!word(b,0)||word(b,0)==65535||(word(b,0)&0x8000))return false;
 if(b[510]==0xa5){unsigned sum=0;for(unsigned i=0;i<512;i++)sum+=b[i];if(sum&255)return false;}
 return true;
}
static int monitor_collect(bool smart,struct status_report *out) {
 memset(out,0,sizeof(*out));out->smart_requested=smart;
 if(geteuid()!=0){out->error="Root and block-device access required";return 3;}
 char sys[PATH_MAX];struct stat st;
 if(!realpath("/sys/class/block/sda",sys)||!live_sysfs_identity(sys)){out->error="Pinned StarTech USB/SCSI identity mismatch";return 3;}
 int fd=open("/dev/sda",O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
 if(fd<0){out->error="Cannot open /dev/sda read-only; use HAOS host or configured monitor app";return 3;}
 int rc=3;unsigned char b[512];
 if(fstat(fd,&st)||!S_ISBLK(st.st_mode)||st.st_rdev!=makedev(8,0)||flock(fd,LOCK_EX|LOCK_NB)){out->error="Block identity or cooperative lock failed";goto done;}
 if(!monitor_read(fd,&inquiry,b,out)||!live_inquiry_identity(b)){out->error="INQUIRY failed or SCSI identity mismatched";goto done;}
 if(!live_sysfs_identity(sys)){out->error="Identity changed after INQUIRY";goto done;}
 static const unsigned char expected[]={0x16,0x10,0x28,0xb5,0x30,0x81};
 if(!monitor_read(fd,&fw_version,b,out)||memcmp(b,expected,6)){out->error="Firmware version gate failed; expected 161028_B5_30_81";goto done;}
 if(!live_sysfs_identity(sys)){out->error="Identity changed before state read";goto done;}
 if(!monitor_read(fd,&raid_state,b,out)){out->error="State read failed or incomplete";rc=4;goto done;}
 out->state_ok=true;out->raw=b[0];rc=out->raw>=1&&out->raw<=4?0:4;
 if(!live_sysfs_identity(sys)){out->error="Identity changed after state read; discard entire sample";out->state_ok=false;rc=3;goto done;}
 if(rc||!smart)goto done;
 for(unsigned port=0;port<2;port++) {
  struct disk_info *d=&out->disks[port];char name[32];
  if(!live_sysfs_identity(sys)){out->error="Identity changed; discard entire sample";out->state_ok=false;rc=3;goto done;}
  snprintf(name,sizeof(name),"identify%u",port);
  if(!monitor_read(fd,lookup(name),d->id,out)){d->error="IDENTIFY transport failed; port presence unknown";continue;}
  if(!identify_valid(d->id)){d->error="Invalid IDENTIFY response";continue;}
  ata_string(d->id,27,40,d->model);ata_string(d->id,10,20,d->serial);ata_string(d->id,23,8,d->firmware);
  if(!*d->model||!*d->serial){d->error="Missing SSD identity";continue;}
  d->identified=true;
  if(!(word(d->id,82)&1)||!(word(d->id,85)&1)){d->error="SMART not reported supported and enabled; no enabling command sent";continue;}
  if(!live_sysfs_identity(sys)){out->error="Identity changed; discard entire sample";out->state_ok=false;rc=3;goto done;}
  snprintf(name,sizeof(name),"smart%u",port);
  if(!monitor_read(fd,lookup(name),d->data,out)){d->error="SMART data transport failed";continue;}
  if(!smart_valid(d->data)||!attrs_valid(d->data)){d->error="Invalid SMART data checksum/revision/attributes";continue;}
  d->data_ok=true;
  if(!live_sysfs_identity(sys)){out->error="Identity changed; discard entire sample";out->state_ok=false;rc=3;goto done;}
  snprintf(name,sizeof(name),"thresholds%u",port);
  if(!monitor_read(fd,lookup(name),d->thresholds,out)){d->error="SMART thresholds transport failed";continue;}
  if(!smart_valid(d->thresholds)||!attrs_valid(d->thresholds)){d->error="Invalid SMART thresholds checksum/revision/attributes";continue;}
  d->thresholds_ok=true;
 }
 if(!live_sysfs_identity(sys)){out->error="Identity changed; discard entire sample";out->state_ok=false;rc=3;goto done;}
 if(out->disks[0].identified&&out->disks[1].identified&&!strcmp(out->disks[0].serial,out->disks[1].serial))out->duplicate_identity=true;
done:
 close(fd);return rc;
}
static unsigned threshold_for(const struct disk_info *d,unsigned id) {
 if(!d->thresholds_ok)return 0;
 for(unsigned j=2;j<362;j+=12)if(d->thresholds[j]==id)return d->thresholds[j+1];
 return 0;
}
static const char *attribute_check(const struct disk_info *d,unsigned *compared,unsigned *failed) {
 *compared=*failed=0;
 if(!d->data_ok||!d->thresholds_ok)return "Unknown";
 for(unsigned i=2;i<362;i+=12)if(d->data[i]&&(d->data[i+1]&1)) {
  unsigned t=threshold_for(d,d->data[i]),v=d->data[i+3];
  if(t&&v&&v!=255){(*compared)++;if(v<=t)(*failed)++;}
 }
 return !*compared?"Unknown":*failed?"Failed":"Passed";
}
static int temperature_hint(const struct disk_info *d,unsigned *id) {
 *id=0;if(!d->data_ok)return -1;
 for(unsigned target=194;target>=190;target-=4)for(unsigned i=2;i<362;i+=12)
  if(d->data[i]==target&&d->data[i+5]>0&&d->data[i+5]<126){*id=target;return d->data[i+5];}
 return -1;
}
static uint64_t raw48(const unsigned char *b){uint64_t v=0;for(unsigned i=0;i<6;i++)v|=(uint64_t)b[i]<<(8*i);return v;}
static uint64_t capacity(const struct disk_info *d) {
 uint64_t sectors=0,logical=512;
 if(!d->identified)return 0;
 unsigned w83=word(d->id,83);
 if((w83&0xc000)==0x4000&&(w83&0x400))for(unsigned i=0;i<4;i++)sectors|=(uint64_t)word(d->id,100+i)<<(16*i);
 else sectors=(uint64_t)word(d->id,60)|((uint64_t)word(d->id,61)<<16);
 unsigned w106=word(d->id,106);
 if((w106&0xc000)==0x4000&&(w106&0x1000))logical=2*((uint64_t)word(d->id,117)|((uint64_t)word(d->id,118)<<16));
 return !logical||logical>1048576||sectors>UINT64_MAX/logical?0:sectors*logical;
}
static void disk_json(const struct disk_info *d,unsigned port,bool requested,bool identity_trusted) {
 unsigned compared,failed,temp_id;const char *check=attribute_check(d,&compared,&failed);int temp=temperature_hint(d,&temp_id);
 printf("{\"port\":%u,\"requested\":%s,\"identity_verified\":%s,\"presence\":",port,requested?"true":"false",identity_trusted&&d->identified?"true":"false");
 js(!requested?"not_queried":d->identified?"responded_to_identify":"unknown");
 printf(",\"model\":");if(d->identified)js(d->model);else printf("null");
 printf(",\"serial\":");if(d->identified)js(d->serial);else printf("null");
 printf(",\"firmware\":");if(d->identified)js(d->firmware);else printf("null");
 printf(",\"capacity_bytes\":");uint64_t bytes=capacity(d);if(bytes)printf("%" PRIu64,bytes);else printf("null");
 printf(",\"smart_data_valid\":%s,\"smart_thresholds_valid\":%s,\"smart_overall_health\":\"Unknown\",\"ata_return_status_queried\":false",d->data_ok?"true":"false",d->thresholds_ok?"true":"false");
 printf(",\"attribute_check\":");js(identity_trusted?check:"Unknown");printf(",\"attributes_compared\":%u,\"failed_prefail_attributes\":%u",compared,failed);
 printf(",\"temperature_candidate_c\":");if(temp>=0&&identity_trusted)printf("%d",temp);else printf("null");
 printf(",\"temperature_attribute\":%u,\"temperature_source\":\"vendor_attribute_raw_byte0_unverified\",\"array_membership\":\"Unknown\",\"error\":",temp_id);
 if(d->error)js(d->error);else printf("null");printf(",\"attributes\":[");
 bool first=true;if(d->data_ok)for(unsigned i=2;i<362;i+=12)if(d->data[i]) {
  if(!first)putchar(',');
  first=false;
  printf("{\"id\":%u,\"flags\":%u,\"normalized\":%u,\"worst\":%u,\"raw\":%" PRIu64 ",\"raw_hex\":",d->data[i],d->data[i+1]|(d->data[i+2]<<8),d->data[i+3],d->data[i+4],raw48(d->data+i+5));hex(d->data+i+5,6);
  printf(",\"threshold\":");unsigned t=threshold_for(d,d->data[i]);if(t)printf("%u",t);else printf("null");putchar('}');
 }
 printf("]}");
}
static bool smart_complete(const struct status_report *s) {
 return s->state_ok&&!s->duplicate_identity&&s->disks[0].identified&&s->disks[1].identified&&s->disks[0].thresholds_ok&&s->disks[1].thresholds_ok;
}
static void report_json(const struct status_report *s) {
 time_t now=time(NULL);
 printf("{\"schema\":1,\"tool_version\":\"" STATUS_VERSION "\",\"sample_time_unix\":%lld,\"device\":\"/dev/sda\",\"expected_firmware\":\"161028_B5_30_81\",\"query_ok\":%s,\"raid_raw\":",(long long)now,s->state_ok?"true":"false");
 if(s->state_ok)printf("%u",s->raw);else printf("null");
 const char *label=state_name(s->state_ok?s->raw:0);printf(",\"raid_status\":");js(label);
 char display[64];if(s->state_ok)snprintf(display,sizeof(display),"%u - %s",s->raw,label);else snprintf(display,sizeof(display),"Unknown - query failed");
 printf(",\"raid_display\":");js(display);
 printf(",\"mapping_validation\":\"static_reverse_engineering\",\"transport_validated_on_target\":true,\"known_condition_validated\":false,\"array_synchronization_verified\":false,\"state_address\":\"0x0aed\",\"smart_requested\":%s,\"smart_complete\":%s,\"duplicate_ssd_identity\":%s,\"error\":",s->smart_requested?"true":"false",smart_complete(s)?"true":"false",s->duplicate_identity?"true":"false");
 if(s->error)js(s->error);else printf("null");
 printf(",\"ssds\":[");for(unsigned p=0;p<2;p++){if(p)putchar(',');disk_json(&s->disks[p],p,s->smart_requested,s->state_ok&&!s->duplicate_identity);}
 printf("],\"transfers\":[");for(unsigned i=0;i<s->count;i++) {
  const struct audit_entry *a=&s->audit[i];const struct transfer *t=&a->t;if(i)putchar(',');
  printf("{\"request\":");js(a->r->name);printf(",\"cdb_hex\":");hex(a->r->cdb,a->r->len);
  printf(",\"bytes\":%u,\"timeout_ms\":3000,\"direction\":\"from_device\",\"errno\":%d,\"scsi_status\":%u,\"host_status\":%u,\"driver_status\":%u,\"residual\":%d,\"sense_hex\":",a->r->bytes,t->os_error,t->status,t->host,t->driver,t->resid);hex(t->sense,t->sense_n);printf(",\"response_hex\":");hex(a->bytes,a->r->bytes);putchar('}');
 }
 puts("]}");
}
static void report_text(const struct status_report *s) {
 if(s->state_ok)printf("RAID status: %u - %s\n",s->raw,state_name(s->raw));else printf("RAID status: Unknown\n");
 puts("Source: controller state byte; mapping from firmware analysis; mirror synchronization not independently verified.");
 if(s->error)printf("Error: %s\n",s->error);
 if(s->duplicate_identity)puts("Warning: both ports returned the same serial; per-SSD attribution is unverified.");
 if(s->smart_requested)for(unsigned p=0;p<2;p++) {
  const struct disk_info *d=&s->disks[p];unsigned compared,failed,id;int temp=temperature_hint(d,&id);
  printf("SSD port %u: %s | serial: %s | firmware: %s\n",p,d->identified?d->model:"Unknown",d->identified?d->serial:"Unknown",d->identified?d->firmware:"Unknown");
  uint64_t bytes=capacity(d);if(bytes)printf("  Capacity: %" PRIu64 " bytes\n",bytes);
  printf("  SMART overall: Unknown (ATA RETURN STATUS is unsupported by this bridge transport)\n  Pre-fail attribute thresholds: %s\n",s->state_ok&&!s->duplicate_identity?attribute_check(d,&compared,&failed):"Unknown");
  if(temp>=0&&s->state_ok&&!s->duplicate_identity)printf("  Temperature candidate: %d C (attribute %u; vendor encoding unverified)\n",temp,id);
  else puts("  Temperature: Unknown");
  if(d->error)printf("  Detail: %s\n",d->error);
 }
}
#ifndef STATUS_ENTRY
#define STATUS_ENTRY main
#endif
int STATUS_ENTRY(int argc,char **argv) {
 if(argc==1||(argc==2&&!strcmp(argv[1],"--help"))) {
  puts("StarTech RAID Status " STATUS_VERSION "\n  status [--json] [--smart]\n  explain BYTE\nNo hardware access without 'status'. Fixed StarTech /dev/sda and firmware 161028_B5_30_81.\n--smart adds six fixed read-only ATA requests maximum; no self-tests or SMART enabling.\nJSON: one object, exit 0 when requested data collected (including unhealthy states).\nExit 2: arguments; 3: identity/access/version; 4: unknown state/transport; 7: partial SMART.\nRead RAID status independently of SMART. See DOCS.md.");return 0;
 }
 if(argc==3&&!strcmp(argv[1],"explain")) {
  char *end=NULL;errno=0;unsigned long value=strtoul(argv[2],&end,10);
  if(errno||!*argv[2]||*end||argv[2][0]=='-'||value>255)return 2;
  printf("%lu - %s\n",value,state_name((unsigned)value));return 0;
 }
 if(strcmp(argv[1],"status")){fputs("Unknown command; use --help\n",stderr);return 2;}
 bool json=false,smart=false;
 for(int i=2;i<argc;i++) {
  if(!strcmp(argv[i],"--json")&&!json)json=true;
  else if(!strcmp(argv[i],"--smart")&&!smart)smart=true;
  else {fputs("Unknown or duplicate option; no device opened\n",stderr);return 2;}
 }
 struct status_report result;int rc=monitor_collect(smart,&result);
 if(!rc&&smart&&!smart_complete(&result))rc=7;
 if(json)report_json(&result);else report_text(&result);
 if(fflush(stdout))return 4;
 return rc;
}
