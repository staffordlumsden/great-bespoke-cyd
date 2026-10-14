#include "file_policy.h"
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <cerrno>
namespace fsapp {
namespace {int hex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}}
bool canonical_utf8(const unsigned char* p){while(*p){unsigned first=*p++;if(first<128)continue;unsigned n=0,value=0,minimum=0;if(first>=0xc2&&first<=0xdf){n=1;value=first&31;minimum=128;}else if(first>=0xe0&&first<=0xef){n=2;value=first&15;minimum=2048;}else if(first>=0xf0&&first<=0xf4){n=3;value=first&7;minimum=65536;}else return false;while(n--){if((*p&0xc0)!=0x80)return false;value=(value<<6)|(*p++&63);}if(value<minimum||value>0x10ffff||(value>=0xd800&&value<=0xdfff))return false;}return true;}
bool decode_path(const char* encoded,char* out,size_t capacity){
 size_t n=0;
 for(size_t i=0;encoded[i];++i){unsigned c=static_cast<unsigned char>(encoded[i]);
  if(c=='%'){if(!encoded[i+1]||!encoded[i+2])return false;int a=hex(encoded[i+1]),b=hex(encoded[i+2]);if(a<0||b<0)return false;c=(a<<4)|b;i+=2;}
  if(c<32||c==127||c=='\\'||c==':'||n+1>=capacity)return false;
  out[n++]=static_cast<char>(c);
 }out[n]=0;return canonical_utf8(reinterpret_cast<const unsigned char*>(out));
}
bool shared_path(const char* path,bool allow_root){
 if(!path||path[0]!='/')return false;
 if(allow_root&&!path[1])return true;
 if(std::strncmp(path,"/files",6)&&std::strncmp(path,"/firmware",9))return false;
 const size_t root=!std::strncmp(path,"/files",6)?6:9;
 if(path[root]&&path[root]!='/')return false;
 for(const char* p=path+1;*p;){const char* end=std::strchr(p,'/');const size_t n=end?size_t(end-p):std::strlen(p);
  if(!n||n>128||p[0]=='.')return false;
  for(size_t i=0;i<n;++i){const unsigned c=static_cast<unsigned char>(p[i]);if(c<32||c==127||c==':'||c=='\\')return false;}
  if(!end)break;
  p=end+1;if(!*p)return false;
 }return true;
}
bool transfer_id(const char* id){const size_t n=std::strlen(id);if(n<8||n>24)return false;for(size_t i=0;i<n;++i)if(hex(id[i])<0)return false;return true;}
bool byte_range(const char* h,uint32_t size,uint32_t& start,uint32_t& length){
 start=0;length=size;if(!h||!*h)return true;
 if(std::strncmp(h,"bytes=",6)||std::strchr(h,',')||!size)return false;
 const char* p=h+6;char* end=nullptr;
 auto number=[](const char* s,char** e)->uint64_t{errno=0;return std::strtoull(s,e,10);};
 if(*p=='-'){if(p[1]<'0'||p[1]>'9')return false;auto suffix=number(p+1,&end);if(errno||*end||!suffix)return false;length=std::min<uint64_t>(suffix,size);start=size-length;return true;}
 if(*p<'0'||*p>'9')return false;
 const auto first=number(p,&end);if(errno||*end!='-'||first>=size)return false;p=end+1;
 uint64_t last=size-1;
 if(*p){if(*p<'0'||*p>'9')return false;last=number(p,&end);if(errno||*end||last<first)return false;last=std::min<uint64_t>(last,size-1);}
 start=first;length=last-first+1;return true;
}
}
