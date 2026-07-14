#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

struct framebuffer {
    uint8_t *base;
    size_t size;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t bpp;
};

static uint32_t
env_u32(const char *name, uint32_t fallback)
{
    const char *value = getenv(name);
    char *end = NULL;
    unsigned long parsed;

    if (!value || !*value) {
        return fallback;
    }

    parsed = strtoul(value, &end, 0);
    if (!end || *end || parsed == 0 || parsed > UINT32_MAX) {
        fprintf(stderr, "fbtri: ignoring invalid %s=%s\n", name, value);
        return fallback;
    }

    return (uint32_t)parsed;
}

static int
edge(int ax, int ay, int bx, int by, int px, int py)
{
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static void
put_pixel(struct framebuffer *fb, uint32_t x, uint32_t y, uint32_t color)
{
    uint8_t *p;

    if (x >= fb->width || y >= fb->height) {
        return;
    }

    p = fb->base + ((size_t)y * fb->stride) + ((size_t)x * (fb->bpp / 8));

    if (fb->bpp == 32) {
        p[0] = (uint8_t)(color >> 0);
        p[1] = (uint8_t)(color >> 8);
        p[2] = (uint8_t)(color >> 16);
        p[3] = (uint8_t)(color >> 24);
    } else if (fb->bpp == 24) {
        p[0] = (uint8_t)(color >> 0);
        p[1] = (uint8_t)(color >> 8);
        p[2] = (uint8_t)(color >> 16);
    } else if (fb->bpp == 16) {
        uint16_t r = (uint16_t)((color >> 19) & 0x1f);
        uint16_t g = (uint16_t)((color >> 10) & 0x3f);
        uint16_t b = (uint16_t)((color >> 3) & 0x1f);
        uint16_t rgb565 = (uint16_t)((r << 11) | (g << 5) | b);
        p[0] = (uint8_t)(rgb565 >> 0);
        p[1] = (uint8_t)(rgb565 >> 8);
    }
}

static void
fill_rect(struct framebuffer *fb, uint32_t color)
{
    uint32_t x;
    uint32_t y;

    for (y = 0; y < fb->height; y++) {
        for (x = 0; x < fb->width; x++) {
            put_pixel(fb, x, y, color);
        }
    }
}

static void
draw_triangle(struct framebuffer *fb)
{
    int ax = (int)(fb->width / 2);
    int ay = (int)(fb->height / 6);
    int bx = (int)(fb->width / 5);
    int by = (int)((fb->height * 5) / 6);
    int cx = (int)((fb->width * 4) / 5);
    int cy = by;
    int min_x = bx < ax ? bx : ax;
    int max_x = cx > ax ? cx : ax;
    int min_y = ay < by ? ay : by;
    int max_y = cy > ay ? cy : ay;
    int area = edge(ax, ay, bx, by, cx, cy);
    int x;
    int y;

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
                uint32_t r = (uint32_t)((w0 < 0 ? -w0 : w0) * 255 / (area < 0 ? -area : area));
                uint32_t g = (uint32_t)((w1 < 0 ? -w1 : w1) * 255 / (area < 0 ? -area : area));
                uint32_t b = (uint32_t)((w2 < 0 ? -w2 : w2) * 255 / (area < 0 ? -area : area));
                put_pixel(fb, (uint32_t)x, (uint32_t)y, 0xff000000u | (r << 16) | (g << 8) | b);
            }
        }
    }
}

static int
write_all(int fd, const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;

    while (len) {
        ssize_t wrote = write(fd, p, len);
        if (wrote < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        p += wrote;
        len -= (size_t)wrote;
    }

    return 0;
}

int
main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : getenv("FBTRI_DEVICE");
    struct framebuffer fb;
    int fd;
    int mapped = 0;

    if (!path || !*path) {
        path = "/dev/fb0";
    }

    memset(&fb, 0, sizeof(fb));
    fb.width = env_u32("FBTRI_WIDTH", 1024);
    fb.height = env_u32("FBTRI_HEIGHT", 768);
    fb.bpp = env_u32("FBTRI_BPP", 32);
    if (fb.bpp != 16 && fb.bpp != 24 && fb.bpp != 32) {
        fprintf(stderr, "fbtri: unsupported bpp %u\n", fb.bpp);
        return 2;
    }

    fb.stride = env_u32("FBTRI_STRIDE", fb.width * (fb.bpp / 8));
    fb.size = (size_t)fb.stride * fb.height;

    fd = open(path, O_RDWR, 0);
    if (fd < 0) {
        fprintf(stderr, "fbtri: open %s failed: %s\n", path, strerror(errno));
        return 1;
    }

    fb.base = mmap(NULL, fb.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fb.base != MAP_FAILED) {
        mapped = 1;
    } else {
        fb.base = malloc(fb.size);
        if (!fb.base) {
            fprintf(stderr, "fbtri: allocating %lu bytes failed\n", (unsigned long)fb.size);
            close(fd);
            return 1;
        }
    }

    fill_rect(&fb, 0xff182028u);
    draw_triangle(&fb);

    if (!mapped) {
        lseek(fd, 0, SEEK_SET);
        if (write_all(fd, fb.base, fb.size) != 0) {
            fprintf(stderr, "fbtri: write %s failed: %s\n", path, strerror(errno));
            free(fb.base);
            close(fd);
            return 1;
        }
        free(fb.base);
    }

    close(fd);
    printf("fbtri: drew %ux%u %u-bpp triangle to %s\n", fb.width, fb.height, fb.bpp, path);
    return 0;
}
