/*
 * Looks up IOGOPFramebuffer, opens a user client connection, maps its VRAM
 * (kIOFBVRAMMemory, from IOGraphicsTypesPrivate.h), and draws a plain fill +
 * a triangle into it - same drawing code as fbtri.c, just backed by a real
 * IOKit-mapped framebuffer instead of a /dev/fb0 file.
 */
#include <PDGOP.h>
#include <mach/mach.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int
edge(int ax, int ay, int bx, int by, int px, int py)
{
	return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static void
put_pixel(uint8_t *base, uint32_t stride, uint32_t bpp, uint32_t width,
    uint32_t height, uint32_t x, uint32_t y, uint32_t color)
{
	uint8_t *p;

	if (x >= width || y >= height) {
		return;
	}

	p = base + ((size_t)y * stride) + ((size_t)x * (bpp / 8));

	if (bpp == 32) {
		p[0] = (uint8_t)(color >> 0);
		p[1] = (uint8_t)(color >> 8);
		p[2] = (uint8_t)(color >> 16);
		p[3] = (uint8_t)(color >> 24);
	} else if (bpp == 16) {
		uint16_t r = (uint16_t)((color >> 19) & 0x1f);
		uint16_t g = (uint16_t)((color >> 10) & 0x3f);
		uint16_t b = (uint16_t)((color >> 3) & 0x1f);
		uint16_t rgb565 = (uint16_t)((r << 11) | (g << 5) | b);
		p[0] = (uint8_t)(rgb565 >> 0);
		p[1] = (uint8_t)(rgb565 >> 8);
	}
}

static void
draw(uint8_t *base, uint32_t stride, uint32_t bpp, uint32_t width, uint32_t height)
{
	int ax = (int)(width / 2), ay = (int)(height / 6);
	int bx = (int)(width / 5), by = (int)((height * 5) / 6);
	int cx = (int)((width * 4) / 5), cy = by;
	int min_x = bx < ax ? bx : ax, max_x = cx > ax ? cx : ax;
	int min_y = ay < by ? ay : by, max_y = cy > ay ? cy : ay;
	int area = edge(ax, ay, bx, by, cx, cy);
	int x, y;
	uint32_t px, py;

	for (py = 0; py < height; py++) {
		for (px = 0; px < width; px++) {
			put_pixel(base, stride, bpp, width, height, px, py, 0xff182028u);
		}
	}

	if (area == 0) {
		return;
	}

	for (y = min_y; y <= max_y; y++) {
		for (x = min_x; x <= max_x; x++) {
			int w0 = edge(bx, by, cx, cy, x, y);
			int w1 = edge(cx, cy, ax, ay, x, y);
			int w2 = edge(ax, ay, bx, by, x, y);

			if ((w0 >= 0 && w1 >= 0 && w2 >= 0) ||
			    (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
				int aabs = area < 0 ? -area : area;
				uint32_t r = (uint32_t)((w0 < 0 ? -w0 : w0) * 255 / aabs);
				uint32_t g = (uint32_t)((w1 < 0 ? -w1 : w1) * 255 / aabs);
				uint32_t b = (uint32_t)((w2 < 0 ? -w2 : w2) * 255 / aabs);
				put_pixel(base, stride, bpp, width, height, (uint32_t)x,
				    (uint32_t)y, 0xff000000u | (r << 16) | (g << 8) | b);
			}
		}
	}
}

int
main(void)
{
	kern_return_t kr;
	PDGOPFramebuffer fb;

	kr = PDGOPOpen(&fb);
	if (kr != KERN_SUCCESS) {
		fprintf(stderr, "iokittest: PDGOPOpen failed at %s: 0x%x\n",
		    PDGOPLastErrorStage(), kr);
		return 1;
	}

	printf("iokittest: mapped IOGOPFramebuffer %ux%u stride=%u bpp=%u "
	    "addr=0x%llx size=%llu masks=%08x/%08x/%08x\n",
	    fb.width, fb.height, fb.stride, fb.bpp,
	    (unsigned long long)fb.address, (unsigned long long)fb.size,
	    fb.componentMasks[0], fb.componentMasks[1], fb.componentMasks[2]);

	if (fb.height == 0 || fb.stride == 0 || fb.bpp == 0 ||
	    fb.size < (mach_vm_size_t)fb.stride * fb.height) {
		fprintf(stderr, "iokittest: invalid framebuffer geometry\n");
		PDGOPClose(&fb);
		return 1;
	}

	draw((uint8_t *)(uintptr_t)fb.address, fb.stride, fb.bpp,
	    fb.width, fb.height);
	printf("iokittest: drew %ux%u %u-bpp triangle into IOKit-mapped VRAM\n",
	    fb.width, fb.height, fb.bpp);

	PDGOPClose(&fb);
	return 0;
}
