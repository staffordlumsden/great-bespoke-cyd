#include "sdkconfig.h"
#include "ssh_client.h"
#ifdef CONFIG_CM_M4_TERMINAL
#include "ssh_terminal.h"
#endif
#include "ssh_host_verifier.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include "libssh2.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/dns.h"
#include "lwip/tcpip.h"
namespace cm {
namespace {
struct Event {SshState state;int error;bool auth;unsigned stack;};
struct DnsResult {uint32_t address;int error;};
StaticQueue_t event_control{},request_control{},dns_control{};
uint8_t event_storage[16*sizeof(Event)]{},request_storage[sizeof(TrustProbe)]{},dns_storage[sizeof(DnsResult)]{};
QueueHandle_t events=nullptr,requests=nullptr,dns_results=nullptr;
portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
bool busy=false,cancel=false,overflow=false,dns_pending=false;
unsigned colour_connect=0;
#ifdef CONFIG_CM_M4_TERMINAL
bool cleanup_broken=false;
#endif
char dns_name[254]{};
const AppConfig* host_config=nullptr;unsigned selected_host=0;const HostProfile* profile=nullptr;SshSnapshot view{};
uint64_t millis(){return static_cast<uint64_t>(esp_timer_get_time()/1000);}
bool cancelled(){portENTER_CRITICAL(&lock);bool value=cancel;portEXIT_CRITICAL(&lock);return value;}
void publish(SshState state,int error=0,bool auth=false) {
    Event e{state,error,auth,static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr))};
    if(xQueueSend(events,&e,0)!=pdTRUE){portENTER_CRITICAL(&lock);overflow=true;cancel=true;portEXIT_CRITICAL(&lock);}
    constexpr uint32_t caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
    std::printf("M3 ssh state=%s error=%d authentication_started=%d free_internal=%u minimum_internal=%u largest_internal=%u worker_stack_min=%u values=REDACTED\n",
        ssh_state_name(state),error,auth,static_cast<unsigned>(heap_caps_get_free_size(caps)),
        static_cast<unsigned>(heap_caps_get_minimum_free_size(caps)),static_cast<unsigned>(heap_caps_get_largest_free_block(caps)),e.stack);
}
void dns_reply(const char*,const ip_addr_t* address,void*) {
    DnsResult result{0,ESP_FAIL};
    if(address && IP_IS_V4(address)){result.address=ip4_addr_get_u32(ip_2_ip4(address));result.error=0;}
    portENTER_CRITICAL(&lock);dns_pending=false;portEXIT_CRITICAL(&lock);
    xQueueSend(dns_results,&result,0);
}
void begin_dns(void*) {
    ip_addr_t resolved{};const err_t result=dns_gethostbyname_addrtype(dns_name,&resolved,dns_reply,nullptr,LWIP_DNS_ADDRTYPE_IPV4);
    if(result==ERR_OK)dns_reply(nullptr,&resolved,nullptr);
    else if(result!=ERR_INPROGRESS)dns_reply(nullptr,nullptr,nullptr);
}
bool resolve(uint32_t& address) {
    in_addr literal{};
    if(inet_pton(AF_INET,profile->host,&literal)==1){address=literal.s_addr;return true;}
    portENTER_CRITICAL(&lock);const bool pending=dns_pending;if(!pending)dns_pending=true;portEXIT_CRITICAL(&lock);
    if(pending)return false;
    xQueueReset(dns_results);std::snprintf(dns_name,sizeof(dns_name),"%s",profile->host);
    if(tcpip_callback(begin_dns,nullptr)!=ERR_OK){portENTER_CRITICAL(&lock);dns_pending=false;portEXIT_CRITICAL(&lock);return false;}
    const uint64_t end=millis()+5000;DnsResult result{};
    while(millis()<end && !cancelled())if(xQueueReceive(dns_results,&result,pdMS_TO_TICKS(20))==pdTRUE){address=result.address;return !result.error;}
    // A late callback writes only static storage. No subsequent DNS request
    // reuses the name until that callback has completed.
    return false;
}
bool wait_io(LIBSSH2_SESSION* session,int socket,uint64_t end) {
    if(cancelled() || millis()>=end)return false;
    int direction=session?libssh2_session_block_directions(session):LIBSSH2_SESSION_BLOCK_OUTBOUND;
    fd_set read_set,write_set;FD_ZERO(&read_set);FD_ZERO(&write_set);
    if(!direction || (direction&LIBSSH2_SESSION_BLOCK_INBOUND))FD_SET(socket,&read_set);
    if(!direction || (direction&LIBSSH2_SESSION_BLOCK_OUTBOUND))FD_SET(socket,&write_set);
    timeval timeout{0,20000};
    const int result=select(socket+1,&read_set,&write_set,nullptr,&timeout);
    return result>=0 || errno==EINTR;
}
template<class Operation>int retry(Operation operation,LIBSSH2_SESSION* session,int socket,uint64_t end) {
    for(;;) {
        if(cancelled())return LIBSSH2_ERROR_SOCKET_DISCONNECT;
        const int result=operation();
        if(result!=LIBSSH2_ERROR_EAGAIN)return result;
        if(!wait_io(session,socket,end))return LIBSSH2_ERROR_TIMEOUT;
    }
}
bool load_key(char*& data,size_t& length) {
    char path[160]{};std::snprintf(path,sizeof(path),"/sdcard%s",profile->private_key_file);
    FILE* file=std::fopen(path,"rb");if(!file)return false;
    bool good=std::fseek(file,0,SEEK_END)==0;long size=good?std::ftell(file):-1;
    good=good && size>0 && size<=8192 && std::fseek(file,0,SEEK_SET)==0;
    if(good){data=static_cast<char*>(std::calloc(static_cast<size_t>(size)+1,1));good=data!=nullptr;}
    if(good){length=static_cast<size_t>(size);good=std::fread(data,1,length,file)==length && !std::ferror(file);}
    std::fclose(file);
    if(good)good=length>32 && !std::memcmp(data,"-----BEGIN RSA PRIVATE KEY-----",31);
    return good;
}
void release_key(char*& data,size_t length){if(data){wipe(data,length+1);std::free(data);data=nullptr;}}
size_t session_request=0;
char ssh_log_buffer[384];
void log_library_failure(LIBSSH2_SESSION* session) {
    char* message=nullptr;int length=0;
    const int code=libssh2_session_last_error(session,&message,&length,0);
    const char* reason="LIBRARY_ERROR";
    struct FixedReason {const char* message;const char* label;};
    const FixedReason reasons[]={
        {"Callback returned error","SIGNATURE_CALLBACK_FAILED"},
        {"Callback did not return signature","SIGNATURE_MISSING"},
        {"Invalid public key, too short","PUBLIC_KEY_TOO_SHORT"},
        {"Invalid public key","PUBLIC_KEY_FORMAT"},
        {"Waiting for USERAUTH response","AUTH_RESPONSE_FAILED"},
        {"Waiting for publickey USERAUTH response","SIGNED_AUTH_RESPONSE_FAILED"},
        {"Auth failed","SERVER_REJECTED_AUTH"},
        {"Username/PublicKey combination invalid","SERVER_REJECTED_KEY"},
        {"username/publickey combination invalid","SERVER_REJECTED_KEY"}
    };
    for(const auto& fixed:reasons)if(message && length>=0 &&
       static_cast<size_t>(length)==std::strlen(fixed.message) &&
       !std::memcmp(message,fixed.message,static_cast<size_t>(length)))reason=fixed.label;
    const char* rejected_prefix="Invalid signature for supplied public key, or bad ";
    if(message && length>=static_cast<int>(std::strlen(rejected_prefix)) &&
       !std::memcmp(message,rejected_prefix,std::strlen(rejected_prefix)))reason="SERVER_REJECTED_SIGNED_AUTH";
    std::printf("M3 ssh library_error=%d reason=%s values=REDACTED\n",code,reason);
    std::fflush(stdout);
}
LIBSSH2_ALLOC_FUNC(tracked_alloc) {
    (void)abstract;
    if(!session_request) {
        session_request=count;
        std::printf("M3 ssh allocation=session bytes=%u free_internal=%u largest_internal=%u\n",
            static_cast<unsigned>(count),static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
            static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
    }
    void* value=std::malloc(count);
    if(!value && count)std::printf("M3 ssh allocation=FAILED bytes=%u\n",static_cast<unsigned>(count));
    return value;
}
LIBSSH2_FREE_FUNC(tracked_free) {(void)abstract;std::free(ptr);}
LIBSSH2_REALLOC_FUNC(tracked_realloc) {
    (void)abstract;void* value=std::realloc(ptr,count);
    if(!value && count)std::printf("M3 ssh reallocation=FAILED bytes=%u\n",static_cast<unsigned>(count));
    return value;
}
bool preferences(LIBSSH2_SESSION* session) {
    return !libssh2_session_method_pref(session,LIBSSH2_METHOD_KEX,"ecdh-sha2-nistp256,ecdh-sha2-nistp384,diffie-hellman-group14-sha256") &&
        !libssh2_session_method_pref(session,LIBSSH2_METHOD_HOSTKEY,"ecdsa-sha2-nistp256") &&
        !libssh2_session_method_pref(session,LIBSSH2_METHOD_CRYPT_CS,"aes128-ctr,aes256-ctr") &&
        !libssh2_session_method_pref(session,LIBSSH2_METHOD_CRYPT_SC,"aes128-ctr,aes256-ctr") &&
        !libssh2_session_method_pref(session,LIBSSH2_METHOD_MAC_CS,"hmac-sha2-256-etm@openssh.com,hmac-sha2-256") &&
        !libssh2_session_method_pref(session,LIBSSH2_METHOD_MAC_SC,"hmac-sha2-256-etm@openssh.com,hmac-sha2-256") &&
        !libssh2_session_method_pref(session,LIBSSH2_METHOD_COMP_CS,"none") &&
        !libssh2_session_method_pref(session,LIBSSH2_METHOD_COMP_SC,"none") &&
        !libssh2_session_method_pref(session,LIBSSH2_METHOD_SIGN_ALGO,"rsa-sha2-512,rsa-sha2-256");
}
void session_run(TrustProbe probe) {
    int socket=-1,result=0;LIBSSH2_SESSION* session=nullptr;
    char* key=nullptr;size_t key_size=0;bool authenticated=false,blocked=false,auth_started=false;
    do {
        publish(SshState::resolving);uint32_t address=0;
        if(!resolve(address)){result=ESP_ERR_TIMEOUT;break;}
        publish(SshState::connecting);
        socket=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if(socket<0){result=errno;break;}
        if(fcntl(socket,F_SETFL,O_NONBLOCK)<0){result=errno;break;}
        sockaddr_in target{};target.sin_family=AF_INET;target.sin_port=htons(probe==TrustProbe::handshake_timeout?22222:profile->port);target.sin_addr.s_addr=address;
        result=connect(socket,reinterpret_cast<sockaddr*>(&target),sizeof(target));
        if(result<0 && errno!=EINPROGRESS){result=errno;break;}
        const uint64_t connect_end=millis()+5000;
        while(result<0 && wait_io(nullptr,socket,connect_end)) {
            int error=0;socklen_t size=sizeof(error);
            fd_set set;FD_ZERO(&set);FD_SET(socket,&set);timeval zero{};
            if(select(socket+1,nullptr,&set,nullptr,&zero)>0) {
                if(getsockopt(socket,SOL_SOCKET,SO_ERROR,&error,&size)<0 || error){result=error?error:errno;break;}
                result=0;
            }
        }
        if(result){if(result==-1)result=ESP_ERR_TIMEOUT;break;}
        session_request=0;
        session=libssh2_session_init_ex(tracked_alloc,tracked_free,tracked_realloc,nullptr);
        if(!session){result=ESP_ERR_NO_MEM;break;}
        libssh2_session_set_blocking(session,0);
        if(!preferences(session)){result=ESP_ERR_NOT_SUPPORTED;break;}
        publish(SshState::handshake);
        const uint64_t handshake_start=millis();
        result=retry([&]{return libssh2_session_handshake(session,socket);},session,socket,handshake_start+20000);
        if(probe==TrustProbe::handshake_timeout) {
            std::printf("M3 ssh probe=STALLED_HANDSHAKE elapsed_ms=%u result=%d authentication_started=0\n",
                        static_cast<unsigned>(millis()-handshake_start),result);
            if(!result)result=ESP_ERR_INVALID_STATE; // A diagnostic never authenticates.
        }
        if(result){log_library_failure(session);break;}
        // Only public algorithm identifiers are logged; never library error
        // strings, username, host address, fingerprints or key material.
        std::printf("M3 ssh algorithms kex=%s hostkey=%s cipher_tx=%s cipher_rx=%s mac_tx=%s mac_rx=%s\n",
            libssh2_session_methods(session,LIBSSH2_METHOD_KEX),libssh2_session_methods(session,LIBSSH2_METHOD_HOSTKEY),
            libssh2_session_methods(session,LIBSSH2_METHOD_CRYPT_CS),libssh2_session_methods(session,LIBSSH2_METHOD_CRYPT_SC),
            libssh2_session_methods(session,LIBSSH2_METHOD_MAC_CS),libssh2_session_methods(session,LIBSSH2_METHOD_MAC_SC));
        publish(SshState::verifying);
        size_t raw_size=0;int type=0;const char* raw=libssh2_session_hostkey(session,&raw_size,&type);
        uint8_t expected[32]{};std::memcpy(expected,profile->host_sha256,32);
        if(probe==TrustProbe::changed_pin)expected[0]^=1;
        const auto trust=verify_server_key(reinterpret_cast<const uint8_t*>(raw),raw_size,expected,probe!=TrustProbe::missing_pin);
        wipe(expected,sizeof(expected));
        std::printf("M3 ssh trust=%s authentication_started=0 mode=%u\n",host_trust_name(trust),static_cast<unsigned>(probe));
        if(trust!=HostTrustResult::verified){blocked=true;publish(SshState::trust_blocked,static_cast<int>(trust),false);break;}
        publish(SshState::loading_key);
        if(!load_key(key,key_size)){result=ESP_ERR_INVALID_SIZE;break;}
        auth_started=true;publish(SshState::authenticating,0,true);
        result=retry([&]{return libssh2_userauth_publickey_frommemory(session,profile->username,std::strlen(profile->username),nullptr,0,key,key_size,nullptr);},session,socket,millis()+20000);
        release_key(key,key_size);
        if(result || !libssh2_userauth_authenticated(session)){
            log_library_failure(session);
            if(!result)result=LIBSSH2_ERROR_AUTHENTICATION_FAILED;
            break;
        }
        authenticated=true;publish(SshState::authenticated,0,true);
#ifdef CONFIG_CM_M4_TERMINAL
        result=ssh_terminal_run(session,socket,*profile,publish,cancelled,colour_connect);
#else
        // M3 deliberately stops before PTY/shell/remote OpenClaw. Keep the
        // authenticated transport until deliberate disconnect or Wi-Fi loss.
        while(!cancelled())vTaskDelay(pdMS_TO_TICKS(20));
#endif
    } while(false);
    release_key(key,key_size);
    if(session) {
        retry([&]{return libssh2_session_disconnect_ex(session,SSH_DISCONNECT_BY_APPLICATION,"Contextual Micro disconnect","en");},session,socket,millis()+1000);
        // Release packet/crypto buffers even after a peer has disappeared.
#ifdef CONFIG_CM_M4_TERMINAL
        if(!ssh_terminal_cleanup(session,socket)){portENTER_CRITICAL(&lock);cleanup_broken=true;portEXIT_CRITICAL(&lock);result=ESP_FAIL;}
#else
        libssh2_session_set_blocking(session,1);libssh2_session_free(session);
#endif
    }
    if(socket>=0){shutdown(socket,SHUT_RDWR);close(socket);}
    if(!blocked)publish(authenticated || cancelled()?SshState::disconnected:SshState::error,result,auth_started);
}
void worker(void*) {
    // A persistent worker may wait indefinitely between requests. Keep each
    // project line observable and avoid a per-task dynamic stdio buffer.
    std::setvbuf(stdout,ssh_log_buffer,_IOLBF,sizeof(ssh_log_buffer));
    TrustProbe probe{};
    for(;;)if(xQueueReceive(requests,&probe,portMAX_DELAY)==pdTRUE) {
        session_run(probe);portENTER_CRITICAL(&lock);busy=false;portEXIT_CRITICAL(&lock);
    }
}
}
bool ssh_start(const AppConfig& config) {
    if(events)return false;
    if(!config.host_count){view.state=SshState::error;view.error=ESP_ERR_NOT_FOUND;return false;}
    host_config=&config;selected_host=0;profile=&config.hosts[0];
    events=xQueueCreateStatic(16,sizeof(Event),event_storage,&event_control);
    requests=xQueueCreateStatic(1,sizeof(TrustProbe),request_storage,&request_control);
    dns_results=xQueueCreateStatic(1,sizeof(DnsResult),dns_storage,&dns_control);
    const int result=libssh2_init(0);if(result){view.state=SshState::error;view.error=result;return false;}
    if(xTaskCreatePinnedToCore(worker,"cm_ssh",8192,nullptr,3,nullptr,1)!=pdPASS){view.state=SshState::error;view.error=ESP_ERR_NO_MEM;return false;}
    view.state=SshState::idle;return true;
}
static void ssh_connect_mode(TrustProbe probe,unsigned colours) {
    if(!requests || !profile)return;
#ifdef CONFIG_CM_M4_TERMINAL
    portENTER_CRITICAL(&lock);const bool broken=cleanup_broken;portEXIT_CRITICAL(&lock);
    if(broken){view.state=SshState::error;view.error=ESP_ERR_INVALID_STATE;return;}
#endif
    portENTER_CRITICAL(&lock);const bool active=busy;if(!active){busy=true;cancel=false;colour_connect=colours;}portEXIT_CRITICAL(&lock);
    if(active)return;
    view.authentication_started=false;view.error=0;
    if(xQueueSend(requests,&probe,0)!=pdTRUE){portENTER_CRITICAL(&lock);busy=false;portEXIT_CRITICAL(&lock);view.state=SshState::error;view.error=ESP_ERR_NO_MEM;}
}
void ssh_connect(TrustProbe probe){
#ifdef CONFIG_CM_M5_STARTUP
 ssh_connect_mode(probe,3);
#else
 ssh_connect_mode(probe,0);
#endif
}
bool ssh_select_host(unsigned index){
    portENTER_CRITICAL(&lock);
    const bool allowed=host_config && index<host_config->host_count && !busy;
    if(allowed){selected_host=index;profile=&host_config->hosts[index];}
    portEXIT_CRITICAL(&lock);
    if(allowed){view.state=SshState::idle;view.error=0;view.authentication_started=false;std::printf("M6 host selected_index=%u values=REDACTED\n",index);}
    return allowed;
}
unsigned ssh_selected_host(){portENTER_CRITICAL(&lock);const unsigned index=selected_host;portEXIT_CRITICAL(&lock);return index;}
bool ssh_shell(){if(ssh_active())return false;ssh_connect_mode(TrustProbe::normal,0);return ssh_active();}
void ssh_disconnect(){portENTER_CRITICAL(&lock);cancel=true;portEXIT_CRITICAL(&lock);}
bool ssh_active(){portENTER_CRITICAL(&lock);const bool value=busy;portEXIT_CRITICAL(&lock);return value;}
void ssh_poll() {
    if(!events)return;
    Event e{};while(xQueueReceive(events,&e,0)==pdTRUE)view={e.state,e.error,e.auth,e.stack};
    portENTER_CRITICAL(&lock);bool lost=overflow;overflow=false;portEXIT_CRITICAL(&lock);
    if(lost){view.state=SshState::error;view.error=ESP_ERR_NO_MEM;}
}
bool ssh_input(const char* bytes,size_t size){
#ifdef CONFIG_CM_M4_TERMINAL
    return view.state==SshState::terminal && ssh_terminal_input(bytes,size);
#else
    (void)bytes;(void)size;return false;
#endif
}
SshSnapshot ssh_snapshot(){return view;}
bool ssh_diagnostic(bool scroll){
#ifdef CONFIG_CM_M4_TERMINAL
 if(ssh_active())return view.state==SshState::terminal && ssh_terminal_diagnostic(scroll);
 ssh_connect_mode(TrustProbe::normal,scroll?2:1);return ssh_active();
#else
 return false;
#endif
}
const char* ssh_state_name(SshState state) {
    switch(state) {
        case SshState::off:return "OFF";case SshState::idle:return "READY";
        case SshState::resolving:return "RESOLVING";case SshState::connecting:return "CONNECTING";
        case SshState::handshake:return "HANDSHAKE";case SshState::verifying:return "VERIFYING HOST";
        case SshState::loading_key:return "LOADING KEY";case SshState::authenticating:return "AUTHENTICATING";
        case SshState::pty:return "REQUESTING PTY";case SshState::shell:return "STARTING SHELL";case SshState::terminal:return "TERMINAL";
        case SshState::authenticated:return "AUTHENTICATED";case SshState::trust_blocked:return "HOST KEY BLOCKED";
        case SshState::disconnected:return "DISCONNECTED";case SshState::error:return "ERROR";
    }return "ERROR";
}
}
