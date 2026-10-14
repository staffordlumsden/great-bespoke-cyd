#pragma once
struct StaticSemaphore_t{};
using SemaphoreHandle_t=StaticSemaphore_t*;
inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* p){return p;}
inline int xSemaphoreTake(SemaphoreHandle_t,unsigned){return 1;}
inline void xSemaphoreGive(SemaphoreHandle_t){}
