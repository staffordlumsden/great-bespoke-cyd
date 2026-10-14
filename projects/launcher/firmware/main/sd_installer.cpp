#include "sd_installer.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_ota_ops.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <algorithm>
#include <cstring>
#ifndef CYD_LIBRARY_DIR
#define CYD_LIBRARY_DIR "/sdcard/firmware"
#endif

namespace cyd {
namespace {
constexpr const char* prefs="cyd_loader";
struct LastAppRecord {uint32_t magic=0x43594431,size=0;uint8_t slot=0,reserved[3]{};uint8_t hash[32]{};};
bool ready=false;
bool fail(char (&error)[64],const char* message){std::snprintf(error,sizeof(error),"%s",message);return false;}
bool sdk_fail(char (&error)[64],const char* stage,esp_err_t code){std::snprintf(error,sizeof(error),"%s: %s",stage,esp_err_to_name(code));return false;}
const esp_partition_t* slot(unsigned number){return esp_partition_find_first(ESP_PARTITION_TYPE_APP,static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_0+number),nullptr);}
bool readback(const esp_partition_t* part,const FirmwareImage& image,char (&error)[64],InstallProgress progress,void* user){
    mbedtls_sha256_context hash;mbedtls_sha256_init(&hash);
    int result=mbedtls_sha256_starts(&hash,0);
    uint8_t buffer[4096],digest[32]{};
    for(uint32_t done=0;!result && done<image.size;){
        const size_t n=std::min<uint32_t>(image.size-done,sizeof(buffer));
        const auto code=esp_partition_read(part,done,buffer,n);
        if(code!=ESP_OK){mbedtls_sha256_free(&hash);return sdk_fail(error,"Readback",code);}
        result=mbedtls_sha256_update(&hash,buffer,n);done+=n;
        if(progress)progress("Verifying flash",done*100/image.size,user);
    }
    if(!result)result=mbedtls_sha256_finish(&hash,digest);
    mbedtls_sha256_free(&hash);
    if(result || std::memcmp(digest,image.file_hash,32)){
        // Image digests identify public firmware, never configuration or credentials.
        std::printf("LOADER readback_mismatch slot=%u size=%lu sha_result=%d expected=",unsigned(part->subtype-ESP_PARTITION_SUBTYPE_APP_OTA_0),static_cast<unsigned long>(image.size),result);
        for(auto byte:image.file_hash)std::printf("%02x",byte);
        std::printf(" actual=");for(auto byte:digest)std::printf("%02x",byte);std::printf("\n");
        return fail(error,"Flash SHA-256 mismatch");
    }
    return true;
}
bool save_slot(const esp_partition_t* part,const FirmwareImage& image,char (&error)[64]){
    nvs_handle_t handle;auto code=nvs_open(prefs,NVS_READWRITE,&handle);
    if(code!=ESP_OK)return sdk_fail(error,"Save app",code);
    LastAppRecord record;record.slot=part->subtype-ESP_PARTITION_SUBTYPE_APP_OTA_0;record.size=image.size;
    std::memcpy(record.hash,image.file_hash,32);
    code=nvs_set_blob(handle,"last_app",&record,sizeof(record));
    if(code==ESP_OK)code=nvs_commit(handle);
    nvs_close(handle);
    return code==ESP_OK || sdk_fail(error,"Save app",code);
}
struct CheckContext {InstallProgress progress;void* user;};
void check_progress(unsigned percent,void* context){auto& c=*static_cast<CheckContext*>(context);if(c.progress)c.progress("Checking SD image",percent,c.user);}
}
bool installer_init(char (&error)[64]){
    error[0]=0;
    const auto* factory=esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_FACTORY,nullptr);
    const auto* current=esp_ota_get_running_partition();
    const auto* first=slot(0);const auto* second=slot(1);
    const auto* data=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_OTA,nullptr);
    if(!factory || current!=factory || factory->address!=0x10000 || factory->size!=0x80000 ||
       !first || first->address!=0x90000 || first->size!=app_slot_size ||
       !second || second->address!=0x240000 || second->size!=app_slot_size ||
       !data || data->address!=0x3f0000 || data->size!=0x2000)return fail(error,"USB layout migration needed");
    // Never erase NVS on an init error: it contains the keyboard bond.
    const auto code=nvs_flash_init();
    if(code!=ESP_OK)return sdk_fail(error,"NVS",code);
    ready=true;return true;
}
bool installed_app(InstalledApp& app){
    app={};if(!ready)return false;
    nvs_handle_t handle;if(nvs_open(prefs,NVS_READONLY,&handle)!=ESP_OK)return false;
    LastAppRecord record;size_t record_size=sizeof(record);
    auto code=nvs_get_blob(handle,"last_app",&record,&record_size);
    nvs_close(handle);
    if(code!=ESP_OK || record_size!=sizeof(record) || record.magic!=0x43594431 || record.slot>1 || !record.size || record.size>app_slot_size)return false;
    app.image.size=record.size;std::memcpy(app.image.file_hash,record.hash,32);
    app.partition=slot(record.slot);esp_app_desc_t desc{};
    if(!app.partition || esp_ota_get_partition_description(app.partition,&desc)!=ESP_OK)return false;
    std::memcpy(app.image.project,desc.project_name,32);app.image.project[31]=0;
    std::memcpy(app.image.version,desc.version,32);app.image.version[31]=0;
    return true;
}
bool boot_installed(char (&error)[64],InstallProgress progress,void* user){
    InstalledApp app;
    if(!installed_app(app))return fail(error,"No installed app yet");
    if(!readback(app.partition,app.image,error,progress,user))return false;
    const auto code=esp_ota_set_boot_partition(app.partition);
    if(code!=ESP_OK)return sdk_fail(error,"Select app",code);
    std::printf("LOADER boot_selected slot=%u size=%lu\n",unsigned(app.partition->subtype-ESP_PARTITION_SUBTYPE_APP_OTA_0),static_cast<unsigned long>(app.image.size));
    return true;
}
namespace {
const esp_partition_t* clock_helper(){if(!ready)return nullptr;for(unsigned i=0;i<2;++i){const auto* part=slot(i);esp_app_desc_t desc{};if(part&&esp_ota_get_partition_description(part,&desc)==ESP_OK&&!std::strncmp(desc.project_name,"CYDFileServer",32)&&std::memchr(desc.version,0,32)&&std::strstr(desc.version,"-clock"))return part;}return nullptr;}
}
bool clock_helper_image(const char* project,const char* version){return project&&version&&!std::strcmp(project,"CYDFileServer")&&std::strstr(version,"-clock");}
int install_destination_slot(bool had_previous,unsigned previous_slot,int helper_slot,bool installing_clock_helper){
    if(!installing_clock_helper&&(helper_slot==0||helper_slot==1))return 1-helper_slot;
    return had_previous?1-static_cast<int>(previous_slot):0;
}
bool clock_helper_available(){return clock_helper()!=nullptr;}
bool boot_clock_helper(char (&error)[64]){const auto* part=clock_helper();if(!part)return fail(error,"Install File Server clock update first");const auto code=esp_ota_set_boot_partition(part);if(code!=ESP_OK)return sdk_fail(error,"Check clock helper",code);std::printf("LOADER clock_helper_selected slot=%u last_app=PRESERVED\n",unsigned(part->subtype-ESP_PARTITION_SUBTYPE_APP_OTA_0));return true;}
bool install_firmware(const char* name,char (&error)[64],InstallProgress progress,void* user){
    error[0]=0;
    if(!ready)return fail(error,"Installer not ready");
    if(!firmware_filename(name))return fail(error,"Invalid firmware filename");
    char path[128];std::snprintf(path,sizeof(path),CYD_LIBRARY_DIR "/%.100s",name);
    FILE* file=std::fopen(path,"rb");if(!file)return fail(error,"Cannot open SD image");
    FirmwareImage image;esp_chip_info_t chip{};esp_chip_info(&chip);
    CheckContext context{progress,user};
    if(!verify_firmware(file,app_slot_size,chip.revision,image,error,check_progress,&context)){std::fclose(file);return false;}
    std::printf("LOADER sd_image project=%s version=%s size=%lu\n",image.project,image.version,static_cast<unsigned long>(image.size));
    InstalledApp previous;
    const bool had_previous=installed_app(previous);
    // A saved digest is only metadata: a USB update can replace the slot.
    // Skip writing only after verifying the actual flash. Install + run must
    // repair a stale/corrupt cached image from the already verified SD file.
    if(had_previous && previous.image.size==image.size && !std::memcmp(previous.image.file_hash,image.file_hash,32)){
        char cached_error[64]{};
        if(readback(previous.partition,image,cached_error,progress,user)){
            std::fclose(file);
            const auto code=esp_ota_set_boot_partition(previous.partition);
            if(code!=ESP_OK)return sdk_fail(error,"Select app",code);
            std::printf("LOADER cached_app=VERIFIED slot=%u\n",unsigned(previous.partition->subtype-ESP_PARTITION_SUBTYPE_APP_OTA_0));
            return true;
        }
        std::printf("LOADER cached_app=STALE reinstall=1\n");
    }
    const auto* helper=clock_helper();
    const int helper_slot=helper?helper->subtype-ESP_PARTITION_SUBTYPE_APP_OTA_0:-1;
    const unsigned previous_slot=had_previous?previous.partition->subtype-ESP_PARTITION_SUBTYPE_APP_OTA_0:0;
    const int target=install_destination_slot(had_previous,previous_slot,helper_slot,clock_helper_image(image.project,image.version));
    const auto* destination=slot(target);
    if(!destination || destination==esp_ota_get_running_partition()){std::fclose(file);return fail(error,"No safe app slot");}
    esp_ota_handle_t update=0;
    if(progress)progress("Preparing app slot",0,user);
    auto code=esp_ota_begin(destination,OTA_WITH_SEQUENTIAL_WRITES,&update);
    if(code!=ESP_OK){std::fclose(file);return sdk_fail(error,"Begin install",code);}
    uint8_t buffer[4096];
    for(uint32_t done=0;done<image.size;){
        const size_t n=std::min<uint32_t>(image.size-done,sizeof(buffer));
        if(std::fread(buffer,1,n,file)!=n){esp_ota_abort(update);std::fclose(file);return fail(error,"SD read failed; app not selected");}
        code=esp_ota_write(update,buffer,n);
        if(code!=ESP_OK){esp_ota_abort(update);std::fclose(file);return sdk_fail(error,"Write app",code);}
        done+=n;if(progress)progress("Installing app",done*100/image.size,user);
    }
    const bool extra=std::fgetc(file)!=EOF || std::ferror(file);std::fclose(file);
    if(extra){esp_ota_abort(update);return fail(error,"SD image changed during install");}
    // Verify bytes against the preflight digest before closing/selecting the image.
    if(!readback(destination,image,error,progress,user)){esp_ota_abort(update);return false;}
    code=esp_ota_end(update);
    if(code!=ESP_OK)return sdk_fail(error,"Validate app",code);
    if(!save_slot(destination,image,error))return false;
    code=esp_ota_set_boot_partition(destination);
    if(code!=ESP_OK)return sdk_fail(error,"Select app",code);
    std::printf("LOADER installed slot=%u size=%lu hash=VERIFIED\n",unsigned(destination->subtype-ESP_PARTITION_SUBTYPE_APP_OTA_0),static_cast<unsigned long>(image.size));
    return true;
}
}
