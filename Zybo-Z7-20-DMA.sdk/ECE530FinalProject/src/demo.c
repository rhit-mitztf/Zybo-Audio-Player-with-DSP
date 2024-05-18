/************************************************************************/
/*																		*/
/*	demo.c	--	Zybo DMA Demo				 						*/
/*																		*/
/************************************************************************/
/*	Author: Sam Lowe											*/
/*	Copyright 2015, Digilent Inc.										*/
/************************************************************************/
/*  Module Description: 												*/
/*																		*/
/*		This file contains code for running a demonstration of the		*/
/*		DMA audio inputs and outputs on the Zybo.					*/
/*																		*/
/*																		*/
/************************************************************************/
/*  Notes:																*/
/*																		*/
/*		- The DMA max burst size needs to be set to 16 or less			*/
/*																		*/
/************************************************************************/
/*  Revision History:													*/
/* 																		*/
/*		9/6/2016(SamL): Created										*/
/*																		*/
/************************************************************************/


#include "demo.h"




#include "audio/audio.h"
#include "dma/dma.h"
#include "intc/intc.h"
#include "userio/userio.h"
#include "iic/iic.h"
#include "ff.h"
/***************************** Include Files *********************************/

#include "xaxidma.h"
#include "xparameters.h"
#include "xil_exception.h"
#include "xdebug.h"
#include "xiic.h"
#include "xaxidma.h"
#include "xtime_l.h"
#include "custom/audio_constants.h"
#include "custom/custom.h"
#include <math.h>


#ifdef XPAR_INTC_0_DEVICE_ID
 #include "xintc.h"
 #include "microblaze_sleep.h"
#else
 #include "xscugic.h"
#include "sleep.h"
#include "xil_cache.h"
#endif

/************************** Constant Definitions *****************************/

/*
 * Device hardware build related constants.
 */

// Audio constants
// Number of seconds to record/playback
//#define NR_SEC_TO_REC_PLAY		20 //For some reason, this is only accurate for 96K Sample Rate

// ADC/DAC sampling rate in Hz
//#define AUDIO_SAMPLING_RATE		1000
//#define AUDIO_SAMPLING_RATE	  48000

#define DATA_BYTES (NR_SEC_TO_REC_PLAY * BYTES_PER_SECOND)

//was 2097152

/* Timeout loop counter for reset
 */
#define RESET_TIMEOUT_COUNTER	10000

#define TEST_START_VALUE	0x0


/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/


/************************** Function Prototypes ******************************/
#if (!defined(DEBUG))
extern void xil_printf(const char *format, ...);
#endif

//prototype - applies reverb effect to recording, with configurable delay and decay
//void basicReverb(recording* buf, int delayMs, float decay);
void apply_reverb(u32 delay_ms, float decay, u32 start_addr);
void apply_echo(u32 delay_ms, float decay, u32 start_addr);

/************************** Variable Definitions *****************************/
/*
 * Device instance definitions
 */

static XIic sIic;
static XAxiDma sAxiDma;		/* Instance of the XAxiDma */

static XGpio sUserIO;

#ifdef XPAR_INTC_0_DEVICE_ID
 static XIntc sIntc;
#else
 static XScuGic sIntc;
#endif

//
// Interrupt vector table
#ifdef XPAR_INTC_0_DEVICE_ID
const ivt_t ivt[] = {
	//IIC
	{XPAR_AXI_INTC_0_AXI_IIC_0_IIC2INTC_IRPT_INTR, (XInterruptHandler)XIic_InterruptHandler, &sIic},
	//DMA Stream to MemoryMap Interrupt handler
	{XPAR_AXI_INTC_0_AXI_DMA_0_S2MM_INTROUT_INTR, (XInterruptHandler)fnS2MMInterruptHandler, &sAxiDma},
	//DMA MemoryMap to Stream Interrupt handler
	{XPAR_AXI_INTC_0_AXI_DMA_0_MM2S_INTROUT_INTR, (XInterruptHandler)fnMM2SInterruptHandler, &sAxiDma},
	//User I/O (buttons, switches, LEDs)
	{XPAR_AXI_INTC_0_AXI_GPIO_0_IP2INTC_IRPT_INTR, (XInterruptHandler)fnUserIOIsr, &sUserIO}
};
#else
const ivt_t ivt[] = {
	//IIC
	{XPAR_FABRIC_AXI_IIC_0_IIC2INTC_IRPT_INTR, (Xil_ExceptionHandler)XIic_InterruptHandler, &sIic},
	//DMA Stream to MemoryMap Interrupt handler
	{XPAR_FABRIC_AXI_DMA_0_S2MM_INTROUT_INTR, (Xil_ExceptionHandler)fnS2MMInterruptHandler, &sAxiDma},
	//DMA MemoryMap to Stream Interrupt handler
	{XPAR_FABRIC_AXI_DMA_0_MM2S_INTROUT_INTR, (Xil_ExceptionHandler)fnMM2SInterruptHandler, &sAxiDma},
	//User I/O (buttons, switches, LEDs)
	{XPAR_FABRIC_AXI_GPIO_0_IP2INTC_IRPT_INTR, (Xil_ExceptionHandler)fnUserIOIsr, &sUserIO}
};
#endif


// SD card stuff
static FATFS FS_instance;
static FRESULT result;
static char* Path = "0:/";

/*****************************************************************************/
/**
*
* Main function
*
* This function is the main entry of the interrupt test. It does the following:
*	Initialize the interrupt controller
*	Initialize the IIC controller
*	Initialize the User I/O driver
*	Initialize the DMA engine
*	Initialize the Audio I2S controller
*	Enable the interrupts
*	Wait for a button event then start selected task
*	Wait for task to complete
*
* @param	None
*
* @return
*		- XST_SUCCESS if example finishes successfully
*		- XST_FAILURE if example fails.
*
* @note		None.
*
******************************************************************************/
int main(void)
{
	int Status;

	Demo.u8Verbose = 0;

	//Xil_DCacheDisable();

	xil_printf("\r\n--- Entering main() --- \r\n");


	//
	//Initialize the interrupt controller

	Status = fnInitInterruptController(&sIntc);
	if(Status != XST_SUCCESS) {
		xil_printf("Error initializing interrupts");
		return XST_FAILURE;
	}


	// Initialize IIC controller
	Status = fnInitIic(&sIic);
	if(Status != XST_SUCCESS) {
		xil_printf("Error initializing I2C controller");
		return XST_FAILURE;
	}

    // Initialize User I/O driver
    Status = fnInitUserIO(&sUserIO);
    if(Status != XST_SUCCESS) {
    	xil_printf("User I/O ERROR");
    	return XST_FAILURE;
    }


	//Initialize DMA
	Status = fnConfigDma(&sAxiDma);
	if(Status != XST_SUCCESS) {
		xil_printf("DMA configuration ERROR");
		return XST_FAILURE;
	}


	//Initialize Audio I2S
	Status = fnInitAudio();
	if(Status != XST_SUCCESS) {
		xil_printf("Audio initializing ERROR");
		return XST_FAILURE;
	}

	{
		XTime  tStart, tEnd;

		XTime_GetTime(&tStart);
		do {
			XTime_GetTime(&tEnd);
		}
		while((tEnd-tStart)/(COUNTS_PER_SECOND/10) < 20);
	}
	//Initialize Audio I2S
	Status = fnInitAudio();
	if(Status != XST_SUCCESS) {
		xil_printf("Audio initializing ERROR");
		return XST_FAILURE;
	}


	// Enable all interrupts in our interrupt vector table
	// Make sure all driver instances using interrupts are initialized first
	fnEnableInterrupts(&sIntc, &ivt[0], sizeof(ivt)/sizeof(ivt[0]));

	recording* recordings;
	recordings = (recording*) malloc(sizeof(recording)*4);
	int i;
	//initialize and zero out recording arrays
	for (i=0; i < 4; i++)
	{
		recordings[i] = (recording) {.arr = {0}};
	}

	// Mount the SD card and load recordings from files
	result = f_mount(&FS_instance, Path, 1);
	xil_printf("Loading recordings from file..\r\n");

	//load recordings from files
	for (i = 0; i < 4; i++)
	{
		loadFromFile(i, (u32)&recordings[i]);
	}

	xil_printf("Done..\r\n");

	// Initializing switches & setting direction
	XGpio dip;
	XGpio_Initialize(&dip, XPAR_SWITCHES_DEVICE_ID);
	XGpio_SetDataDirection(&dip, 1, 0xffffffff);

	int dip_check = XGpio_DiscreteRead(&dip, 1);

	u32 addr = (u32) &recordings[dip_check];

	xil_printf("----------------------------------------------------------\r\n");
	xil_printf("Zybo Z7-20 DMA Audio Demo\r\n");
	xil_printf("----------------------------------------------------------\r\n");
	xil_printf("  Controls:\r\n");
	xil_printf("  BTN0: Play/Pause (only if playing)\r\n");
	xil_printf("  BTN1: Record from LINE IN\r\n");
	xil_printf("  BTN2: Play on HPH OUT\r\n");
	xil_printf("  BTN3: Play on HPH OUT With Effects (Volume Toggle if Already Playing)\r\n");
	xil_printf("----------------------------------------------------------\r\n");

    //main loop
	short  pause = 0;
	short toggle = 0;
    while(1) {
    	//XGpio_DiscreteWrite(&led, 1, dip_check);
		// Checking the DMA S2MM event flag
		if (Demo.fDmaS2MMEvent)
		{
			xil_printf("\r\nRecording Done...");

			// Disable Stream function to send data (S2MM)
			Xil_Out32(I2S_STREAM_CONTROL_REG, 0x00000000);
			Xil_Out32(I2S_TRANSFER_CONTROL_REG, 0x00000000);

			Xil_DCacheInvalidateRange(addr, 5*NR_AUDIO_SAMPLES);
			//microblaze_invalidate_dcache();
			// Reset S2MM event and record flag
			Demo.fDmaS2MMEvent = 0;
			Demo.fAudioRecord = 0;

			writeToFile(dip_check, addr);
		}

		// Checking the DMA MM2S event flag
		if (Demo.fDmaMM2SEvent)
		{
			xil_printf("\r\nPlayback Done...");

			// Disable Stream function to send data (S2MM)
			Xil_Out32(I2S_STREAM_CONTROL_REG, 0x00000000);
			Xil_Out32(I2S_TRANSFER_CONTROL_REG, 0x00000000);
			//Flush cache
			Xil_DCacheFlushRange(addr, 1*5*NR_AUDIO_SAMPLES);
			//Reset MM2S event and playback flag
			Demo.fDmaMM2SEvent = 0;
			Demo.fAudioPlayback = 0;
		}

		// Checking the DMA Error event flag
		if (Demo.fDmaError)
		{
			xil_printf("\r\nDma Error...");
			xil_printf("\r\nDma Reset...");


			Demo.fDmaError = 0;
			Demo.fAudioPlayback = 0;
			Demo.fAudioRecord = 0;
		}

		// Checking the btn change event
		if(Demo.fUserIOEvent) {

			switch(Demo.chBtn) {
				case 'u': //button 1 - recording
					xil_printf("\r\n case u \r\n");
					if (!Demo.fAudioRecord && !Demo.fAudioPlayback)
					{

						dip_check = XGpio_DiscreteRead(&dip, 1);
						addr = (u32) &recordings[dip_check];

						xil_printf("\r\nStart Recording...\r\n");

//						fnSetMicInput(); //record from mic input
						fnSetLineInput(); //record from line input


						fnAudioRecord(sAxiDma,1*NR_AUDIO_SAMPLES, addr);
						Demo.fAudioRecord = 1;
					}
					else
					{
						if (Demo.fAudioRecord)
						{
							xil_printf("\r\nStill Recording...\r\n");
						}
						else
						{
							xil_printf("\r\nStill Playing back...\r\n");
						}
					}
					break;
				case 'd': //button 2 - playback
					xil_printf("\r\n case d \r\n");
					if (!Demo.fAudioRecord && !Demo.fAudioPlayback)
					{
						dip_check = XGpio_DiscreteRead(&dip, 1);

						addr = (u32) &recordings[dip_check];
						xil_printf("\r\nStart Play back...\r\n");
						fnSetHpOutput();
						fnAudioPlay(sAxiDma,1*NR_AUDIO_SAMPLES, addr);
						Demo.fAudioPlayback = 1;
					}
					else
					{
						if (Demo.fAudioRecord)
						{
							xil_printf("\r\nStill Recording...\r\n");
						}
						else
						{
							xil_printf("\r\nStill Playing back...\r\n");
						}
					}
					break;
				case 'r': // button 3 - playback with effects applied, or volume toggle if already playing
//					***** VOLUME TOGGLE - ONLY WORKS WHEN AUDIO IS PLAYING *****
					if(Demo.fAudioPlayback && !Demo.fAudioRecord) {
					xil_printf("\r\n adjusting volume \r\n");
					if (!toggle) {
							fnAudioWriteToReg(R0_LEFT_ADC_VOL, 0b000011111);
							fnAudioWriteToReg(R1_RIGHT_ADC_VOL, 0b000011111);
							fnAudioWriteToReg(R2_LEFT_DAC_VOL, 0b101111111);
							fnAudioWriteToReg(R3_RIGHT_DAC_VOL, 0b101111111);
							toggle = 1;
						} else {
							fnAudioWriteToReg(R0_LEFT_ADC_VOL, 0b000010111);
							fnAudioWriteToReg(R1_RIGHT_ADC_VOL, 0b000010111);
							fnAudioWriteToReg(R2_LEFT_DAC_VOL, 0b101111001);
							fnAudioWriteToReg(R3_RIGHT_DAC_VOL, 0b101111001);
							toggle = 0;
						}
					}
					else {
	//					*** Play currently selected track with reverb and echo effect ***
	//					track # chosen w/ dip switches
						xil_printf("\r\n Filtering Recorded Audio and Playing... \r\n");
						dip_check = XGpio_DiscreteRead(&dip, 1);
						if(!Demo.fAudioRecord && !Demo.fAudioPlayback) {
	//						allocate new recording array
							recording* tempRecording = (recording*) malloc(sizeof(recording));
	//						copy recording stored at recordings[dipswitch value] into tempRecording
							memcpy(tempRecording, &recordings[dip_check], sizeof(recording));

							addr = (u32)tempRecording;
	//						effects - comment out to disable an effect, or leave uncommented to apply both
							apply_echo(500, 0.5, addr);
							apply_reverb(100, 0.75, addr);

							//enable HPH out and play audio
							fnSetHpOutput(); //headphone out
							fnAudioPlay(sAxiDma,1*NR_AUDIO_SAMPLES, addr);
							Demo.fAudioPlayback = 1;
							//free allocated temp recording
							free(tempRecording);
						}
						else
						{
							if (Demo.fAudioRecord)
							{
								xil_printf("\r\nStill Recording...\r\n");
							}
							else
							{
								xil_printf("\r\nStill Playing back...\r\n");
							}
						}
					}
					break;
				case 'l': // button 2 - Playback
					xil_printf("\r\n case l \r\n");
					if (!Demo.fAudioRecord && !Demo.fAudioPlayback)
					{

						dip_check = XGpio_DiscreteRead(&dip, 1);

						addr = (u32) &recordings[dip_check];
						xil_printf("\r\nStart Playback...");
						xil_printf("\r\nPlay back audio %d \r\n", dip_check);

//						fnSetLineOutput();
						fnSetHpOutput();
						fnAudioPlay(sAxiDma, NR_AUDIO_SAMPLES, addr);
						Demo.fAudioPlayback = 1;
					}
					else
					{
						if (Demo.fAudioRecord)
						{
							xil_printf("\r\nStill Recording...\r\n");
						}
						else
						{
							xil_printf("\r\nStill Playing back...\r\n");
						}
					}
					break;
				case 'c': // button 0 - plays/pauses currently playing audio from SD card

					if (!pause) {
						// Disable Stream function to send data (S2MM)
						Xil_Out32(I2S_STREAM_CONTROL_REG, 0x00000000);
						Xil_Out32(I2S_TRANSFER_CONTROL_REG, 0x00000000);
						//Flush cache
						Xil_DCacheFlushRange(addr, 5*NR_AUDIO_SAMPLES);
						//Reset MM2S event and playback flag
						Demo.fDmaMM2SEvent = 0;
						Demo.fAudioPlayback = 0;
						pause = 1;
					} else {
//						fnSetLineOutput();
						fnSetHpOutput();
						fnAudioPlay(sAxiDma,NR_AUDIO_SAMPLES, addr);
						Demo.fAudioPlayback = 1;
						pause = 0;
					}
					break;
				default:
					xil_printf("\r\nButton pressed is %d\r\n",Demo.chBtn);
					break;
			}

			// Reset the user I/O flag
			Demo.chBtn = 0;
			Demo.fUserIOEvent = 0;
		}

    }

	xil_printf("\r\n--- Exiting main() --- \r\n");


	return XST_SUCCESS;

}

void writeToFile(int recordingIndex, u32 addr)
{
	FIL file1;
	char FileName[32];
	char* FileNamePtr;

	unsigned int bytesWr;
	uint recordingSize = RECORDING_SIZE_BYTES;

	sprintf(FileName, "%d.txt", recordingIndex);
	FileNamePtr = (char *)FileName;
	result = f_open(&file1, FileNamePtr, FA_CREATE_ALWAYS | FA_WRITE);

	result = f_write(&file1, (const void*)addr, recordingSize, &bytesWr);

	result = f_close(&file1);
}

void loadFromFile(int recordingIndex, u32 addr)
{
	unsigned int bytesR;
	uint recordingSize = RECORDING_SIZE_BYTES;

	FIL file1;
	char FileName[32];
	char* FileNamePtr;

	sprintf(FileName, "%d.txt", recordingIndex);
	FileNamePtr = (char *)FileName;
	result = f_open(&file1, FileNamePtr, FA_READ);
	result = f_read(&file1, (void*)addr, recordingSize, &bytesR);
	result = f_close(&file1);
}


//applies reverb effect to audio at start_addr using params delay_ms and decay
void apply_reverb(u32 delay_ms, float decay, u32 start_addr) {
//	don't modify to first and last 10% of audio to avoid edge effects and buffer overflows
	u32 edge_skip_bytes = BYTES_PER_SECOND / 10;
	u32 delay_bytes = BYTES_PER_SECOND * delay_ms / 1000;
	//	apply reverb to rest of audio - iterate through audio forwards
	for (u32 i = edge_skip_bytes; i < DATA_BYTES - edge_skip_bytes - delay_bytes; i += BYTES_PER_SAMPLE) {
//		get read and write addresses
		UINTPTR read_addr = start_addr + i;
		UINTPTR write_addr = read_addr + delay_bytes;
//		put samples at read and write addr in a & b
		u32 a = Xil_In32(read_addr);
		u32 b = Xil_In32(write_addr);
//		overflow prevention
		if (a > 4 * 1e6 || b > 4 * 1e6) {
			continue;
		}
		//
		//add original delayed sample to new decayed current sample
		u32 c_out = (a * decay) + b;
		Xil_Out32(write_addr, c_out);
	}
}

//applies echo effect to audio at start_addr using params delay_ms and decay
void apply_echo(u32 delay_ms, float decay, u32 start_addr) {
//	don't modify to first and last 10% of audio to avoid edge effects and buffer overflows
	u32 edge_skip_bytes = BYTES_PER_SECOND / 10;
	u32 delay_bytes = BYTES_PER_SECOND * delay_ms / 1000;
//	apply echo to rest of audio - iterate through audio backwards
	for (u32 i = DATA_BYTES - edge_skip_bytes - delay_bytes; i > edge_skip_bytes; i -= BYTES_PER_SAMPLE) {
//		get read and write addresses
		UINTPTR read_addr = start_addr + i;
		UINTPTR write_addr = read_addr + delay_bytes;
//		put samples at read and write addr in a & b
		u32 a = Xil_In32(read_addr);
		u32 b = Xil_In32(write_addr);
//		prevent overflows
		if (a > 4 * 1e6 || b > 4 * 1e6) {
			continue;
		}
//		add original delayed sample 'b' to decayed current sample 'b'
		u32 c = (a * decay) + b;
//		write modified sample to memory
		Xil_Out32(write_addr, c);
	}
}






