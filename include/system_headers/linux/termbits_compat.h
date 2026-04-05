/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Linux-specific termios constants not available on all POSIX platforms.
 * On Linux, these come from <asm-generic/termbits.h> via <termios.h>.
 * On other platforms (e.g., macOS/BSD), we provide fallback definitions
 * for the constants that are missing. Each is guarded with #ifndef to
 * avoid conflicts with any platform-provided definitions.
 *
 * Include this header AFTER <termios.h>.
 */

#pragma once

/* Control character indices */
#ifndef VSWTC
#define VSWTC    7
#endif

/* Input flags */
#ifndef IUCLC
#define IUCLC    0001000
#endif

#ifndef IMAXBEL
#define IMAXBEL  0020000
#endif

#ifndef IUTF8
#define IUTF8    0040000
#endif

/* Output flags */
#ifndef OLCUC
#define OLCUC    0000002
#endif

#ifndef NLDLY
#define NLDLY    0000400
#endif

#ifndef CRDLY
#define CRDLY    0003000
#endif

#ifndef TABDLY
#define TABDLY   0014000
#endif

#ifndef BSDLY
#define BSDLY    0020000
#endif

#ifndef VTDLY
#define VTDLY    0040000
#endif

#ifndef FFDLY
#define FFDLY    0100000
#endif

/* Control flags */
#ifndef CBAUD
#define CBAUD    0010017
#endif

#ifndef CBAUDEX
#define CBAUDEX  0010000
#endif

#ifndef CIBAUD
#define CIBAUD   002003600000
#endif

#ifndef CMSPAR
#define CMSPAR   010000000000
#endif

#ifndef CRTSCTS
#define CRTSCTS  020000000000
#endif

/* Local flags */
#ifndef XCASE
#define XCASE    0000004
#endif

#ifndef ECHOCTL
#define ECHOCTL  0001000
#endif

#ifndef ECHOPRT
#define ECHOPRT  0002000
#endif

#ifndef ECHOKE
#define ECHOKE   0004000
#endif

#ifndef FLUSHO
#define FLUSHO   0010000
#endif

#ifndef PENDIN
#define PENDIN   0040000
#endif

/* Extended baud rates */
#ifndef B57600
#define B57600   0010001
#endif

#ifndef B115200
#define B115200  0010002
#endif

#ifndef B230400
#define B230400  0010003
#endif

#ifndef B460800
#define B460800  0010004
#endif

#ifndef B500000
#define B500000  0010005
#endif

#ifndef B576000
#define B576000  0010006
#endif

#ifndef B921600
#define B921600  0010007
#endif

#ifndef B1000000
#define B1000000 0010010
#endif

#ifndef B1152000
#define B1152000 0010011
#endif

#ifndef B1500000
#define B1500000 0010012
#endif

#ifndef B2000000
#define B2000000 0010013
#endif

#ifndef B2500000
#define B2500000 0010014
#endif

#ifndef B3000000
#define B3000000 0010015
#endif

#ifndef B3500000
#define B3500000 0010016
#endif

#ifndef B4000000
#define B4000000 0010017
#endif

/* Linux tty ioctl commands (from <asm-generic/ioctls.h>) */
#ifndef TCGETS
#define TCGETS   0x5401
#endif

#ifndef TCSETS
#define TCSETS   0x5402
#endif

#ifndef TCSETSW
#define TCSETSW  0x5403
#endif

#ifndef TCSETSF
#define TCSETSF  0x5404
#endif
