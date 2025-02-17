/* Audio Library for Teensy 3.X
 * Copyright (c) 2014, Paul Stoffregen, paul@pjrc.com
 *
 * Development of this audio library was funded by PJRC.COM, LLC by sales of
 * Teensy and Audio Adaptor boards.  Please support PJRC's efforts to develop
 * open source software by purchasing Teensy or other PJRC products.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice, development funding notice, and this permission
 * notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef output_i2s_esp32_h_
#define output_i2s_esp32_h_

#include "Arduino.h"
#include "AudioStream.h"
#include "DMAChannel.h"

class AudioOutputI2S_ESP32 : public AudioStream
{
public:
  AudioOutputI2S_ESP32(void) : AudioStream(2, inputQueueArray) { begin(); }
  virtual void update(void);
  void begin(void);
  friend class AudioInputI2S_ESP32;
#if defined(__IMXRT1062__)
  friend class AudioOutputI2SQuad;
  friend class AudioInputI2SQuad;
  friend class AudioOutputI2SHex;
  friend class AudioInputI2SHex;
  friend class AudioOutputI2SOct;
  friend class AudioInputI2SOct;
#endif
protected:
  AudioOutputI2S_ESP32(int dummy): AudioStream(2, inputQueueArray) {} // to be used only inside AudioOutputI2Sslave_ESP32 !!
  static void config_i2s(void);
  static audio_block_t *block_left_1st;
  static audio_block_t *block_right_1st;
  static bool update_responsibility;
  static DMAChannel dma;
  static void isr(void);
private:
  static audio_block_t *block_left_2nd;
  static audio_block_t *block_right_2nd;
  static uint16_t block_left_offset;
  static uint16_t block_right_offset;
  audio_block_t *inputQueueArray[2];
};


class AudioOutputI2Sslave_ESP32 : public AudioOutputI2S_ESP32
{
public:
  AudioOutputI2Sslave_ESP32(void) : AudioOutputI2S_ESP32(0) { begin(); } ;
  void begin(void);
  friend class AudioInputI2Sslave_ESP32;
  friend void dma_ch0_isr(void);
protected:
  static void config_i2s(void);
};

#endif
