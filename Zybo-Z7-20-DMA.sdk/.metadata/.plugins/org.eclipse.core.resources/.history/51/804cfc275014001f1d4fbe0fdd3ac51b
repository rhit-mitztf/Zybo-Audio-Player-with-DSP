#include "custom.h"

#include "audio_constants.h"
#include "../demo.h"

#include <math.h>


// should be divisible by 1000 to ensure sample bytes alignment
#define BYTES_PER_SECOND (NR_CHANNELS * BYTES_PER_SAMPLE * AUDIO_SAMPLING_RATE)

#define DATA_BYTES (NR_SEC_TO_REC_PLAY * BYTES_PER_SECOND)


void apply_reverb(u32 delay_ms, float decay) {

	u32 edge_skip_bytes = BYTES_PER_SECOND / 10;
	u32 delay_bytes = BYTES_PER_SECOND * delay_ms / 1000;

	for (u32 i = edge_skip_bytes; i < DATA_BYTES - edge_skip_bytes - delay_bytes; i += BYTES_PER_SAMPLE) {

		UINTPTR read_addr = MEM_BASE_ADDR + i;
		UINTPTR write_addr = read_addr + delay_bytes;

		u32 a = Xil_In32(read_addr);
		u32 b = Xil_In32(write_addr);

		if (a > 4 * 1e6 || b > 4 * 1e6) {
			continue;
		}

		u32 c_out = (a * decay) + b;

		Xil_Out32(write_addr, c_out);

	}

}

void apply_echo(u32 delay_ms, float decay) {

	u32 edge_skip_bytes = BYTES_PER_SECOND / 10;
	u32 delay_bytes = BYTES_PER_SECOND * delay_ms / 1000;

	for (u32 i = DATA_BYTES - edge_skip_bytes - delay_bytes; i > edge_skip_bytes; i -= BYTES_PER_SAMPLE) {

		UINTPTR read_addr = MEM_BASE_ADDR + i;
		UINTPTR write_addr = read_addr + delay_bytes;

		u32 a = Xil_In32(read_addr);
		u32 b = Xil_In32(write_addr);

		if (a > 4 * 1e6 || b > 4 * 1e6) {
			continue;
		}

		u32 c = (a * decay) + b;

		Xil_Out32(write_addr, c);

	}

}


void process_buffer(void) {

	xil_printf("\r\n");
	xil_printf("Starting\r\n");

	apply_echo(500, 0.5);

	xil_printf("Done\r\n");

}
