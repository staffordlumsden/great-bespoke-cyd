#pragma once
#include <cstddef>
#include <cstdint>
namespace fsapp {
bool decode_path(const char* encoded,char* decoded,size_t capacity);
bool shared_path(const char* path,bool allow_root=false);
bool transfer_id(const char* id);
bool byte_range(const char* header,uint32_t size,uint32_t& start,uint32_t& length);
}
