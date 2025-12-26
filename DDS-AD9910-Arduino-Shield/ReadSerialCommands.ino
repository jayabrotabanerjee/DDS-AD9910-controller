/* FILE: ReadSerialCommands.ino */
#include "main.h"
#include "ad9910.h"
#include "menuclk.h" // Needed to see Ref_Clk
#include <GParser.h>

#define SERIAL_PACKAGE_MAX_LENGTH 40
char SerialBuffer[SERIAL_PACKAGE_MAX_LENGTH];

void ReadSerialCommands()
{
  if (!Serial.available()) return;
  int RcvCounter = Serial.readBytesUntil('\n', SerialBuffer, 110);
  if (RcvCounter == 0) return;
  SerialBuffer[RcvCounter]='\0';

  uint32_t value=0;
  char command;

  GParser data(SerialBuffer, ';');
  int commandsCounter = data.split();
  for (int i=0; i < commandsCounter; i++)
    {
      long tempVal;
      sscanf(data[i], "%c%li", &command, &tempVal);
      value = (uint32_t)tempVal;

      switch (command)
      {
        case 'F': // Set Freq
          if (value >= 100000) {
            Serial.print(F("Set Freq: ")); Serial.println(value);
            H = value % 1000; value /= 1000;
            K = value % 1000; value /= 1000;
            M = value;
            MakeOut(); 
            UpdateDisplay();
            CheckPLL(); // Report Status
          }
        break;
        
        case 'W': // Manual Debug
           ManualWriteProfile0(value);
        break;

        case 'x': Write_CFR_Reg(0x00, value); break; 
        case 'y': Write_CFR_Reg(0x01, value); break; 
        case 'z': Write_CFR_Reg(0x02, value); break; 
        case 'r': Read_Register((uint8_t)value); break; 

        case 'P': // Power
          int pwr; sscanf(data[i], "%c%d", &command, &pwr);
          if (DACCurrentIndex == 0) A = -1 * pwr; else A = -1 * (pwr - 4);
          MakeOut();
        break;
      } 
    } 
}
