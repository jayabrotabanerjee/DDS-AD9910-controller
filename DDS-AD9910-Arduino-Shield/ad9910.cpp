/* FILE: ad9910.cpp */
#include <Arduino.h>
#include <Float64.h>
#include "ad9910.h"
#include "main.h"
#include "menuclk.h" // Needed for variable visibility
#include <math.h>

// --- HARDWARE CONSTANTS ---
// 10 MHz Red Pitaya -> 1 GHz Core
#define HW_REF_CLK       10000000UL   
#define HW_CORE_CLOCK    1000000000UL 
// --------------------------

int hspi1 = 0;
uint8_t strBuffer[9];
uint32_t FTW;
uint32_t *jlob;

// Variables defined in menuclk.cpp
extern uint32_t Ref_Clk;
extern uint32_t DDS_Core_Clock;
extern int DACCurrentIndex;
extern int16_t ClockOffset;

void HAL_SPI_Transmit(int *blank, uint8_t *strBuffer, int nums, int pause) {
  for (int i = 0; i < nums; i++) SPI.transfer(*(strBuffer + i));
}

void HAL_Delay(int del) { delayMicroseconds(del); }

// FIXED: GPIO Wrapper (Ignores 'Port', takes only Pin/Mode)
void HAL_GPIO_WritePin(int pin, int mode) { 
  digitalWrite(pin, mode); 
}

void DDS_GPIO_Init(void) {
   pinMode(DDS_SPI_SCLK_PIN, OUTPUT); pinMode(DDS_SPI_SDIO_PIN, OUTPUT);
   pinMode(DDS_SPI_SDO_PIN, INPUT);   pinMode(DDS_SPI_CS_PIN, OUTPUT);
   pinMode(DDS_IO_UPDATE_PIN, OUTPUT);pinMode(DDS_IO_RESET_PIN, OUTPUT);
   pinMode(DDS_MASTER_RESET_PIN, OUTPUT);
   pinMode(DDS_PROFILE_0_PIN, OUTPUT); pinMode(DDS_PROFILE_1_PIN, OUTPUT); pinMode(DDS_PROFILE_2_PIN, OUTPUT);
   pinMode(DDS_OSK_PIN, OUTPUT); pinMode(DDS_TxENABLE_PIN, OUTPUT);
   pinMode(DDS_DRHOLD_PIN, OUTPUT); pinMode(DDS_PWR_DWN_PIN, OUTPUT); pinMode(DDS_DRCTL_PIN, OUTPUT);
   pinMode(DDS_PLL_LOCK, INPUT); 

   HAL_Delay(10);
   HAL_GPIO_WritePin(DDS_IO_UPDATE_PIN, LOW);
   HAL_GPIO_WritePin(DDS_MASTER_RESET_PIN, LOW);
   HAL_GPIO_WritePin(DDS_SPI_CS_PIN, HIGH);
   HAL_GPIO_WritePin(DDS_OSK_PIN, LOW);
   HAL_Delay(10);
}

void DDS_SPI_Init(void) {
  SPI.begin(); 
  SPI.setDataMode(SPI_MODE0); 
  SPI.setClockDivider(SPI_CLOCK_DIV2); 
  SPI.setBitOrder(MSBFIRST);
}

void DDS_UPDATE(void) {
   HAL_Delay(2);
   HAL_GPIO_WritePin(DDS_IO_UPDATE_PIN, HIGH);
   HAL_Delay(2);
   HAL_GPIO_WritePin(DDS_IO_UPDATE_PIN, LOW);
   HAL_Delay(2);
}

void CheckPLL() {
  if(digitalRead(DDS_PLL_LOCK) == HIGH) Serial.println(F("PLL Status: LOCKED"));
  else Serial.println(F("PLL Status: UNLOCKED (Check 10MHz Input)"));
}

void DDS_Init(bool PLL, bool Divider, uint32_t Ref_Clk_In) {
   // Force correct settings for 10MHz -> 1GHz
   Ref_Clk = HW_REF_CLK;
   DDS_Core_Clock = HW_CORE_CLOCK;
   
   DDS_GPIO_Init();
   HAL_GPIO_WritePin(DDS_MASTER_RESET_PIN, HIGH); 
   HAL_Delay(100);
   HAL_GPIO_WritePin(DDS_MASTER_RESET_PIN, LOW); 
   DDS_SPI_Init();
   
   // CFR1
   HAL_GPIO_WritePin(DDS_SPI_CS_PIN, LOW);    
   strBuffer[0] = CFR1_addr; strBuffer[1]=0; strBuffer[2]=0; strBuffer[3]=0; strBuffer[4]=SDIO_input_only;
   HAL_SPI_Transmit(&hspi1, (uint8_t*)strBuffer, 5, 1000);
   HAL_GPIO_WritePin(DDS_SPI_CS_PIN, HIGH);
   DDS_UPDATE();
   
   // CFR2
   HAL_GPIO_WritePin(DDS_SPI_CS_PIN, LOW);    
   strBuffer[0] = CFR2_addr; strBuffer[1]=Enable_amplitude_scale_from_single_tone_profiles; strBuffer[2]=0; strBuffer[3]=0; strBuffer[4]=Sync_timing_validation_disable;
   HAL_SPI_Transmit(&hspi1, (uint8_t*)strBuffer, 5, 1000);
   HAL_GPIO_WritePin(DDS_SPI_CS_PIN, HIGH);
   DDS_UPDATE();

   // --- CFR3 (PLL SETUP) ---
   Serial.println(F("Initializing PLL: 10MHz -> 1GHz..."));
   
   HAL_GPIO_WritePin(DDS_SPI_CS_PIN, LOW);    
   strBuffer[0] = CFR3_addr;
   
   strBuffer[1] = VCO5;     // Range 5 (820-1150 MHz)
   strBuffer[2] = Icp387uA; // Max Charge Pump Current

   // Input Divider: BYPASSED (Because 10MHz < 60MHz)
   strBuffer[3] = REFCLK_input_divider_bypass | REFCLK_input_divider_ResetB | PLL_enable; 

   // Multiplier N = 100 (10 MHz * 100 = 1000 MHz)
   // Register format N << 1. 
   // 100 decimal = 0x64. 0x64 << 1 = 0xC8.
   strBuffer[4] = 0xC8; 
   
   HAL_SPI_Transmit(&hspi1, (uint8_t*)strBuffer, 5, 1000);
   HAL_GPIO_WritePin(DDS_SPI_CS_PIN, HIGH);
   DDS_UPDATE();
   
   delay(100);
   CheckPLL();

   // FSC
   HAL_GPIO_WritePin(DDS_SPI_CS_PIN, LOW);    
   strBuffer[0] = FSC_addr; strBuffer[1]=0; strBuffer[2]=0; strBuffer[3]=0; strBuffer[4]=0xFF; 
   HAL_SPI_Transmit(&hspi1, (uint8_t*)strBuffer, 5, 1000);
   HAL_GPIO_WritePin(DDS_SPI_CS_PIN, HIGH);
   DDS_UPDATE();
}

uint32_t CalcRealDDSCoreClockFromOffset() {
  return HW_CORE_CLOCK + ClockOffset;
}

uint32_t FreqToFTW(uint32_t Freq) {
    uint32_t RealCore = CalcRealDDSCoreClockFromOffset();
    if(RealCore == 0) RealCore = 1000000000UL;
    float64_t f64Freq = f64(Freq);
    float64_t f64Core = f64(RealCore);
    float64_t TwoPow32 = f64(4294967295UL);
    float64_t f64FTW = f64_div(TwoPow32, f64Core);
    f64FTW = f64_mul(f64FTW, f64Freq);
    bool a;
    return f64_to_ui32(f64FTW, softfloat_round_near_maxMag, a);
}

void SingleProfileFreqOut(uint32_t freq_output, int16_t amplitude_dB_output) {
  HAL_GPIO_WritePin(DDS_SPI_CS_PIN, LOW);    
  strBuffer[0] = CFR1_addr; strBuffer[1]=0; strBuffer[2]=0; strBuffer[3]=0; strBuffer[4]=SDIO_input_only;
  HAL_SPI_Transmit(&hspi1, (uint8_t*)strBuffer, 5, 1000);
  HAL_GPIO_WritePin(DDS_SPI_CS_PIN, HIGH);  
  DDS_UPDATE();
  DDS_Fout(&freq_output, amplitude_dB_output, Single_Tone_Profile_0);
}

void DDS_Fout (uint32_t *F_OUT, int16_t Amplitude_dB, uint8_t Num_Prof) {
   FTW = FreqToFTW(*F_OUT);
   jlob = &FTW;
   HAL_GPIO_WritePin(DDS_SPI_CS_PIN, LOW); 	 
   strBuffer[0] = Num_Prof; 
   uint16_t amp = (uint16_t)powf(10,(Amplitude_dB+84.288)/20.0);
   if (amp > 16383) amp = 16383;
   strBuffer[1] = amp >> 8; strBuffer[2] = amp & 0xFF;         
   strBuffer[3] = 0; strBuffer[4] = 0;
   strBuffer[5] = *(((uint8_t*)jlob)+ 3); strBuffer[6] = *(((uint8_t*)jlob)+ 2);
   strBuffer[7] = *(((uint8_t*)jlob)+ 1); strBuffer[8] = *(((uint8_t*)jlob));
   HAL_SPI_Transmit(&hspi1, (uint8_t*)strBuffer, 9, 1000);
   HAL_GPIO_WritePin(DDS_SPI_CS_PIN, HIGH);
   DDS_UPDATE(); 
   
   int p = Num_Prof - 0x0E;
   digitalWrite(DDS_PROFILE_0_PIN, (p & 1));
   digitalWrite(DDS_PROFILE_1_PIN, (p & 2));
   digitalWrite(DDS_PROFILE_2_PIN, (p & 4));
   DDS_UPDATE(); 
   
   // Check Lock on every update for debugging
   // CheckPLL(); 
}

// --- DEBUG FUNCTIONS ---
void Write_CFR_Reg(uint8_t reg_addr, uint32_t value) {
    HAL_GPIO_WritePin(DDS_SPI_CS_PIN, LOW);
    strBuffer[0] = reg_addr;
    strBuffer[1] = (uint8_t)(value >> 24); strBuffer[2] = (uint8_t)(value >> 16);
    strBuffer[3] = (uint8_t)(value >> 8);  strBuffer[4] = (uint8_t)(value & 0xFF);
    HAL_SPI_Transmit(&hspi1, (uint8_t*)strBuffer, 5, 1000);
    HAL_GPIO_WritePin(DDS_SPI_CS_PIN, HIGH);
    DDS_UPDATE();
    Serial.print(F("CFR Write 0x")); Serial.println(value, HEX);
    CheckPLL();
}

void Read_Register(uint8_t reg_addr) {
    int len = (reg_addr >= 0x0E) ? 8 : 4;
    HAL_GPIO_WritePin(DDS_SPI_CS_PIN, LOW);
    SPI.transfer(0x80 | reg_addr);
    Serial.print(F("READ 0x")); Serial.print(reg_addr, HEX); Serial.print(": ");
    for(int i=0; i<len; i++) {
        Serial.print(SPI.transfer(0x00), HEX); Serial.print(" ");
    }
    Serial.println();
    HAL_GPIO_WritePin(DDS_SPI_CS_PIN, HIGH);
}

void ManualWriteProfile0(uint32_t freq_hz) {
  // Manual check using 1GHz math
  float ratio = 4294967296.0 / 1000000000.0; 
  uint32_t ftw_val = (uint32_t)((float)freq_hz * ratio);
  
  Serial.print(F("Manual Write: ")); Serial.print(freq_hz);
  Serial.print(F(" Hz (FTW: 0x")); Serial.print(ftw_val, HEX); Serial.println(")");

  HAL_GPIO_WritePin(DDS_SPI_CS_PIN, LOW);
  strBuffer[0] = 0x0E; 
  strBuffer[1] = 0x3F; strBuffer[2] = 0xFF; strBuffer[3] = 0x00; strBuffer[4] = 0x00; 
  strBuffer[5] = (ftw_val >> 24) & 0xFF; strBuffer[6] = (ftw_val >> 16) & 0xFF;
  strBuffer[7] = (ftw_val >> 8) & 0xFF;  strBuffer[8] = ftw_val & 0xFF;
  HAL_SPI_Transmit(&hspi1, (uint8_t*)strBuffer, 9, 1000);
  HAL_GPIO_WritePin(DDS_SPI_CS_PIN, HIGH);
  DDS_UPDATE();
  
  // Force Profile 0
  digitalWrite(DDS_PROFILE_0_PIN, LOW);
  digitalWrite(DDS_PROFILE_1_PIN, LOW);
  digitalWrite(DDS_PROFILE_2_PIN, LOW);
  
  CheckPLL();
}

// Stubs
void PrepRegistersToSaveWaveForm (uint64_t Step_Rate, uint16_t Step){}
void calcBestStepRate(uint16_t *Step, uint64_t *Step_Rate, uint32_t F_mod){}
void SaveAMWavesToRAM(uint32_t F_carrier, uint32_t F_mod, uint32_t AM_DEPH, int16_t Amplitude_dB){}
void PlaybackAMFromRAM(uint32_t F_carrier){}
void SaveFMWavesToRAM (uint32_t F_carrier, uint32_t F_mod, uint32_t F_dev){}
void PlaybackFMFromRAM(int16_t Amplitude_dB){}
void DigitalRamp(uint32_t FTWStart, uint32_t FTWEnd, uint32_t FTWStepSize, uint16_t StepRate){}
void Sweep(uint32_t StartSweepFreq, uint32_t StopSweepFreq, uint16_t SweepTime, uint8_t SweepTimeFormat){}
