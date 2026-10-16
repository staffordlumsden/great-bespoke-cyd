#pragma once
void vTaskDelay(unsigned);
int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, void *);

unsigned uxTaskGetStackHighWaterMark(void*);
