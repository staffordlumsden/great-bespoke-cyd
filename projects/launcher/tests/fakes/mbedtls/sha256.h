#ifdef __APPLE__
#pragma once
#include <CommonCrypto/CommonDigest.h>
#include <cstddef>
struct mbedtls_sha256_context {CC_SHA256_CTX cc;};
inline void mbedtls_sha256_init(mbedtls_sha256_context*){}
inline void mbedtls_sha256_free(mbedtls_sha256_context*){}
inline int mbedtls_sha256_starts(mbedtls_sha256_context* c,int){return CC_SHA256_Init(&c->cc)?0:-1;}
inline int mbedtls_sha256_update(mbedtls_sha256_context* c,const unsigned char* b,size_t n){return CC_SHA256_Update(&c->cc,b,n)?0:-1;}
inline int mbedtls_sha256_finish(mbedtls_sha256_context* c,unsigned char* b){return CC_SHA256_Final(b,&c->cc)?0:-1;}

#else
#pragma once
#include <openssl/sha.h>
#include <cstddef>
struct mbedtls_sha256_context { SHA256_CTX cc; };
inline void mbedtls_sha256_init(mbedtls_sha256_context*){}
inline void mbedtls_sha256_free(mbedtls_sha256_context*){}
inline int mbedtls_sha256_starts(mbedtls_sha256_context* c,int){return SHA256_Init(&c->cc)?0:-1;}
inline int mbedtls_sha256_update(mbedtls_sha256_context* c,const unsigned char* b,size_t n){return SHA256_Update(&c->cc,b,n)?0:-1;}
inline int mbedtls_sha256_finish(mbedtls_sha256_context* c,unsigned char* b){return SHA256_Final(b,&c->cc)?0:-1;}
#endif
