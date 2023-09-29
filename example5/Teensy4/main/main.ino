#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>




#include "input_i2s2_16bit.h"
#include "output_i2s2_16bit.h"
#include "plotter.h"

#include <Audio.h>



AudioInputI2S i2s1;    
AudioControlSGTL5000     sgtl5000_1;   

AudioInputI2S2_16bit i2s2;
Plotter plotter(8);
AudioOutputSPDIF3 spdifOut;
AudioConnection          patchCord1(i2s2, 0, plotter, 0);
AudioConnection          patchCord2(i2s2, 1, plotter, 1);
AudioConnection          patchCord3(i2s2, 0, i2s1, 0);
AudioConnection          patchCord4(i2s2, 1, i2s1, 1);


void setup() {
  AudioMemory(260);
  Serial.begin(115200);
  plotter.activate(true);
  sgtl5000_1.enable();
  sgtl5000_1.volume(0.5);
}

void loop() {
   
}
