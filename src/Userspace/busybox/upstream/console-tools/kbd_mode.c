/* vi: set sw=4 ts=4: */
/*
 * Mini kbd_mode implementation for busybox
 *
 * Copyright (C) 2007 Loic Grenie <loic.grenie@gmail.com>
 *   written using Andries Brouwer <aeb@cwi.nl>'s kbd_mode from
 *   console-utils v0.2.3, licensed under GNU GPLv2
 *
 * Darwin termios port.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config KBD_MODE
//config:	bool "kbd_mode (4.3 kb)"
//config:	default y
//config:	help
//config:	This program reports and sets keyboard mode.

//applet:IF_KBD_MODE(APPLET_NOEXEC(kbd_mode, kbd_mode, BB_DIR_BIN, BB_SUID_DROP, kbd_mode))

//kbuild:lib-$(CONFIG_KBD_MODE) += kbd_mode.o

//usage:#define kbd_mode_trivial_usage
//usage:       "[-a|k|s|u] [-C TTY]"
//usage:#define kbd_mode_full_usage "\n\n"
//usage:       "Report or set terminal keyboard mode\n"
//usage:     "\n	-a	Default (ASCII/canonical)"
//usage:     "\n	-k	Medium-raw (keycode-like)"
//usage:     "\n	-s	Raw (byte stream)"
//usage:     "\n	-u	Unicode (UTF-8/canonical)"
//usage:     "\n	-C TTY	Affect TTY"

#include "libbb.h"

#if defined(__linux__)

# include <linux/kd.h>

static int linux_kbd_mode(unsigned opt, int fd)
{
	enum {
		SCANCODE  = (1 << 0),
		ASCII     = (1 << 1),
		MEDIUMRAW = (1 << 2),
		UNICODE   = (1 << 3),
	};

	if (!opt) {
		const char *mode = "unknown";
		int m;

		xioctl(fd, KDGKBMODE, &m);

		if (m == K_RAW)
			mode = "raw (scancode)";
		else if (m == K_XLATE)
			mode = "default (ASCII)";
		else if (m == K_MEDIUMRAW)
			mode = "mediumraw (keycode)";
		else if (m == K_UNICODE)
			mode = "Unicode (UTF-8)";
		else if (m == 4 /* K_OFF */)
			mode = "off";

		printf("The keyboard is in %s mode\n", mode);
	} else {
		/*
		 * KDSKBMODE values correspond to the option-bit arrangement:
		 *
		 * K_RAW       = 0
		 * K_XLATE     = 1
		 * K_MEDIUMRAW = 2
		 * K_UNICODE   = 3
		 */
		opt = opt & UNICODE ? 3 : opt >> 1;
		xioctl(fd, KDSKBMODE, (void *)(ptrdiff_t)opt);
	}

	return EXIT_SUCCESS;
}

#else

# include <termios.h>

enum {
	KBD_SCANCODE  = (1 << 0),
	KBD_ASCII     = (1 << 1),
	KBD_MEDIUMRAW = (1 << 2),
	KBD_UNICODE   = (1 << 3),
};

static void get_termios_or_die(int fd, struct termios *tio)
{
	if (tcgetattr(fd, tio) != 0)
		bb_perror_msg_and_die("tcgetattr");
}

static void set_termios_or_die(int fd, const struct termios *tio)
{
	if (tcsetattr(fd, TCSANOW, tio) != 0)
		bb_perror_msg_and_die("tcsetattr");
}

/*
 * Darwin does not expose Linux VT keyboard translation modes.
 *
 * Approximate them with terminal input modes:
 *
 *   ASCII / Unicode:
 *       canonical, cooked terminal input
 *
 *   Medium-raw:
 *       noncanonical input, but retain ISIG so interrupt/suspend keys work
 *
 *   Scancode/raw:
 *       cfmakeraw() byte-stream input
 */
static int darwin_kbd_mode(unsigned opt, int fd)
{
	struct termios tio;

	get_termios_or_die(fd, &tio);

	if (!opt) {
		const char *mode;

		if (tio.c_lflag & ICANON) {
			/*
			 * Darwin has no separate ASCII versus Unicode keyboard
			 * translation mode. Canonical input is reported as the
			 * default mode.
			 */
			mode = "default (ASCII/UTF-8)";
		} else if (tio.c_lflag & ISIG) {
			mode = "mediumraw (keycode-like)";
		} else {
			mode = "raw (byte stream)";
		}

		printf("The keyboard is in %s mode\n", mode);
		return EXIT_SUCCESS;
	}

	/*
	 * getopt32 allows combinations, but retain the upstream precedence:
	 * Unicode wins, followed by medium-raw, ASCII, then scancode.
	 */
	if (opt & KBD_UNICODE) {
		/*
		 * Restore normal cooked input. IUTF8 is not universally
		 * available on Darwin-derived systems, so do not require it.
		 */
		tio.c_iflag |= BRKINT | ICRNL | IXON;
		tio.c_iflag &= ~(IGNBRK | IGNCR | INLCR | IXOFF);

		tio.c_oflag |= OPOST;

		tio.c_lflag |= ICANON | ISIG | IEXTEN | ECHO;
		tio.c_lflag |= ECHOE | ECHOK;

		tio.c_cflag |= CREAD;
		tio.c_cflag &= ~(CSIZE | PARENB);
		tio.c_cflag |= CS8;

		tio.c_cc[VMIN] = 1;
		tio.c_cc[VTIME] = 0;
	} else if (opt & KBD_MEDIUMRAW) {
		/*
		 * Deliver bytes immediately while preserving terminal signals
		 * such as VINTR, VQUIT, and VSUSP.
		 */
		tio.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
		tio.c_oflag &= ~OPOST;

		tio.c_cflag |= CREAD;
		tio.c_cflag &= ~(CSIZE | PARENB);
		tio.c_cflag |= CS8;

		tio.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN);
		tio.c_lflag |= ISIG;

		tio.c_cc[VMIN] = 1;
		tio.c_cc[VTIME] = 0;
	} else if (opt & KBD_ASCII) {
		tio.c_iflag |= BRKINT | ICRNL | IXON;
		tio.c_iflag &= ~(IGNBRK | IGNCR | INLCR | IXOFF);

		tio.c_oflag |= OPOST;

		tio.c_lflag |= ICANON | ISIG | IEXTEN | ECHO;
		tio.c_lflag |= ECHOE | ECHOK;

		tio.c_cflag |= CREAD;
		tio.c_cflag &= ~(CSIZE | PARENB);
		tio.c_cflag |= CS8;

		tio.c_cc[VMIN] = 1;
		tio.c_cc[VTIME] = 0;
	} else {
		/*
		 * There are no hardware keyboard scancodes available through
		 * a Darwin tty. The closest useful equivalent is raw terminal
		 * byte input.
		 */
		cfmakeraw(&tio);
		tio.c_cc[VMIN] = 1;
		tio.c_cc[VTIME] = 0;
	}

	set_termios_or_die(fd, &tio);
	return EXIT_SUCCESS;
}

#endif

int kbd_mode_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int kbd_mode_main(int argc UNUSED_PARAM, char **argv)
{
	int fd;
	unsigned opt;
	const char *tty_name = NULL;

	opt = getopt32(argv, "sakuC:", &tty_name);

	if (opt & (1 << 4)) {
		/*
		 * Clear the -C bit, preserving only the four mode bits.
		 */
		opt &= 0x0f;
		fd = xopen_nonblocking(tty_name);
	} else {
#if defined(__linux__)
		fd = get_console_fd_or_die();
#else
		/*
		 * get_console_fd_or_die() is Linux-console-oriented and may
		 * probe /dev/tty0 or issue Linux-specific ioctls. On Darwin,
		 * use the controlling terminal directly.
		 */
		fd = open("/dev/tty", O_RDWR | O_NONBLOCK);

		if (fd < 0) {
			if (isatty(STDIN_FILENO))
				fd = STDIN_FILENO;
			else if (isatty(STDOUT_FILENO))
				fd = STDOUT_FILENO;
			else if (isatty(STDERR_FILENO))
				fd = STDERR_FILENO;
			else
				bb_simple_perror_msg_and_die("/dev/tty");
		}
#endif
	}

#if defined(__linux__)
	linux_kbd_mode(opt, fd);
#else
	darwin_kbd_mode(opt, fd);
#endif

	if (ENABLE_FEATURE_CLEAN_UP
	 && fd != STDIN_FILENO
	 && fd != STDOUT_FILENO
	 && fd != STDERR_FILENO
	) {
		close(fd);
	}

	return EXIT_SUCCESS;
}