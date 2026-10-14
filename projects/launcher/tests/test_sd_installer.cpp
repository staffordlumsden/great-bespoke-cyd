#include "sd_installer.h"
#include "esp_ota_ops.h"
#include "nvs.h"
#include "mbedtls/sha256.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include <filesystem>

namespace {
esp_partition_t partitions[]={{0,ESP_PARTITION_SUBTYPE_APP_FACTORY,0x10000,0x80000},
{0,ESP_PARTITION_SUBTYPE_APP_OTA_0,0x90000,0x1b0000},
{0,static_cast<esp_partition_subtype_t>(17),0x240000,0x1b0000},
{1,ESP_PARTITION_SUBTYPE_DATA_OTA,0x3f0000,0x2000}};
std::vector<uint8_t> flash[4],record,uncommitted;
const esp_partition_t* updating=nullptr;const esp_partition_t* boot=&partitions[0];
unsigned begins=0,writes=0,aborts=0,ends=0,selections=0;
enum class Failure {none,begin,write,readback,end,save,select,short_read};
Failure failure=Failure::none;
unsigned index(const esp_partition_t* part){return part-partitions;}
void put(const std::filesystem::path& path,const std::vector<uint8_t>& data){std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(data.data()),data.size());}
std::vector<uint8_t> get(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
bool verify(const std::vector<uint8_t>& bytes){FILE* f=std::tmpfile();assert(f);assert(std::fwrite(bytes.data(),1,bytes.size(),f)==bytes.size());cyd::FirmwareImage info;char error[64];bool result=cyd::verify_firmware(f,cyd::app_slot_size,301,info,error);std::fclose(f);return result;}
void rehash(std::vector<uint8_t>& image){
    size_t offset=24;uint8_t checksum=0xef;
    for(unsigned segment=0;segment<image[1];++segment){
        const auto* p=image.data()+offset+4;const uint32_t size=p[0]|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;
        offset+=8;for(uint32_t i=0;i<size;++i)checksum^=image[offset+i];offset+=size;
    }
    assert((offset|15u)+33==image.size());image[offset|15u]=checksum;
    mbedtls_sha256_context hash;mbedtls_sha256_init(&hash);assert(!mbedtls_sha256_starts(&hash,0));assert(!mbedtls_sha256_update(&hash,image.data(),image.size()-32));assert(!mbedtls_sha256_finish(&hash,image.data()+image.size()-32));
}
}
const esp_partition_t* esp_partition_find_first(int type,esp_partition_subtype_t sub,const char*){for(auto& p:partitions)if(p.type==type && p.subtype==sub)return &p;return nullptr;}
const esp_partition_t* esp_ota_get_running_partition(){return &partitions[0];}
esp_err_t esp_ota_get_partition_description(const esp_partition_t* part,esp_app_desc_t* desc){const auto& data=flash[index(part)];if(data.size()<32+sizeof(*desc))return ESP_FAIL;std::memcpy(desc,data.data()+32,sizeof(*desc));return ESP_OK;}
esp_err_t esp_partition_read(const esp_partition_t* part,size_t off,void* out,size_t n){if(failure==Failure::readback)return ESP_FAIL;const auto& data=flash[index(part)];if(off+n>data.size())return ESP_FAIL;std::memcpy(out,data.data()+off,n);return ESP_OK;}
esp_err_t esp_ota_begin(const esp_partition_t* part,size_t,esp_ota_handle_t* handle){++begins;assert(part!=&partitions[0]);if(failure==Failure::begin)return ESP_FAIL;updating=part;flash[index(part)].clear();*handle=1;if(failure==Failure::short_read)std::filesystem::resize_file("firmware/test.bin",100);return ESP_OK;}
esp_err_t esp_ota_write(esp_ota_handle_t handle,const void* b,size_t n){assert(handle==1 && updating);++writes;if(failure==Failure::write)return ESP_FAIL;const auto* bytes=static_cast<const uint8_t*>(b);auto& data=flash[index(updating)];data.insert(data.end(),bytes,bytes+n);return ESP_OK;}
esp_err_t esp_ota_abort(esp_ota_handle_t){++aborts;updating=nullptr;return ESP_OK;}
esp_err_t esp_ota_end(esp_ota_handle_t){++ends;assert(updating);updating=nullptr;return failure==Failure::end?ESP_FAIL:ESP_OK;}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t* part){++selections;if(failure==Failure::select)return ESP_FAIL;boot=part;return ESP_OK;}
esp_err_t nvs_flash_init(){return ESP_OK;}
esp_err_t nvs_open(const char* name,int,nvs_handle_t* handle){assert(!std::strcmp(name,"cyd_loader"));*handle=1;return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t,const char*,const void* b,size_t n){const auto* bytes=static_cast<const uint8_t*>(b);uncommitted.assign(bytes,bytes+n);return ESP_OK;}
esp_err_t nvs_get_blob(nvs_handle_t,const char*,void* out,size_t* n){if(record.empty())return ESP_FAIL;assert(*n>=record.size());std::memcpy(out,record.data(),record.size());*n=record.size();return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t){if(failure==Failure::save)return ESP_FAIL;record=uncommitted;return ESP_OK;}
void nvs_close(nvs_handle_t){}
int main(int argc,char** argv){
    assert(argc==2);auto image=get(argv[1]);assert(!image.empty());assert(verify(image));
    assert(cyd::clock_helper_image("CYDFileServer","0.1.2-clock"));
    assert(!cyd::clock_helper_image("CYDFileServer","0.1.1-clearcode"));
    assert(!cyd::clock_helper_image("TerminalMicro","0.1.0"));
    assert(cyd::install_destination_slot(false,0,-1,false)==0);
    assert(cyd::install_destination_slot(true,0,-1,false)==1);
    assert(cyd::install_destination_slot(true,1,-1,false)==0);
    assert(cyd::install_destination_slot(true,0,1,false)==0);
    assert(cyd::install_destination_slot(true,1,0,false)==1);
    assert(cyd::install_destination_slot(true,0,1,true)==1);
    for(const char* name:{"CM-0.5.5.bin","example.BIN","My App.bin"})assert(cyd::firmware_filename(name));
    for(const char* name:{"../config.json","/absolute.bin","._secret.bin","app.bin/../key","bad\n.bin","bad$.bin",".bin"})assert(!cyd::firmware_filename(name));
    for(size_t n:{0u,23u,31u,255u,319u,1024u}){auto bad=image;bad.resize(n);assert(!verify(bad));}
    for(size_t off:{0u,1u,12u,15u,23u,24u,28u,32u,400u}){auto bad=image;bad[off]^=0xff;assert(!verify(bad));}
    {auto bad=image;bad.back()^=1;assert(!verify(bad));}
    {auto bad=image;bad.push_back(0);assert(!verify(bad));}
    {auto bad=image;bad.resize(cyd::app_slot_size+1);assert(!verify(bad));}
    {auto bad=image;bad[12]=9;rehash(bad);assert(!verify(bad));} // ESP32-S3 is not this board.
    {auto bad=image;bad[15]=0x90;bad[16]=1;rehash(bad);assert(!verify(bad));} // Requires rev 4.
    std::filesystem::create_directory("firmware");char error[64];assert(cyd::installer_init(error));
    // Invalid data must be rejected before any flash begins.
    auto bad=image;bad[400]^=1;put("firmware/test.bin",bad);assert(!cyd::install_firmware("test.bin",error,nullptr));assert(begins==0 && selections==0);
    for(auto mode:{Failure::begin,Failure::write,Failure::readback,Failure::end,Failure::save,Failure::short_read}){
        put("firmware/test.bin",image);failure=mode;boot=&partitions[0];record.clear();
        const auto selected_before=selections;
        assert(!cyd::install_firmware("test.bin",error,nullptr));assert(selections==selected_before && boot==&partitions[0]);
    }
    failure=Failure::none;put("firmware/test.bin",image);record.clear();assert(cyd::install_firmware("test.bin",error,nullptr));assert(boot==&partitions[1]);assert(flash[1]==image);
    cyd::InstalledApp installed;assert(cyd::installed_app(installed));assert(installed.partition==&partitions[1]);
    const auto count=begins;assert(cyd::install_firmware("test.bin",error,nullptr));assert(begins==count); // Same app runs without rewriting flash.
    auto helper=image;std::memset(helper.data()+48,0,64);std::memcpy(helper.data()+48,"0.1.2-clock",11);std::memcpy(helper.data()+80,"CYDFileServer",13);rehash(helper);assert(verify(helper));flash[2]=helper;
    assert(cyd::clock_helper_available());const auto record_before=record;assert(cyd::boot_clock_helper(error));assert(boot==&partitions[2]&&record==record_before);boot=&partitions[0];
    auto other=image;other[48]='9';rehash(other);assert(verify(other));put("firmware/test.bin",other);
    assert(cyd::install_firmware("test.bin",error,nullptr));assert(boot==&partitions[1]);assert(flash[1]==other && flash[2]==helper);
    const auto selected_before=selections;flash[1][400]^=1;assert(!cyd::boot_installed(error,nullptr));assert(selections==selected_before);
    // Install + run repairs stale metadata/flash left by a direct USB update,
    // whereas Run last above still blocks corrupt bytes. Keep the helper.
    const auto repair_begins=begins;
    assert(cyd::install_firmware("test.bin",error,nullptr));
    assert(begins==repair_begins+1 && flash[1]==other && flash[2]==helper && boot==&partitions[1]);
    assert(aborts>=2 && ends>=3 && flash[0].empty());
    std::printf("PASS: real image, malformed/truncated/corrupt/wrong-chip images, path rejection, injected IO/flash/NVS failures, unchanged boot on failure, clock-helper slot retention, readback integrity, no rewrite on same app; factory never written\n");
}
