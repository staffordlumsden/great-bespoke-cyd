#include "terminal_bootstrap.h"
#include "ssh_terminal.h"
#include "terminal_renderer.h"
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <sys/socket.h>
#include <sys/select.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "terminal_diagnostic.h"
namespace cm {
namespace {
struct Input {uint8_t size;char data[64];};
StaticQueue_t queue_control{};uint8_t queue_storage[8*sizeof(Input)]{};
QueueHandle_t input_queue=nullptr;std::atomic<bool> active{false};
portMUX_TYPE input_gate=portMUX_INITIALIZER_UNLOCKED;
std::atomic<unsigned> generation{0};
std::atomic<unsigned> rejected{0};unsigned sent=0;
std::atomic<unsigned> diagnostic_requested{0};
std::atomic<bool> diagnostic_display{false};
void zero(void* data,size_t size){auto* bytes=static_cast<volatile uint8_t*>(data);for(size_t i=0;i<size;++i)bytes[i]=0;}
void deactivate(){portENTER_CRITICAL(&input_gate);active=false;++generation;portEXIT_CRITICAL(&input_gate);if(input_queue)xQueueReset(input_queue);zero(queue_storage,sizeof(queue_storage));}
uint64_t now(){return static_cast<uint64_t>(esp_timer_get_time()/1000);}
bool wait_io(LIBSSH2_SESSION* session,int socket,uint64_t end,bool(*cancel)()){
    if(cancel() || now()>=end)return false;
    const int direction=libssh2_session_block_directions(session);
    fd_set readable,writable;FD_ZERO(&readable);FD_ZERO(&writable);
    if(!direction || (direction&LIBSSH2_SESSION_BLOCK_INBOUND))FD_SET(socket,&readable);
    if(!direction || (direction&LIBSSH2_SESSION_BLOCK_OUTBOUND))FD_SET(socket,&writable);
    timeval timeout{0,20000};const int result=select(socket+1,&readable,&writable,nullptr,&timeout);
    return result>=0 || errno==EINTR;
}
template<class Operation>int retry(Operation operation,LIBSSH2_SESSION* session,int socket,uint64_t end,bool(*cancel)()){
    for(;;){if(cancel())return LIBSSH2_ERROR_SOCKET_DISCONNECT;const int result=operation();if(result!=LIBSSH2_ERROR_EAGAIN)return result;if(!wait_io(session,socket,end,cancel))return LIBSSH2_ERROR_TIMEOUT;}
}
void reply(void*,const char* bytes,size_t size){ssh_terminal_input(bytes,size);}
int run_colour_diagnostic(LIBSSH2_SESSION* session,int socket,bool(*cancel)(),bool scroll){
 LIBSSH2_CHANNEL* probe=nullptr;const uint64_t end=now()+(scroll?30000:5000);
 int result=retry([&]{probe=libssh2_channel_open_ex(session,"session",7,8192,4096,nullptr,0);return probe?0:libssh2_session_last_errno(session);},session,socket,end,cancel);
 if(!result && probe)result=retry([&]{return libssh2_channel_exec(probe,scroll?terminal_scroll_command:terminal_colour_command);},session,socket,end,cancel);
 if(!result && !terminal_reset())result=ESP_FAIL;
 unsigned total=0;char bytes[512]{};
 while(!result && probe && !libssh2_channel_eof(probe)){
  if(cancel()){result=LIBSSH2_ERROR_SOCKET_DISCONNECT;break;}
  const auto count=libssh2_channel_read(probe,bytes,sizeof(bytes));
  if(count>0){total+=static_cast<unsigned>(count);if(total>2048 || !terminal_feed(reinterpret_cast<const uint8_t*>(bytes),static_cast<size_t>(count)))result=ESP_FAIL;zero(bytes,sizeof(bytes));}
  else if(count<0 && count!=LIBSSH2_ERROR_EAGAIN)result=static_cast<int>(count);
  else if(!libssh2_channel_eof(probe) && !wait_io(session,socket,end,cancel))result=LIBSSH2_ERROR_TIMEOUT;
 }
 zero(bytes,sizeof(bytes));
 if(!result && !scroll && !terminal_check_colours())result=ESP_FAIL;
 if(probe){const int released=retry([&]{return libssh2_channel_free(probe);},session,socket,now()+1000,cancel);if(!result)result=released;}
 std::printf("M4 diagnostic channel=ISOLATED_EXEC error=%d bytes=%u input=DISABLED values=PUBLIC_FIXTURE\n",result,total);
 return result;
}
}
bool ssh_terminal_diagnostic(bool scroll){
 portENTER_CRITICAL(&input_gate);const bool ready=active.load() || diagnostic_display.load();if(ready){active=false;++generation;}portEXIT_CRITICAL(&input_gate);
 if(!ready)return false;
 if(input_queue)xQueueReset(input_queue);
 zero(queue_storage,sizeof(queue_storage));diagnostic_requested=scroll?2:1;return true;
}
bool ssh_terminal_input(const char* bytes,size_t size){
    const unsigned epoch=generation.load();
    if(!active.load() || !bytes || !size || size>512){++rejected;return false;}
    Input inputs[8]{};const unsigned count=static_cast<unsigned>((size+63)/64);
    for(unsigned i=0;i<count;++i){const size_t offset=i*64,part=std::min(size-offset,size_t{64});inputs[i].size=static_cast<uint8_t>(part);std::memcpy(inputs[i].data,bytes+offset,part);}
    if(size==3 && inputs[0].data[0]==27 && inputs[0].data[1]=='[' && std::strchr("ABCDHF",inputs[0].data[2]) && terminal_application_cursor())inputs[0].data[1]='O';
    portENTER_CRITICAL(&input_gate);
    bool queued=active.load() && generation.load()==epoch && uxQueueSpacesAvailable(input_queue)>=count;
    if(queued)for(unsigned i=0;i<count;++i)queued=xQueueSend(input_queue,&inputs[i],0)==pdTRUE && queued;
    portEXIT_CRITICAL(&input_gate);
    zero(inputs,sizeof(inputs));if(queued)return true;
    ++rejected;return false;
}
int ssh_terminal_run(LIBSSH2_SESSION* session,int socket,const HostProfile& profile,void(*publish)(SshState,int,bool),bool(*cancel)(),unsigned diagnostic_mode){
    if(!input_queue)input_queue=xQueueCreateStatic(8,sizeof(Input),queue_storage,&queue_control);
    xQueueReset(input_queue);sent=0;rejected=0;diagnostic_requested=0;diagnostic_display=false;
    if(!terminal_begin(reply,nullptr))return ESP_ERR_NO_MEM;
    if(diagnostic_mode==1 || diagnostic_mode==2){
        deactivate();diagnostic_display=true;publish(SshState::terminal,0,true);
        const int result=run_colour_diagnostic(session,socket,cancel,diagnostic_mode==2);if(result)return result;
        uint64_t metrics_due=now()+5000;
        while(!cancel()){if(const unsigned requested=diagnostic_requested.exchange(0)){const int result=run_colour_diagnostic(session,socket,cancel,requested==2);if(result)return result;}if(now()>=metrics_due){terminal_metrics();metrics_due=now()+5000;}vTaskDelay(pdMS_TO_TICKS(20));}
        return LIBSSH2_ERROR_SOCKET_DISCONNECT;
    }
    LIBSSH2_CHANNEL* channel=nullptr;publish(SshState::pty,0,true);
    int result=retry([&]{channel=libssh2_channel_open_ex(session,"session",7,8192,4096,nullptr,0);return channel?0:libssh2_session_last_errno(session);},session,socket,now()+10000,cancel);
    if(result || !channel)return result?result:LIBSSH2_ERROR_CHANNEL_FAILURE;
    const auto geometry=terminal_geometry();
    result=retry([&]{return libssh2_channel_request_pty_ex(channel,profile.terminal_type,std::strlen(profile.terminal_type),nullptr,0,geometry.columns,geometry.rows,geometry.columns*geometry.font_width,geometry.rows*geometry.font_height);},session,socket,now()+10000,cancel);
    if(result)return result;
    std::printf("M4 pty columns=%u rows=%u pixel_width=%u pixel_height=%u window=8192 packet=4096 values=REDACTED\n",geometry.columns,geometry.rows,geometry.columns*geometry.font_width,geometry.rows*geometry.font_height);
    publish(SshState::shell,0,true);
#ifdef CONFIG_CM_M5_STARTUP
    const bool startup=diagnostic_mode==3 && profile.startup_command[0];
#else
    constexpr bool startup=false;
#endif
    if(!startup){
        result=retry([&]{return libssh2_channel_shell(channel);},session,socket,now()+10000,cancel);
        if(result)return result;
    }
    char alias[13]{};unsigned alias_size=0;
    for(const unsigned char* p=reinterpret_cast<const unsigned char*>(profile.label);*p && alias_size<12;++p)if((*p>='A' && *p<='Z') || (*p>='a' && *p<='z') || (*p>='0' && *p<='9') || *p=='-')alias[alias_size++]=static_cast<char>(*p);
    char lower[13]{};for(unsigned i=0;i<alias_size;++i)lower[i]=alias[i]>='A' && alias[i]<='Z'?alias[i]-'A'+'a':alias[i];
    if(std::strstr(lower,"mac"))std::strcpy(alias,"MacMini");else if(!alias_size)std::strcpy(alias,"SSH");
#ifdef CONFIG_CM_M5_STARTUP
    if(startup){
        char startup_request[2048]{};
        const size_t size=terminal_startup_command(startup_request,sizeof(startup_request),alias,profile.startup_command);
        if(!size){zero(startup_request,sizeof(startup_request));std::printf("M5 startup rejected=INVALID_COMMAND command=REDACTED\n");return ESP_ERR_INVALID_ARG;}
        result=retry([&]{return libssh2_channel_exec(channel,startup_request);},session,socket,now()+10000,cancel);
        zero(startup_request,sizeof(startup_request));
        if(result)return result;
        std::printf("M5 startup request=ISOLATED_PTY_EXEC bytes=%u command=REDACTED\n",static_cast<unsigned>(size));
    }else
#endif
    {
    char bootstrap[512]{};
    const int bootstrap_size=std::snprintf(bootstrap,sizeof(bootstrap),
        "\x15" R"CM(export COLORTERM=truecolor FORCE_COLOR=3 OPENCLAW_THEME=dark; printf '\033[2J\033[H'; case "${SHELL##*/}" in zsh) exec env PS1="$(printf '%%%%{\033[97m%%%%}%s> %%%%{\033[92m%%%%}')" RPROMPT= RPS1= "$SHELL" -f;; bash) exec env PS1='\[\e[97m\]%s> \[\e[92m\]' "$SHELL" --noprofile --norc;; *) PS1="$(printf '\033[97m%s> \033[92m')"; export PS1; exec "${SHELL:-/bin/sh}";; esac)CM" "\r",alias,alias,alias);
    if(bootstrap_size<=0 || static_cast<size_t>(bootstrap_size)>=sizeof(bootstrap)){zero(bootstrap,sizeof(bootstrap));return ESP_ERR_INVALID_SIZE;}
    size_t bootstrap_offset=0;const uint64_t bootstrap_end=now()+10000;
    while(bootstrap_offset<static_cast<size_t>(bootstrap_size)){
        const auto written=libssh2_channel_write(channel,bootstrap+bootstrap_offset,bootstrap_size-bootstrap_offset);
        if(written>0)bootstrap_offset+=static_cast<size_t>(written);
        else if(written!=LIBSSH2_ERROR_EAGAIN){zero(bootstrap,sizeof(bootstrap));return written?static_cast<int>(written):LIBSSH2_ERROR_CHANNEL_CLOSED;}
        else if(!wait_io(session,socket,bootstrap_end,cancel)){zero(bootstrap,sizeof(bootstrap));return LIBSSH2_ERROR_TIMEOUT;}
    }
    zero(bootstrap,sizeof(bootstrap));std::printf("M4 prompt=SHORT_SESSION colour=WHITE shell_profile=INHERITED_ENV values=REDACTED\n");
    }
    auto remote_geometry=geometry;
    portENTER_CRITICAL(&input_gate);active=true;portEXIT_CRITICAL(&input_gate);publish(SshState::terminal,0,true);
    Input pending{};unsigned offset=0;uint64_t write_end=0,metrics_due=now()+5000;
    bool diagnostic_hold=false;
    while(!cancel()){
        if(const unsigned requested=diagnostic_requested.exchange(0)){zero(&pending,sizeof(pending));offset=0;result=run_colour_diagnostic(session,socket,cancel,requested==2);if(result)break;diagnostic_hold=true;diagnostic_display=true;}
        const auto current_geometry=terminal_geometry();
        if(!diagnostic_hold && (current_geometry.columns!=remote_geometry.columns || current_geometry.rows!=remote_geometry.rows)){
            result=retry([&]{return libssh2_channel_request_pty_size_ex(channel,current_geometry.columns,current_geometry.rows,current_geometry.columns*current_geometry.font_width,current_geometry.rows*current_geometry.font_height);},session,socket,now()+5000,cancel);
            if(result)break;
            remote_geometry=current_geometry;std::printf("M4 pty resize columns=%u rows=%u error=0 values=REDACTED\n",remote_geometry.columns,remote_geometry.rows);
        }
        bool progress=false;
        if(!diagnostic_hold && !pending.size && xQueueReceive(input_queue,&pending,0)==pdTRUE){offset=0;write_end=now()+5000;}
        if(pending.size){
            const auto written=libssh2_channel_write(channel,pending.data+offset,pending.size-offset);
            if(written>0){offset+=static_cast<unsigned>(written);sent+=static_cast<unsigned>(written);progress=true;if(offset==pending.size)zero(&pending,sizeof(pending));}
            else if(written<0 && written!=LIBSSH2_ERROR_EAGAIN){result=static_cast<int>(written);break;}
            if(pending.size && now()>=write_end){result=LIBSSH2_ERROR_TIMEOUT;break;}
        }
        char bytes[512];unsigned received=0;
        for(int stream=0;stream<2;++stream){
            const auto read=libssh2_channel_read_ex(channel,stream,bytes,sizeof(bytes));
            if(read>0){if(!diagnostic_hold && !terminal_feed(reinterpret_cast<uint8_t*>(bytes),static_cast<size_t>(read))){result=ESP_FAIL;break;}received+=static_cast<unsigned>(read);progress=true;}
            else if(read<0 && read!=LIBSSH2_ERROR_EAGAIN){result=static_cast<int>(read);break;}
        }
        zero(bytes,sizeof(bytes));if(result)break;
        if(!received && libssh2_channel_eof(channel)){result=0;break;}
        if(now()>=metrics_due){terminal_metrics();std::printf("M4 input sent_bytes=%u rejected=%u values=REDACTED\n",sent,rejected.load());metrics_due=now()+5000;}
        if(progress)vTaskDelay(pdMS_TO_TICKS(1));else if(!wait_io(session,socket,now()+1000,cancel)){result=LIBSSH2_ERROR_SOCKET_DISCONNECT;break;}
    }
    zero(&pending,sizeof(pending));deactivate();return result;
}
bool ssh_terminal_cleanup(LIBSSH2_SESSION* session,int socket){
    deactivate();diagnostic_display=false;diagnostic_requested=0;const bool cleared=terminal_end();
    // Shut the socket down before releasing libssh2: close/free cannot wait
    // indefinitely for acknowledgements from a lost peer. No new auth occurs.
    if(socket>=0)shutdown(socket,SHUT_RDWR);
    libssh2_session_set_blocking(session,0);const uint64_t end=now()+1000;int result;
    do{result=libssh2_session_free(session);if(result==LIBSSH2_ERROR_EAGAIN)vTaskDelay(pdMS_TO_TICKS(1));}while(result==LIBSSH2_ERROR_EAGAIN && now()<end);
    std::printf("M4 cleanup=%s error=%d terminal_released=%d reload_required=%d values=REDACTED\n",result || !cleared?"FAILED":"COMPLETE",result,cleared,result!=0 || !cleared);
    return result==0 && cleared;
}
}
