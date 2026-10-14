#include "file_server.h"
#include "file_policy.h"
#include "network.h"
#include "cyd_hal.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <strings.h>
#include "esp_http_server.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "mbedtls/sha256.h"
#include "cJSON.h"
#include "ff.h"
#include "lwip/sockets.h"

extern const uint8_t html_begin[] asm("_binary_app_html_gz_start"),html_end[] asm("_binary_app_html_gz_end");
extern const uint8_t thin_begin[] asm("_binary_pixel_thin_woff2_start"),thin_end[] asm("_binary_pixel_thin_woff2_end");
extern const uint8_t bold_begin[] asm("_binary_pixel_semibold_woff2_start"),bold_end[] asm("_binary_pixel_semibold_woff2_end");

namespace fsapp {
namespace {
constexpr size_t buffer_size=16384;
httpd_handle_t server=nullptr;QueueHandle_t jobs=nullptr;TaskHandle_t worker=nullptr;uint8_t* buffer=nullptr;
SemaphoreHandle_t socket_guard=nullptr;
portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;TransferSnapshot progress{};bool cancelled=false;int active_socket=-1;
uint64_t total_storage=0,free_storage=0,login_window=0;unsigned login_failures=0;
char pin[9]{},session[33]{};
struct IoTiming {uint64_t receive_ms=0,write_ms=0,read_ms=0,send_ms=0;};
IoTiming timing{};
struct Job {httpd_req_t* request=nullptr;bool upload=false,head=false,ranged=false;char path[256]{},id[25]{},hash[65]{};uint32_t offset=0,length=0,size=0;};
const char* phase_name(Phase p){switch(p){case Phase::idle:return "idle";case Phase::upload:return "upload";case Phase::download:return "download";case Phase::complete:return "complete";case Phase::cancelled:return "cancelled";case Phase::error:return "error";}return "error";}
void headers(httpd_req_t* r){httpd_resp_set_hdr(r,"X-Content-Type-Options","nosniff");httpd_resp_set_hdr(r,"Cache-Control","no-store");httpd_resp_set_hdr(r,"Cross-Origin-Resource-Policy","same-origin");}
esp_err_t json(httpd_req_t* r,cJSON* object){if(!object)return ESP_FAIL;char* text=cJSON_PrintUnformatted(object);cJSON_Delete(object);if(!text)return ESP_FAIL;headers(r);httpd_resp_set_type(r,"application/json");auto code=httpd_resp_send(r,text,std::strlen(text));std::free(text);return code;}
esp_err_t error(httpd_req_t* r,const char* status,const char* message){httpd_resp_set_status(r,status);httpd_resp_set_hdr(r,"Connection","close");auto* object=cJSON_CreateObject();cJSON_AddStringToObject(object,"error",message);json(r,object);return ESP_FAIL;}
bool header(httpd_req_t* r,const char* name,char* out,size_t capacity){const size_t n=httpd_req_get_hdr_value_len(r,name);if(!n||n>=capacity)return false;return httpd_req_get_hdr_value_str(r,name,out,capacity)==ESP_OK;}
bool host_ok(httpd_req_t* r){char host[64];if(!header(r,"Host",host,sizeof(host)))return false;auto net=network_snapshot();char expected[32];
 if(net.ip[0]){std::snprintf(expected,sizeof(expected),"%s:%u",net.ip,server_port);if(!std::strcmp(host,expected))return true;}
 return net.setup&&!std::strcmp(host,"192.168.4.1:8080");
}
bool origin_ok(httpd_req_t* r){char origin[96],host[64];if(!httpd_req_get_hdr_value_len(r,"Origin"))return true;if(!header(r,"Origin",origin,sizeof(origin))||!header(r,"Host",host,sizeof(host)))return false;char expected[96];std::snprintf(expected,sizeof(expected),"http://%s",host);return !std::strcmp(expected,origin);}
bool authenticated(httpd_req_t* r){char cookie[256];if(!header(r,"Cookie",cookie,sizeof(cookie)))return false;const char* p=cookie;
 while(*p){while(*p==' '||*p==';')++p;const char* end=std::strchr(p,';');size_t n=end?size_t(end-p):std::strlen(p);if(n==36&&!std::strncmp(p,"cyd=",4)&&!std::memcmp(p+4,session,32))return true;if(!end)break;p=end+1;}return false;
}
bool access(httpd_req_t* r){if(!host_ok(r)||!origin_ok(r)){error(r,"403 Forbidden","Use the address shown on the CYD");return false;}if(!authenticated(r)){error(r,"401 Unauthorized","Enter the access code shown on the CYD");return false;}return true;}
bool query(httpd_req_t* r,const char* key,char* out,size_t capacity){char raw[768],encoded[768];const size_t n=httpd_req_get_url_query_len(r);if(!n||n>=sizeof(raw)||httpd_req_get_url_query_str(r,raw,sizeof(raw))!=ESP_OK||httpd_query_key_value(raw,key,encoded,sizeof(encoded))!=ESP_OK)return false;return decode_path(encoded,out,capacity);}
bool path_query(httpd_req_t* r,char (&path)[256],bool root=false){return query(r,"path",path,sizeof(path))&&shared_path(path,root);}
void fat_path(const char* path,char (&out)[260]){std::snprintf(out,sizeof(out),"0:%.255s",path);}
void refresh_storage(){uint64_t t=0,f=0;if(cyd::hal_sd_usage(t,f)){portENTER_CRITICAL(&lock);total_storage=t;free_storage=f;portEXIT_CRITICAL(&lock);}}
bool aborting(){portENTER_CRITICAL(&lock);bool result=cancelled;portEXIT_CRITICAL(&lock);return result;}
void update(uint64_t done){portENTER_CRITICAL(&lock);progress.done=done;portEXIT_CRITICAL(&lock);}
void finish(bool ok,const char* message){portENTER_CRITICAL(&lock);progress.busy=false;progress.phase=ok?Phase::complete:cancelled?Phase::cancelled:Phase::error;progress.ended_ms=cyd::hal_uptime_ms();std::snprintf(progress.message,sizeof(progress.message),"%s",message);active_socket=-1;portEXIT_CRITICAL(&lock);
 auto p=transfer_snapshot();std::printf("FS_TRANSFER phase=%s done=%llu total=%llu elapsed_ms=%llu heap=%u\n",phase_name(p.phase),static_cast<unsigned long long>(p.done),static_cast<unsigned long long>(p.total),static_cast<unsigned long long>(p.ended_ms-p.started_ms),cyd::hal_free_heap_kb());
}
bool send_all(httpd_req_t* r,const char* data,size_t size,int flags=0){while(size&&!aborting()){int n=httpd_socket_send(r->handle,httpd_req_to_sockfd(r),data,size,flags);if(n<=0)return false;data+=n;size-=n;}return !size;}
void escaped_name(const char* filename,char (&encoded)[390]){const char* digits="0123456789ABCDEF";size_t n=0;for(const unsigned char* p=reinterpret_cast<const unsigned char*>(filename);*p&&n+3<sizeof(encoded);++p){unsigned c=*p;if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c=='_')encoded[n++]=c;else{encoded[n++]='%';encoded[n++]=digits[c>>4];encoded[n++]=digits[c&15];}}encoded[n]=0;}
bool download(Job& job,char (&message)[96]){
 char path[260];fat_path(job.path,path);FIL file{};FRESULT code=f_open(&file,path,FA_READ);
 if(code!=FR_OK){std::snprintf(message,sizeof(message),"Cannot open file (%u)",code);error(job.request,"404 Not Found",message);return false;}
 bool ok=f_size(&file)==job.size&&f_lseek(&file,job.offset)==FR_OK;
 if(!ok){f_close(&file);error(job.request,"409 Conflict","File changed before download");std::strcpy(message,"File changed");return false;}
 char name[390],range[100]{},response[800];escaped_name(std::strrchr(job.path,'/')+1,name);
 if(job.ranged)std::snprintf(range,sizeof(range),"Content-Range: bytes %lu-%lu/%lu\r\n",static_cast<unsigned long>(job.offset),static_cast<unsigned long>(job.offset+job.length-1),static_cast<unsigned long>(job.size));
 std::snprintf(response,sizeof(response),"HTTP/1.1 %s\r\nContent-Type: application/octet-stream\r\nContent-Length: %lu\r\nContent-Disposition: attachment; filename*=UTF-8''%s\r\nAccept-Ranges: bytes\r\n%sCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n",job.ranged?"206 Partial Content":"200 OK",static_cast<unsigned long>(job.length),name,range);
 ok=send_all(job.request,response,std::strlen(response));uint32_t done=0;
 while(ok&&!job.head&&done<job.length&&!aborting()){
  const UINT n=std::min<uint32_t>(buffer_size,job.length-done);UINT read=0;const uint64_t read_start=cyd::hal_uptime_ms();code=f_read(&file,buffer,n,&read);timing.read_ms+=cyd::hal_uptime_ms()-read_start;
  if(code!=FR_OK||read!=n){ok=false;std::snprintf(message,sizeof(message),"SD read failed (%u)",code);break;}
  const uint64_t send_start=cyd::hal_uptime_ms();ok=send_all(job.request,reinterpret_cast<char*>(buffer),n,done+n<job.length?MSG_MORE:0);timing.send_ms+=cyd::hal_uptime_ms()-send_start;if(ok){done+=n;update(done);}
 }
 f_close(&file);if(ok&&!aborting())std::strcpy(message,job.head?"File information sent":"Download sent to browser");else if(!message[0])std::strcpy(message,aborting()?"Download cancelled":"Connection interrupted");
 return ok&&!aborting();
}
bool upload(Job& job,char (&message)[96]){
 char target[260],temporary[290];fat_path(job.path,target);const char* slash=std::strrchr(job.path,'/');const size_t directory=slash-job.path;
 std::snprintf(temporary,sizeof(temporary),"0:%.*s/.cyd-upload-%s.part",static_cast<int>(directory),job.path,job.id);
 FILINFO existing{};if(f_stat(target,&existing)==FR_OK){std::strcpy(message,"A file with this name already exists");error(job.request,"409 Conflict",message);return false;}
 FIL file{};auto code=f_open(&file,temporary,FA_WRITE|FA_CREATE_NEW);if(code!=FR_OK){std::snprintf(message,sizeof(message),"Cannot create upload (%u)",code);error(job.request,"507 Insufficient Storage",message);return false;}
 mbedtls_sha256_context hash;mbedtls_sha256_init(&hash);bool ok=mbedtls_sha256_starts(&hash,0)==0;uint32_t done=0;unsigned timeouts=0;
 while(ok&&done<job.length&&!aborting()){
  const size_t n=std::min<uint32_t>(buffer_size,job.length-done);size_t received=0;const uint64_t receive_start=cyd::hal_uptime_ms();
  // Coalesce partial TCP reads into aligned multi-sector SD writes.
  while(received<n&&!aborting()){int part=httpd_req_recv(job.request,reinterpret_cast<char*>(buffer+received),n-received);
   if(part==HTTPD_SOCK_ERR_TIMEOUT&&++timeouts<4)continue;
   if(part<=0){ok=false;std::strcpy(message,"Upload connection interrupted");break;}timeouts=0;received+=part;
  }
  timing.receive_ms+=cyd::hal_uptime_ms()-receive_start;if(!ok||aborting())break;
  UINT written=0;const uint64_t write_start=cyd::hal_uptime_ms();code=f_write(&file,buffer,received,&written);timing.write_ms+=cyd::hal_uptime_ms()-write_start;
  if(code!=FR_OK||written!=received){ok=false;std::snprintf(message,sizeof(message),"SD write failed (%u)",code);break;}
  ok=mbedtls_sha256_update(&hash,buffer,received)==0;done+=received;update(done);
 }
 uint8_t digest[32];char hex[65]{};
 if(ok&&!aborting())ok=mbedtls_sha256_finish(&hash,digest)==0;
 mbedtls_sha256_free(&hash);
 if(ok&&!aborting()){for(unsigned i=0;i<32;++i)std::snprintf(hex+2*i,3,"%02x",digest[i]);if(job.hash[0]&&strcasecmp(hex,job.hash)){ok=false;std::strcpy(message,"Upload SHA-256 mismatch");}}
 if(ok&&!aborting())ok=f_sync(&file)==FR_OK;
 ok=f_close(&file)==FR_OK&&ok;
 if(ok&&!aborting()){code=f_rename(temporary,target);ok=code==FR_OK;if(!ok)std::snprintf(message,sizeof(message),"Cannot publish upload (%u)",code);}
 if(!ok||aborting()){f_unlink(temporary);if(!message[0])std::strcpy(message,aborting()?"Upload cancelled":"Upload could not be saved");error(job.request,"500 Internal Server Error",message);return false;}
 std::strcpy(message,"Upload saved to SD card");httpd_resp_set_status(job.request,"201 Created");auto* result=cJSON_CreateObject();cJSON_AddBoolToObject(result,"saved",true);cJSON_AddNumberToObject(result,"bytes",done);cJSON_AddStringToObject(result,"sha256",hex);json(job.request,result);return true;
}
void transfer_worker(void*){
 Job job{};
 for(;;){if(xQueueReceive(jobs,&job,portMAX_DELAY)!=pdTRUE)continue;char message[96]{};timing={};
  bool ok=job.upload?upload(job,message):download(job,message);
  const auto handle=job.request->handle;const int fd=httpd_req_to_sockfd(job.request);
  (void)handle;xSemaphoreTake(socket_guard,portMAX_DELAY);
  portENTER_CRITICAL(&lock);active_socket=-1;portEXIT_CRITICAL(&lock);
  if(!job.upload||!ok)shutdown(fd,ok?SHUT_WR:SHUT_RDWR);
  httpd_req_async_handler_complete(job.request);xSemaphoreGive(socket_guard);
  refresh_storage();finish(ok,message);std::printf("FS_IO receive_ms=%llu write_ms=%llu read_ms=%llu send_ms=%llu\n",static_cast<unsigned long long>(timing.receive_ms),static_cast<unsigned long long>(timing.write_ms),static_cast<unsigned long long>(timing.read_ms),static_cast<unsigned long long>(timing.send_ms));std::fflush(stdout);
 }
}
esp_err_t start_transfer(httpd_req_t* r,bool uploading,bool head=false){
 if(!access(r))return ESP_FAIL;
 Job job{};job.upload=uploading;job.head=head;
 if(!path_query(r,job.path)||!std::strcmp(job.path,"/files")||!std::strcmp(job.path,"/firmware"))return error(r,"403 Forbidden","Choose a file in Files or Firmware");
 if(!query(r,"id",job.id,sizeof(job.id))){std::snprintf(job.id,sizeof(job.id),"%08lX%08lX",static_cast<unsigned long>(esp_random()),static_cast<unsigned long>(esp_random()));}
 if(!transfer_id(job.id))return error(r,"400 Bad Request","Invalid transfer identifier");
 if(transfer_snapshot().busy)return error(r,"409 Conflict","Another transfer is running");
 char fat[260];fat_path(job.path,fat);FILINFO info{};
 if(uploading){if(r->content_len>0xfffffffeu)return error(r,"413 Content Too Large","FAT32 supports files smaller than 4 GB");job.length=r->content_len;
  uint64_t total=0,free=0;server_storage(total,free);if(job.length>free)return error(r,"507 Insufficient Storage","Not enough free space on the SD card");
  if(f_stat(fat,&info)==FR_OK)return error(r,"409 Conflict","A file with this name already exists");
  if(httpd_req_get_hdr_value_len(r,"X-SHA256")){if(!header(r,"X-SHA256",job.hash,sizeof(job.hash))||std::strlen(job.hash)!=64)return error(r,"400 Bad Request","Invalid SHA-256");for(char* p=job.hash;*p;++p)if(!((*p>='0'&&*p<='9')||(*p>='a'&&*p<='f')||(*p>='A'&&*p<='F')))return error(r,"400 Bad Request","Invalid SHA-256");}
 }else{if(f_stat(fat,&info)!=FR_OK||(info.fattrib&AM_DIR))return error(r,"404 Not Found","File not found");job.size=info.fsize;char range[96]{};job.ranged=httpd_req_get_hdr_value_len(r,"Range")!=0;if((job.ranged&&!header(r,"Range",range,sizeof(range)))||!byte_range(range,job.size,job.offset,job.length))return error(r,"416 Range Not Satisfiable","Invalid byte range");}
 if(httpd_req_async_handler_begin(r,&job.request)!=ESP_OK)return error(r,"503 Service Unavailable","Not enough memory for a transfer");
 portENTER_CRITICAL(&lock);progress={};progress.busy=true;progress.phase=uploading?Phase::upload:Phase::download;progress.total=job.length;progress.started_ms=cyd::hal_uptime_ms();std::strcpy(progress.id,job.id);std::snprintf(progress.name,sizeof(progress.name),"%s",std::strrchr(job.path,'/')+1);cancelled=false;active_socket=httpd_req_to_sockfd(job.request);portEXIT_CRITICAL(&lock);
 if(xQueueSend(jobs,&job,0)!=pdTRUE){error(job.request,"503 Service Unavailable","Transfer queue unavailable");httpd_req_async_handler_complete(job.request);finish(false,"Transfer queue unavailable");}
 return ESP_OK;
}
esp_err_t get_file(httpd_req_t* r){return start_transfer(r,false);}
esp_err_t head_file(httpd_req_t* r){return start_transfer(r,false,true);}
esp_err_t post_file(httpd_req_t* r){return start_transfer(r,true);}
esp_err_t asset(httpd_req_t* r){
 if(!host_ok(r))return error(r,"403 Forbidden","Use the CYD address");
 const uint8_t *begin=html_begin,*end=html_end;const char* type="text/html; charset=utf-8";
 if(!std::strcmp(r->uri,"/pixel-thin.woff2")){begin=thin_begin;end=thin_end;type="font/woff2";}
 else if(!std::strcmp(r->uri,"/pixel-semibold.woff2")){begin=bold_begin;end=bold_end;type="font/woff2";}
 else httpd_resp_set_hdr(r,"Content-Encoding","gzip");
 httpd_resp_set_type(r,type);httpd_resp_set_hdr(r,"Cache-Control",begin==html_begin?"no-cache":"public, max-age=86400");httpd_resp_set_hdr(r,"X-Content-Type-Options","nosniff");
 return httpd_resp_send(r,reinterpret_cast<const char*>(begin),end-begin);
}
bool body(httpd_req_t* r,char* out,size_t capacity){if(r->content_len>=capacity)return false;size_t done=0;while(done<r->content_len){int n=httpd_req_recv(r,out+done,r->content_len-done);if(n<=0)return false;done+=n;}out[done]=0;return true;}
esp_err_t login(httpd_req_t* r){
 if(!host_ok(r)||!origin_ok(r))return error(r,"403 Forbidden","Use the CYD address");
 uint64_t now=cyd::hal_uptime_ms();if(now-login_window>=60000){login_window=now;login_failures=0;}
 if(login_failures>=5)return error(r,"429 Too Many Requests","Wait a minute before trying again");
 char input[40];if(!body(r,input,sizeof(input)))return error(r,"400 Bad Request","Invalid access code");char normalized[9]{};unsigned n=0;
 for(char c:input){if(!c)break;if(c=='-'||c==' ')continue;if(n>=8){n=9;break;}normalized[n++]=(c>='a'&&c<='f')?c-32:c;}
 if(n!=8||std::strcmp(normalized,pin)){++login_failures;return error(r,"401 Unauthorized","Access code did not match");}
 login_failures=0;char cookie[128];std::snprintf(cookie,sizeof(cookie),"cyd=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=86400",session);httpd_resp_set_hdr(r,"Set-Cookie",cookie);auto* result=cJSON_CreateObject();cJSON_AddBoolToObject(result,"ok",true);return json(r,result);
}
esp_err_t status(httpd_req_t* r){if(!access(r))return ESP_FAIL;auto p=transfer_snapshot();auto net=network_snapshot();uint64_t total,free;server_storage(total,free);auto* result=cJSON_CreateObject();
 cJSON_AddStringToObject(result,"phase",phase_name(p.phase));cJSON_AddBoolToObject(result,"busy",p.busy);cJSON_AddStringToObject(result,"id",p.id);cJSON_AddStringToObject(result,"name",p.name);cJSON_AddStringToObject(result,"message",p.message);cJSON_AddNumberToObject(result,"done",p.done);cJSON_AddNumberToObject(result,"total",p.total);
 cJSON_AddNumberToObject(result,"elapsed_ms",(p.busy?cyd::hal_uptime_ms():p.ended_ms)-p.started_ms);cJSON_AddNumberToObject(result,"card_total",total);cJSON_AddNumberToObject(result,"card_free",free);cJSON_AddNumberToObject(result,"heap_kb",cyd::hal_free_heap_kb());cJSON_AddBoolToObject(result,"connected",net.state==NetState::connected);cJSON_AddBoolToObject(result,"setup",net.setup);cJSON_AddStringToObject(result,"ip",net.ip);cJSON_AddStringToObject(result,"ssid",net.ssid);return json(r,result);
}
esp_err_t listing(httpd_req_t* r){if(!access(r))return ESP_FAIL;if(transfer_snapshot().busy)return error(r,"423 Locked","Wait for the current transfer to finish");char path[256];if(!path_query(r,path,true))return error(r,"403 Forbidden","Choose Files or Firmware");
 auto* result=cJSON_CreateObject();auto* array=cJSON_AddArrayToObject(result,"entries");cJSON_AddStringToObject(result,"path",path);unsigned offset=0;char offset_text[16];if(query(r,"offset",offset_text,sizeof(offset_text)))offset=std::strtoul(offset_text,nullptr,10);unsigned shown=0,skipped=0;bool more=false;
 if(!std::strcmp(path,"/")){for(const char* name:{"files","firmware"}){auto* item=cJSON_CreateObject();cJSON_AddStringToObject(item,"name",name);cJSON_AddBoolToObject(item,"directory",true);cJSON_AddNumberToObject(item,"size",0);cJSON_AddItemToArray(array,item);}}
 else{char fat[260];fat_path(path,fat);FF_DIR directory{};auto code=f_opendir(&directory,fat);if(code!=FR_OK){cJSON_Delete(result);return error(r,"404 Not Found","Folder not found");}FILINFO info{};
  while(f_readdir(&directory,&info)==FR_OK&&info.fname[0]){if(info.fname[0]=='.'||(info.fattrib&AM_HID))continue;if(skipped++<offset)continue;if(shown==64){more=true;break;}auto* item=cJSON_CreateObject();cJSON_AddStringToObject(item,"name",info.fname);cJSON_AddBoolToObject(item,"directory",info.fattrib&AM_DIR);cJSON_AddNumberToObject(item,"size",info.fsize);cJSON_AddItemToArray(array,item);++shown;}
  f_closedir(&directory);
 }cJSON_AddBoolToObject(result,"more",more);cJSON_AddNumberToObject(result,"offset",offset);return json(r,result);
}
esp_err_t cancel(httpd_req_t* r){if(!access(r))return ESP_FAIL;char id[25];if(!query(r,"id",id,sizeof(id))||!transfer_id(id))return error(r,"400 Bad Request","Invalid transfer identifier");server_cancel(id);auto* result=cJSON_CreateObject();cJSON_AddBoolToObject(result,"ok",true);return json(r,result);}
esp_err_t folder(httpd_req_t* r){if(!access(r))return ESP_FAIL;if(transfer_snapshot().busy)return error(r,"423 Locked","Wait for the transfer to finish");char path[256];if(!path_query(r,path))return error(r,"403 Forbidden","Invalid shared folder");char fat[260];fat_path(path,fat);auto code=f_mkdir(fat);if(code!=FR_OK)return error(r,"409 Conflict","Cannot create folder; it may already exist");auto* result=cJSON_CreateObject();cJSON_AddBoolToObject(result,"ok",true);return json(r,result);}
esp_err_t networks(httpd_req_t* r){if(!access(r))return ESP_FAIL;auto net=network_snapshot();ScanEntry entries[20];bool scanning=false;unsigned n=network_scan_results(entries,20,scanning);auto* result=cJSON_CreateObject();auto* array=cJSON_AddArrayToObject(result,"networks");
 for(unsigned i=0;i<n;++i){auto* item=cJSON_CreateObject();cJSON_AddStringToObject(item,"ssid",entries[i].ssid);cJSON_AddNumberToObject(item,"rssi",entries[i].rssi);cJSON_AddNumberToObject(item,"auth",entries[i].auth);cJSON_AddItemToArray(array,item);}cJSON_AddBoolToObject(result,"scanning",scanning);cJSON_AddBoolToObject(result,"connected",net.state==NetState::connected);cJSON_AddStringToObject(result,"ip",net.ip);cJSON_AddStringToObject(result,"ssid",net.ssid);return json(r,result);
}
esp_err_t scan(httpd_req_t* r){if(!access(r))return ESP_FAIL;if(transfer_snapshot().busy)return error(r,"423 Locked","Finish the transfer first");network_scan();auto* result=cJSON_CreateObject();cJSON_AddBoolToObject(result,"ok",true);return json(r,result);}
esp_err_t join(httpd_req_t* r){if(!access(r))return ESP_FAIL;if(transfer_snapshot().busy)return error(r,"423 Locked","Finish the transfer first");char text[512];if(!body(r,text,sizeof(text)))return error(r,"400 Bad Request","Invalid network settings");auto* root=cJSON_Parse(text);if(!root)return error(r,"400 Bad Request","Invalid network settings");auto* ssid=cJSON_GetObjectItemCaseSensitive(root,"ssid");auto* password=cJSON_GetObjectItemCaseSensitive(root,"password");bool ok=cJSON_IsString(ssid)&&cJSON_IsString(password)&&network_join(ssid->valuestring,password->valuestring);
 if(cJSON_IsString(password)){std::memset(password->valuestring,0,std::strlen(password->valuestring));}
 cJSON_Delete(root);std::memset(text,0,sizeof(text));if(!ok)return error(r,"400 Bad Request","Check the network name and password length");httpd_resp_set_status(r,"202 Accepted");auto* result=cJSON_CreateObject();cJSON_AddBoolToObject(result,"joining",true);return json(r,result);
}
}
const char* access_code(){if(!pin[0]){const char* digits="2345679";for(unsigned i=0;i<8;++i)pin[i]=digits[esp_random()%7];pin[8]=0;uint8_t random[16];esp_fill_random(random,sizeof(random));for(unsigned i=0;i<16;++i)std::snprintf(session+2*i,3,"%02x",random[i]);}return pin;}
bool server_start(){access_code();f_mkdir("0:/files");f_mkdir("0:/firmware");refresh_storage();
 buffer=static_cast<uint8_t*>(heap_caps_malloc(buffer_size,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL));jobs=xQueueCreate(1,sizeof(Job));socket_guard=xSemaphoreCreateMutex();if(!buffer||!jobs||!socket_guard)return false;
 if(xTaskCreate(transfer_worker,"file_transfer",8192,nullptr,4,&worker)!=pdPASS)return false;
 httpd_config_t config=HTTPD_DEFAULT_CONFIG();config.server_port=server_port;config.stack_size=8192;config.max_uri_handlers=16;config.max_open_sockets=7;config.lru_purge_enable=true;config.recv_wait_timeout=5;config.send_wait_timeout=15;config.keep_alive_enable=true;config.max_resp_headers=8;
 if(httpd_start(&server,&config)!=ESP_OK)return false;
 struct Route {const char* path;httpd_method_t method;esp_err_t(*handler)(httpd_req_t*);};
 const Route routes[]={{"/",HTTP_GET,asset},{"/pixel-thin.woff2",HTTP_GET,asset},{"/pixel-semibold.woff2",HTTP_GET,asset},{"/api/login",HTTP_POST,login},{"/api/status",HTTP_GET,status},{"/api/list",HTTP_GET,listing},{"/api/file",HTTP_GET,get_file},{"/api/file",HTTP_HEAD,head_file},{"/api/file",HTTP_POST,post_file},{"/api/cancel",HTTP_POST,cancel},{"/api/folder",HTTP_POST,folder},{"/api/networks",HTTP_GET,networks},{"/api/scan",HTTP_POST,scan},{"/api/join",HTTP_POST,join}};
 for(const auto& route:routes){httpd_uri_t uri{};uri.uri=route.path;uri.method=route.method;uri.handler=route.handler;if(httpd_register_uri_handler(server,&uri)!=ESP_OK)return false;}
 std::printf("FS_SERVER ready port=%u buffer=%u streaming=1 protected_config=1\n",server_port,static_cast<unsigned>(buffer_size));return true;
}
void server_stop(){server_cancel();if(server){httpd_stop(server);server=nullptr;}if(worker){vTaskDelete(worker);worker=nullptr;}if(jobs){vQueueDelete(jobs);jobs=nullptr;}std::free(buffer);buffer=nullptr;}
void server_cancel(const char* id){if(!socket_guard)return;xSemaphoreTake(socket_guard,portMAX_DELAY);portENTER_CRITICAL(&lock);int fd=-1;if(progress.busy&&(!id||!std::strcmp(id,progress.id))){cancelled=true;fd=active_socket;}portEXIT_CRITICAL(&lock);if(fd>=0)shutdown(fd,SHUT_RDWR);xSemaphoreGive(socket_guard);}
TransferSnapshot transfer_snapshot(){portENTER_CRITICAL(&lock);auto value=progress;portEXIT_CRITICAL(&lock);return value;}
void server_storage(uint64_t& total,uint64_t& free){portENTER_CRITICAL(&lock);total=total_storage;free=free_storage;portEXIT_CRITICAL(&lock);}
}
