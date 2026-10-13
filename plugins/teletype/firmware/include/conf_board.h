// Replaces libavr32's conf/teletype/conf_board.h, which carries Atmel's ASF licence (redistribution
// only with Atmel microcontroller products). The board names teletype's sources use: the teletype
// hardware's clock, pins and peripherals. AVR32_* come from monome-rack's avr32 part headers (BSD).
#pragma once

#include "compiler.h"

#define FMCK_HZ 60000000  // master clock
#define FCPU_HZ FMCK_HZ

// trigger inputs 1-8 are A00..A07 (A00 + n); TR outputs 1-4 are B08..B11 (B08 + n)
#define A00 AVR32_PIN_TDI
#define A07 AVR32_PIN_PA07
#define B08 AVR32_PIN_PB08
#define B09 AVR32_PIN_PB09
#define B10 AVR32_PIN_PB10
#define B11 AVR32_PIN_PB11
#define NMI AVR32_PIN_PA13  // front panel button

#define USART (&AVR32_USART1)
#define SPI (&AVR32_SPI)
#define DAC_SPI SPI
#define DAC_SPI_NPCS 0
#define TWI (&AVR32_TWI)
