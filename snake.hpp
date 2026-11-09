#pragma once
#include "GyverOLED.h"

extern void snakeInit(GyverOLED<SSD1306_128x64, OLED_BUFFER>* oled);
extern void snakeGame(void);