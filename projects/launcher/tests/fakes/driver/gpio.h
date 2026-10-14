#pragma once
constexpr int GPIO_NUM_36=36;
extern int mock_touch_irq_level;
inline int gpio_get_level(int){return mock_touch_irq_level;}
