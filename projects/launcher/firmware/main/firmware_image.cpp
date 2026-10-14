#include "firmware_image.h"
#include <algorithm>
#include <cstring>
#include <strings.h>
#include "mbedtls/sha256.h"

namespace cyd {
namespace {
uint16_t u16(const uint8_t* p){return p[0]|uint16_t(p[1])<<8;}
uint32_t u32(const uint8_t* p){return p[0]|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
bool fail(char (&error)[64],const char* why){std::snprintf(error,sizeof(error),"%s",why);return false;}
bool at(FILE* f,uint32_t offset,void* data,size_t length){return std::fseek(f,offset,SEEK_SET)==0 && std::fread(data,1,length,f)==length;}
bool text_field(char* out,const uint8_t* bytes){
    unsigned i=0;
    for(;i<32 && bytes[i];++i){if(bytes[i]<32 || bytes[i]>126)return false;out[i]=char(bytes[i]);}
    if(!i || i==32)return false;
    out[i]=0;return true;
}
struct Hash {
    mbedtls_sha256_context context;
    Hash(){mbedtls_sha256_init(&context);}
    ~Hash(){mbedtls_sha256_free(&context);}
};
}
bool firmware_filename(const char* name){
    const size_t n=std::strlen(name);
    if(n<=4 || n>100 || name[0]=='.' || strcasecmp(name+n-4,".bin"))return false;
    for(size_t i=0;i<n;++i){const unsigned c=static_cast<unsigned char>(name[i]);
        if(!((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') || c==' ' || c=='.' || c=='_' || c=='-'))return false;
    }return true;
}
bool probe_firmware(FILE* f,uint32_t capacity,uint16_t revision,FirmwareImage& info,char (&error)[64]){
    info={};error[0]=0;
    if(!f || std::fseek(f,0,SEEK_END))return fail(error,"Cannot read SD file");
    const long size=std::ftell(f);
    if(size<320)return fail(error,"File is too short");
    if(static_cast<unsigned long>(size)>capacity)return fail(error,"Image exceeds app slot");
    uint8_t header[24],segment[8],desc[256];
    if(!at(f,0,header,sizeof(header)) || !at(f,24,segment,sizeof(segment)) || !at(f,32,desc,sizeof(desc)))return fail(error,"Cannot read SD file");
    if(header[0]!=0xe9 || header[1]<1 || header[1]>16)return fail(error,"Not an ESP application");
    if(u16(header+12)!=0)return fail(error,"Wrong chip: original ESP32 only");
    if((header[3]>>4)>2)return fail(error,"Requires more than 4 MB flash");
    const unsigned minimum=u16(header+15)?u16(header+15):header[14]*100;
    const unsigned maximum=u16(header+17);
    if(revision<minimum || (maximum && maximum!=0xffff && revision>maximum))return fail(error,"Incompatible ESP32 revision");
    if(header[23]!=1)return fail(error,"Image needs appended SHA-256");
    if(u32(segment+4)<256 || u32(desc)!=0xabcd5432)return fail(error,"Use app .bin, not merged image");
    if(!text_field(info.version,desc+16) || !text_field(info.project,desc+48))return fail(error,"Invalid app description");
    if(!std::strcmp(info.project,"cyd_launcher"))return fail(error,"Launcher updates require USB");
    info.size=static_cast<uint32_t>(size);
    return true;
}
bool verify_firmware(FILE* f,uint32_t capacity,uint16_t revision,FirmwareImage& info,char (&error)[64],CheckProgress progress,void* user){
    if(!probe_firmware(f,capacity,revision,info,error))return false;
    uint8_t header[24],segment[8],buffer[4096];
    if(!at(f,0,header,sizeof(header)))return fail(error,"Cannot read SD file");
    uint32_t offset=24;uint8_t checksum=0xef;
    for(unsigned s=0;s<header[1];++s){
        if(offset>info.size-8 || !at(f,offset,segment,sizeof(segment)))return fail(error,"Truncated segment header");
        offset+=8;const uint32_t length=u32(segment+4);
        if(length>info.size-offset || length%4)return fail(error,"Invalid segment length");
        for(uint32_t remaining=length;remaining;){
            const size_t n=std::min<uint32_t>(remaining,sizeof(buffer));
            if(std::fread(buffer,1,n,f)!=n)return fail(error,"SD read failed");
            for(size_t i=0;i<n;++i)checksum^=buffer[i];
            remaining-=n;
        }offset+=length;
    }
    const uint32_t checksum_at=offset|15u;
    if(checksum_at+1+32!=info.size)return fail(error,"Truncated or extra image data");
    uint8_t stored_checksum=0;
    if(!at(f,checksum_at,&stored_checksum,1) || stored_checksum!=checksum)return fail(error,"Image checksum failed");
    const uint32_t hashed_size=checksum_at+1;
    Hash hash;uint8_t digest[32],stored[32];
    if(mbedtls_sha256_starts(&hash.context,0) || std::fseek(f,0,SEEK_SET))return fail(error,"SHA-256 setup failed");
    for(uint32_t done=0;done<hashed_size;){
        const size_t n=std::min<uint32_t>(hashed_size-done,sizeof(buffer));
        if(std::fread(buffer,1,n,f)!=n)return fail(error,"SD read failed");
        if(mbedtls_sha256_update(&hash.context,buffer,n))return fail(error,"SHA-256 failed");
        done+=n;if(progress)progress(done*100/hashed_size,user);
    }
    if(std::fread(stored,1,32,f)!=32 || mbedtls_sha256_finish(&hash.context,digest))return fail(error,"Image hash read failed");
    if(std::memcmp(digest,stored,32))return fail(error,"Image SHA-256 failed");
    // Save a digest of the complete file for flash readback verification.
    if(mbedtls_sha256_starts(&hash.context,0) || std::fseek(f,0,SEEK_SET))return fail(error,"SHA-256 setup failed");
    for(uint32_t done=0;done<info.size;){
        const size_t n=std::min<uint32_t>(info.size-done,sizeof(buffer));
        if(std::fread(buffer,1,n,f)!=n || mbedtls_sha256_update(&hash.context,buffer,n))return fail(error,"SD read failed");
        done+=n;
    }
    if(mbedtls_sha256_finish(&hash.context,info.file_hash) || std::fseek(f,0,SEEK_SET))return fail(error,"SHA-256 failed");
    return true;
}
}
