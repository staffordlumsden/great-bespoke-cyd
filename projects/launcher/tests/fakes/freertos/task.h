#pragma once
void vTaskDelay(unsigned ticks);
using TaskHandle_t=void*;
inline TaskHandle_t xTaskGetCurrentTaskHandle(){return reinterpret_cast<void*>(1);}
inline unsigned uxTaskGetStackHighWaterMark(TaskHandle_t){return 4000;}
inline void xTaskNotifyGive(TaskHandle_t){}
inline unsigned ulTaskNotifyTake(int,unsigned){return 1;}
inline void vTaskDelete(TaskHandle_t){}
inline int xTaskCreate(void(*function)(void*),const char*,unsigned,void* arg,unsigned,TaskHandle_t*){function(arg);return pdPASS;}
