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


#if defined(__IMXRT1062__)
#include <Arduino.h>
#include "input_i2s2_16bit.h"
#include "output_i2s2_16bit.h"

DMAMEM __attribute__((aligned(32))) static uint32_t i2s2_rx_buffer[AUDIO_BLOCK_SAMPLES];
audio_block_t * AudioInputI2S2_16bit::block_left = NULL;
audio_block_t * AudioInputI2S2_16bit::block_right = NULL;
uint16_t AudioInputI2S2_16bit::block_offset = 0;
bool AudioInputI2S2_16bit::update_responsibility = false;
DMAChannel AudioInputI2S2_16bit::dma(false);

// ============== needed for resampling
const float toFloatAudio= (float)1./pow(2., 15.); //(float)1./pow(2., 31.);
constexpr int32_t noSamplerPerIsr=AUDIO_BLOCK_SAMPLES/2;
AsyncAudioInputI2S2_16bitslave::FrequencyM AsyncAudioInputI2S2_16bitslave::frequencyM;	
float* AsyncAudioInputI2S2_16bitslave::sampleBuffer[] ={NULL, NULL} ;
int32_t AsyncAudioInputI2S2_16bitslave::sampleBufferLength = 0;
volatile int32_t AsyncAudioInputI2S2_16bitslave::buffer_offset=0;
volatile int32_t AsyncAudioInputI2S2_16bitslave::resample_offset=0;
DMAChannel AsyncAudioInputI2S2_16bitslave::asyncDma(false);
//======================================

void AudioInputI2S2_16bit::begin(void)
{
	dma.begin(true); // Allocate the DMA channel first

	//block_left_1st = NULL;
	//block_right_1st = NULL;

	// TODO: should we set & clear the I2S_RCSR_SR bit here?
	AudioOutputI2S2_16bit::config_i2s();

	CORE_PIN5_CONFIG = 2;  //EMC_08, 2=SAI2_RX_DATA, page 434
	IOMUXC_SAI2_RX_DATA0_SELECT_INPUT = 0; // 0=GPIO_EMC_08_ALT2, page 876

	dma.TCD->SADDR = (void *)((uint32_t)&I2S2_RDR0+2);
	dma.TCD->SOFF = 0;
	dma.TCD->ATTR = DMA_TCD_ATTR_SSIZE(1) | DMA_TCD_ATTR_DSIZE(1);
	dma.TCD->NBYTES_MLNO = 2;
	dma.TCD->SLAST = 0;
	dma.TCD->DADDR = i2s2_rx_buffer;
	dma.TCD->DOFF = 2;
	dma.TCD->CITER_ELINKNO = sizeof(i2s2_rx_buffer) / 2;
	dma.TCD->DLASTSGA = -sizeof(i2s2_rx_buffer);
	dma.TCD->BITER_ELINKNO = sizeof(i2s2_rx_buffer) / 2;
	dma.TCD->CSR = DMA_TCD_CSR_INTHALF | DMA_TCD_CSR_INTMAJOR;
	dma.triggerAtHardwareEvent(DMAMUX_SOURCE_SAI2_RX);
	dma.enable();

	I2S2_RCSR = I2S_RCSR_RE | I2S_RCSR_BCE | I2S_RCSR_FRDE | I2S_RCSR_FR; // page 2099
	I2S2_TCSR |= I2S_TCSR_TE | I2S_TCSR_BCE; // page 2087

	update_responsibility = update_setup();
	dma.attachInterrupt(isr);
}

void AudioInputI2S2_16bit::isr(void)
{
	uint32_t daddr, offset;
	const int16_t *src, *end;
	int16_t *dest_left, *dest_right;
	audio_block_t *left, *right;

	daddr = (uint32_t)(dma.TCD->DADDR);
	dma.clearInterrupt();

	if (daddr < (uint32_t)i2s2_rx_buffer + sizeof(i2s2_rx_buffer) / 2) {
		// DMA is receiving to the first half of the buffer
		// need to remove data from the second half
		src = (int16_t *)&i2s2_rx_buffer[AUDIO_BLOCK_SAMPLES/2];
		end = (int16_t *)&i2s2_rx_buffer[AUDIO_BLOCK_SAMPLES];
		if (AudioInputI2S2_16bit::update_responsibility) AudioStream::update_all();
	} else {
		// DMA is receiving to the second half of the buffer
		// need to remove data from the first half
		src = (int16_t *)&i2s2_rx_buffer[0];
		end = (int16_t *)&i2s2_rx_buffer[AUDIO_BLOCK_SAMPLES/2];
	}
	left = AudioInputI2S2_16bit::block_left;
	right = AudioInputI2S2_16bit::block_right;
	if (left != NULL && right != NULL) {
		offset = AudioInputI2S2_16bit::block_offset;
		if (offset <= AUDIO_BLOCK_SAMPLES/2) {
			dest_left = &(left->data[offset]);
			dest_right = &(right->data[offset]);
			AudioInputI2S2_16bit::block_offset = offset + AUDIO_BLOCK_SAMPLES/2;

			arm_dcache_delete((void*)src, sizeof(i2s2_rx_buffer) / 2);

			do {
				*dest_left++ = *src++;
				*dest_right++ = *src++;
			} while (src < end);
		}
	}
}



void AudioInputI2S2_16bit::update(void)
{
	audio_block_t *new_left=NULL, *new_right=NULL, *out_left=NULL, *out_right=NULL;

	// allocate 2 new blocks, but if one fails, allocate neither
	new_left = allocate();
	if (new_left != NULL) {
		new_right = allocate();
		if (new_right == NULL) {
			release(new_left);
			new_left = NULL;
		}
	}
	__disable_irq();
	if (block_offset >= AUDIO_BLOCK_SAMPLES) {
		// the DMA filled 2 blocks, so grab them and get the
		// 2 new blocks to the DMA, as quickly as possible
		out_left = block_left;
		block_left = new_left;
		out_right = block_right;
		block_right = new_right;
		block_offset = 0;
		__enable_irq();
		// then transmit the DMA's former blocks
		transmit(out_left, 0);
		release(out_left);
		transmit(out_right, 1);
		release(out_right);
		//Serial.print(".");
	} else if (new_left != NULL) {
		// the DMA didn't fill blocks, but we allocated blocks
		if (block_left == NULL) {
			// the DMA doesn't have any blocks to fill, so
			// give it the ones we just allocated
			block_left = new_left;
			block_right = new_right;
			block_offset = 0;
			__enable_irq();
		} else {
			// the DMA already has blocks, doesn't need these
			__enable_irq();
			release(new_left);
			release(new_right);
		}
	} else {
		// The DMA didn't fill blocks, and we could not allocate
		// memory... the system is likely starving for memory!
		// Sadly, there's nothing we can do.
		__enable_irq();
	}
}


/******************************************************************/

void AudioInputI2S2_16bitslave::begin(void)
{
	dma.begin(true); // Allocate the DMA channel first

	//block_left_1st = NULL;
	//block_right_1st = NULL;

	AudioOutputI2S2_16bitslave::config_i2s();

	CORE_PIN5_CONFIG = 2;  //EMC_08, 2=SAI2_RX_DATA, page 434
	IOMUXC_SAI2_RX_DATA0_SELECT_INPUT = 0; // 0=GPIO_EMC_08_ALT2, page 876

	dma.TCD->SADDR = (void *)((uint32_t)&I2S2_RDR0+2);
	dma.TCD->SOFF = 0;
	dma.TCD->ATTR = DMA_TCD_ATTR_SSIZE(1) | DMA_TCD_ATTR_DSIZE(1);
	dma.TCD->NBYTES_MLNO = 2;
	dma.TCD->SLAST = 0;
	dma.TCD->DADDR = i2s2_rx_buffer;
	dma.TCD->DOFF = 2;
	dma.TCD->CITER_ELINKNO = sizeof(i2s2_rx_buffer) / 2;
	dma.TCD->DLASTSGA = -sizeof(i2s2_rx_buffer);
	dma.TCD->BITER_ELINKNO = sizeof(i2s2_rx_buffer) / 2;
	dma.TCD->CSR = DMA_TCD_CSR_INTHALF | DMA_TCD_CSR_INTMAJOR;
	dma.triggerAtHardwareEvent(DMAMUX_SOURCE_SAI2_RX);
	dma.enable();
	
	I2S2_RCSR = I2S_RCSR_RE | I2S_RCSR_BCE | I2S_RCSR_FRDE | I2S_RCSR_FR;
	update_responsibility = update_setup();
	dma.attachInterrupt(isr);

}



void AsyncAudioInputI2S2_16bitslave::begin()
{
	asyncDma.begin(true); // Allocate the DMA channel first


	AudioOutputI2S2_16bitslave::config_i2s();

	CORE_PIN5_CONFIG = 2;  //EMC_08, 2=SAI2_RX_DATA, page 434
	IOMUXC_SAI2_RX_DATA0_SELECT_INPUT = 0; // 0=GPIO_EMC_08_ALT2, page 876

	asyncDma.TCD->SADDR = (void *)((uint32_t)&I2S2_RDR0+2);
	asyncDma.TCD->SOFF = 0;
	asyncDma.TCD->ATTR = DMA_TCD_ATTR_SSIZE(1) | DMA_TCD_ATTR_DSIZE(1);
	asyncDma.TCD->NBYTES_MLNO = 2;
	asyncDma.TCD->SLAST = 0;
	asyncDma.TCD->DADDR = i2s2_rx_buffer;
	asyncDma.TCD->DOFF = 2;
	asyncDma.TCD->CITER_ELINKNO = sizeof(i2s2_rx_buffer) / 2;
	asyncDma.TCD->DLASTSGA = -sizeof(i2s2_rx_buffer);
	asyncDma.TCD->BITER_ELINKNO = sizeof(i2s2_rx_buffer) / 2;
	asyncDma.TCD->CSR = DMA_TCD_CSR_INTHALF | DMA_TCD_CSR_INTMAJOR;
	asyncDma.triggerAtHardwareEvent(DMAMUX_SOURCE_SAI2_RX);
	asyncDma.enable();
	
	I2S2_RCSR = I2S_RCSR_RE | I2S_RCSR_BCE | I2S_RCSR_FRDE | I2S_RCSR_FR;
	asyncDma.attachInterrupt(isrResample);

}

void AsyncAudioInputI2S2_16bitslave::setResampleBuffer(float** buffer, int32_t bufferLength){
	sampleBuffer[0] = buffer[0];
	sampleBuffer[1] = buffer[1];
	sampleBufferLength = bufferLength;
}
void AsyncAudioInputI2S2_16bitslave::setFrequencyMeasurment(AsyncAudioInputI2S2_16bitslave::FrequencyM fm){
	frequencyM=fm;
}

void AsyncAudioInputI2S2_16bitslave::isrResample(void)
{
	if (frequencyM){
		frequencyM();
	}
	asyncDma.clearInterrupt();

	uint32_t daddr;
	const int16_t *src, *end;

	daddr = (uint32_t)(asyncDma.TCD->DADDR);

	if (daddr < (uint32_t)i2s2_rx_buffer + sizeof(i2s2_rx_buffer) / 2) {
		// DMA is receiving to the first half of the buffer
		// need to remove data from the second half
		src = (int16_t *)&i2s2_rx_buffer[AUDIO_BLOCK_SAMPLES/2];
		end = (int16_t *)&i2s2_rx_buffer[AUDIO_BLOCK_SAMPLES];
	} else {
		// DMA is receiving to the second half of the buffer
		// need to remove data from the first half
		src = (int16_t *)&i2s2_rx_buffer[0];
		end = (int16_t *)&i2s2_rx_buffer[AUDIO_BLOCK_SAMPLES/2];
	}
	int32_t distToResampleOffset = buffer_offset >= resample_offset ? resample_offset + (sampleBufferLength-buffer_offset) : resample_offset-buffer_offset;
	arm_dcache_delete((void*)src, sizeof(i2s2_rx_buffer) / 2);
	if (sampleBuffer[0] != NULL && sampleBuffer[1] != NULL && distToResampleOffset > noSamplerPerIsr) {

		float* dest_left = sampleBuffer[0]+buffer_offset;
		float* dest_right = sampleBuffer[1]+buffer_offset;
		do {
			*dest_left++ = *src++ * toFloatAudio;	//toFloatAudio is currently the factor from 16bit integer to 32bit floating point audio samples
			*dest_right++ = *src++ * toFloatAudio;

			buffer_offset++;
			if (buffer_offset >= sampleBufferLength){
				buffer_offset=0;
				dest_left = sampleBuffer[0];
				dest_right = sampleBuffer[1];
			}
		} while (src < end);
	}
}
int32_t AsyncAudioInputI2S2_16bitslave::getBufferOffset(){
	return buffer_offset;
}
int32_t AsyncAudioInputI2S2_16bitslave::getNumberOfSamplesPerIsr(){
	return noSamplerPerIsr;
}
void AsyncAudioInputI2S2_16bitslave::setResampleOffset(int32_t offset){
	resample_offset = offset;
}



#endif
