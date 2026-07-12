#include <stdint.h>
#include <mach/mach_types.h>
#include <sys/systm.h>
#include <libkern/libkern.h>

#define MINI_RV32_RAM_SIZE 4096
#define MINI_RV32_UART_ADDR 0x10000000U
#define MINI_RV32_SERIAL_SIZE 128

static uint32_t g_minirv32_mmio_addr;
static uint32_t g_minirv32_mmio_value;
static uint32_t g_minirv32_mmio_count;
static uint8_t g_minirv32_image[MINI_RV32_RAM_SIZE];
static char g_minirv32_serial[MINI_RV32_SERIAL_SIZE];
static uint32_t g_minirv32_serial_len;

static const uint32_t g_minirv32_uart_demo[] = {
	0x10000137, /* lui  x2, 0x10000     ; UART @ 0x10000000 */
	0x04800093, /* addi x1, x0, 'H' */
	0x00110023, /* sb   x1, 0(x2) */
	0x06500093, /* addi x1, x0, 'e' */
	0x00110023,
	0x06c00093, /* addi x1, x0, 'l' */
	0x00110023,
	0x06c00093, /* addi x1, x0, 'l' */
	0x00110023,
	0x06f00093, /* addi x1, x0, 'o' */
	0x00110023,
	0x02000093, /* addi x1, x0, ' ' */
	0x00110023,
	0x06600093, /* addi x1, x0, 'f' */
	0x00110023,
	0x07200093, /* addi x1, x0, 'r' */
	0x00110023,
	0x06f00093, /* addi x1, x0, 'o' */
	0x00110023,
	0x06d00093, /* addi x1, x0, 'm' */
	0x00110023,
	0x02000093, /* addi x1, x0, ' ' */
	0x00110023,
	0x05200093, /* addi x1, x0, 'R' */
	0x00110023,
	0x05600093, /* addi x1, x0, 'V' */
	0x00110023,
	0x03300093, /* addi x1, x0, '3' */
	0x00110023,
	0x03200093, /* addi x1, x0, '2' */
	0x00110023,
	0x00a00093, /* addi x1, x0, '\n' */
	0x00110023,
	0x0000006f, /* jal  x0, 0           ; park */
};

static void
minirv32_store32(uint8_t *image, uint32_t offset, uint32_t value)
{
	image[offset + 0] = (uint8_t)(value >> 0);
	image[offset + 1] = (uint8_t)(value >> 8);
	image[offset + 2] = (uint8_t)(value >> 16);
	image[offset + 3] = (uint8_t)(value >> 24);
}

static void
minirv32_zero(void *ptr, uint32_t len)
{
	uint8_t *bytes = ptr;
	uint32_t i;

	for (i = 0; i < len; i++) {
		bytes[i] = 0;
	}
}

static void
minirv32_uart_write(uint32_t addy, uint32_t value)
{
	g_minirv32_mmio_addr = addy;
	g_minirv32_mmio_value = value;
	g_minirv32_mmio_count++;

	if (addy != MINI_RV32_UART_ADDR) {
		return;
	}

	if (g_minirv32_serial_len + 1 < sizeof(g_minirv32_serial)) {
		g_minirv32_serial[g_minirv32_serial_len++] = (char)(value & 0xff);
		g_minirv32_serial[g_minirv32_serial_len] = '\0';
	}
}

#define MINIRV32WARN(...)
#define MINIRV32_HANDLE_MEM_STORE_CONTROL(addy, val) do { \
	minirv32_uart_write((addy), (val)); \
} while (0)
#define MINIRV32_HANDLE_MEM_LOAD_CONTROL(addy, rval) do { \
	(void)(addy); \
	(rval) = 0; \
} while (0)
#define MINIRV32_IMPLEMENTATION
#include "mini-rv32ima.h"

static int
minirv32_smoke_test(void)
{
	struct MiniRV32IMAState state;
	uint32_t i;

	minirv32_zero(g_minirv32_image, sizeof(g_minirv32_image));
	minirv32_zero(&state, sizeof(state));
	minirv32_zero(g_minirv32_serial, sizeof(g_minirv32_serial));
	g_minirv32_mmio_addr = 0;
	g_minirv32_mmio_value = 0;
	g_minirv32_mmio_count = 0;
	g_minirv32_serial_len = 0;

	for (i = 0; i < sizeof(g_minirv32_uart_demo) / sizeof(g_minirv32_uart_demo[0]); i++) {
		minirv32_store32(g_minirv32_image, i * sizeof(uint32_t), g_minirv32_uart_demo[i]);
	}

	state.pc = MINIRV32_RAM_IMAGE_OFFSET;
	state.extraflags = 3;
	state.mstatus = 0;

	(void)MiniRV32IMAStep(&state, g_minirv32_image, 0, 0, 128);

	if (g_minirv32_mmio_count == 0 ||
	    g_minirv32_mmio_addr != MINI_RV32_UART_ADDR ||
	    g_minirv32_serial_len == 0) {
		printf("MiniRV32IMA: smoke test failed count=%u addr=0x%x value=%u serial_len=%u pc=0x%x\n",
		    g_minirv32_mmio_count, g_minirv32_mmio_addr,
		    g_minirv32_mmio_value, g_minirv32_serial_len, state.pc);
		return KERN_FAILURE;
	}

	printf("MiniRV32IMA: serial: %s", g_minirv32_serial);
	printf("MiniRV32IMA: rv32ima smoke test ok pc=0x%x stores=%u last[0x%x]=0x%x\n",
	    state.pc, g_minirv32_mmio_count, g_minirv32_mmio_addr, g_minirv32_mmio_value);
	return KERN_SUCCESS;
}

kern_return_t
MiniRV32IMA_start(kmod_info_t *ki, void *d)
{
	(void)ki;
	(void)d;
	return minirv32_smoke_test();
}

kern_return_t
MiniRV32IMA_stop(kmod_info_t *ki, void *d)
{
	(void)ki;
	(void)d;
	printf("MiniRV32IMA: stop\n");
	return KERN_SUCCESS;
}
