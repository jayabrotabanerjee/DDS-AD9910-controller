/* FILE: menuclk.h */
#ifndef __MENUCLK_H
#define __MENUCLK_H

#include "main.h"

#define CLOCK_SOURCE_INDEX_ADR 0
#define CLOCK_XO_FREQ_INDEX_ADR 2
#define CLOCK_TCXO_FREQ_INDEX_ADR 4
#define DIVIDER_ADR 6
#define EXT_OSC_FREQ_ADR 8 
#define DDS_CORE_CLOCK_ADR 12 
#define DAC_CURRENT_INDEX_ADR 20
#define CORE_CLOCK_OFFSET_ADR 64
#define CLOCK_SETTINGS_FLAG_ADR 101

// Global Externs needed for ad9910.cpp and ReadSerialCommands.ino
extern uint32_t Ref_Clk;
extern uint32_t DDS_Core_Clock;
extern int DACCurrentIndex;
extern int16_t ClockOffset;

void DDS_Clock_Config_Menu();
void DisplayClockSetupMenu();
void SaveClockSettings();
void LoadClockSettings();

#endif
