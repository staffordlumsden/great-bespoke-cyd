#pragma once
#include <cstdlib>
constexpr unsigned MALLOC_CAP_INTERNAL=1,MALLOC_CAP_8BIT=2;
inline void* heap_caps_malloc(size_t n,unsigned){return std::malloc(n);}
inline void heap_caps_free(void* p){std::free(p);}
