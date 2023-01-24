/*
 * Copyright 2022 Hans Busch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include "vito_io.h"
#include <mutex>

#ifndef _WIN32
#  include <termios.h>
#  include <sys/ioctl.h>
#  include <unistd.h>
void msleep(uint32_t t) { usleep(t * 1000); }
#else
#  include <io.h>
#define msleep Sleep
extern "C" {
    extern void Sleep(uint32_t);
}
#define tcflush(x,y) 
#endif

static int fd_serial = -1;
static std::mutex ioMutex;  // protects IO

int vito_open(char *device)
{
#ifndef _WIN32
   
    fd_serial = open(device, O_RDWR | O_NOCTTY);
    if (fd_serial < 0) {
        return logText(0, "init", "Error opening device %s\n%s", device, strerror(errno));
    }

    struct termios tcattr = {};
    tcattr.c_iflag = IGNBRK | IGNPAR;
    tcattr.c_cflag = (CLOCAL | HUPCL | B4800 | CS8 | CREAD | PARENB | CSTOPB);
    tcattr.c_cc[VTIME] = 50; // RX timeout
     
    if ( tcsetattr(fd_serial, TCSAFLUSH, &tcattr) < 0 ) {
        return logText(0, "init", "Error configuring device %s\n%s", device, strerror(errno));
    }
   
    // DTR High for voltage supply
   int  modemctl = 0;
    ioctl( fd_serial, TIOCMGET, &modemctl );
    modemctl |= TIOCM_DTR;
    if ( ioctl(fd_serial,TIOCMSET,&modemctl) < 0 ) {
        return logText(0, "Error activating dtr for %s\n%s", device, strerror(errno));
    }
    return 0;
#else
    return logText(0, "init", "Real device operation not supported on Windows");
#endif
}

int vito_init( void )
{
    int trys;
    uint8_t rec;
    const uint8_t initKw[] = { 0x04 };
    const uint8_t initSeq[] = { 0x16, 0x00, 0x00 };
   
    // Send 0x04 until 0x05 received
    trys = 10;
    do {
        if (--trys == 0) {
            return logText(0, "init", "Reset to KW protocol failed");
        }

        tcflush( fd_serial, TCIOFLUSH );
        logDump(4, "WR", 0, initKw, 1);
        write( fd_serial, initKw, 1 );
        msleep( 200 );
        tcflush( fd_serial, TCIFLUSH );
        read( fd_serial, &rec, 1 );    // wait for 0x05
        logDump(4, "RD", 0, &rec, 1);
    }
    while ( rec != 0x05 );
   
    logDump(4, "WR", 0, initSeq, 3);
    write( fd_serial, initSeq, 3 );

    read( fd_serial, &rec, 1 );
    logDump(4, "RD", 0, &rec, 1);
    if (rec != 0x06) {
        return logText(0, "init", "Unexpected resp %02x", rec);
    }
    return 0;
}

// 8-bit crc excluding the preamble
static uint8_t vito_crc( uint8_t *buffer )
{
  int crc = 0;
  for (int i = 1; i <= buffer[1] + 1; i++) {
      crc += buffer[i];
  }
  return (crc & 0xff);
}

enum VITO_RW {
    VITO_READ = 1,
    VITO_WRITE = 2
};

/**
 * Central UART communication method using 300 protocol.
 * Tries to re-initialize on communication errors.
 * Errors are written to logfiles. Optionally  low level communication (level>=4) can be logged as well.
 *
 * @return Number of bytes written. Negative in case of error. Zero when no serial port is connected and in simulation mode.
 */
static int vito_io(int addr, VITO_RW rw, void* vbuffer, size_t len)
{
    std::lock_guard<std::mutex> lock(ioMutex);

    uint8_t* buffer = (uint8_t*)vbuffer;

    if (fd_serial < 0) {
        logDump(2, rw == VITO_WRITE ? "t-" : "r-", addr, buffer, len);      // mark offline case
        return 0;       // offline
    }
    logDump(2, rw == VITO_WRITE ? "tx" : "rx", addr, buffer, len);

    int writeLen = (rw == VITO_WRITE) ? (int)len : 0;
    uint8_t cmd[128] = {
        0x41,                       // preamble
        (uint8_t)(5 + writeLen),    // message length without preamble, this field and CRC
        0,                          // message direction: Host -> Vitodens
        (uint8_t)rw,                // VITO_READ or VITO_WRITE
        (uint8_t)(addr >> 8),
        (uint8_t)addr,
        (uint8_t)len                // read or write content length
    };

    if (len < 1 || len > sizeof(cmd) - 8) return -1;       // sanity check

    if (writeLen) memcpy(cmd + 7, buffer, writeLen);

    cmd[7 + writeLen] = vito_crc(cmd);

    int retries = 3;
    uint8_t byte;
    do {
        logDump(4, "WR", 0, cmd, 8 + writeLen);

        tcflush(fd_serial, TCIOFLUSH);
        if (write(fd_serial, cmd, 8 + writeLen) < 8 + writeLen) return -1;

        if (read(fd_serial, &byte, 1) == 1 && byte == 6) break;
        if (byte == 5) {     // wrong mode => try to re-init
            vito_init();
        }
        if (--retries < 0) return -2;
    } while (1);

    if (read(fd_serial, &byte, 1) < 1 || byte != 0x41) return -3;
    if (read(fd_serial, cmd+1, 1) < 1 || cmd[1] > sizeof(cmd) - 3) return -4;
    int rlen = cmd[1];
    for (int i = 0; i < rlen + 1; i++) {
        if (read(fd_serial, &cmd[i + 2], 1) < 1) return -5;
    }
    logDump(4, "RD", 0, cmd, rlen + 3);

    if (vito_crc(cmd) != cmd[rlen + 2]) return -6;

    if (rw == VITO_READ) memcpy(buffer, cmd + 7, len);

    return rlen;
}

int vito_read(int addr, void* buffer, size_t size)
{
    int ret = vito_io(addr, VITO_READ, buffer, size);
    if (ret < 0) logText(1, "rx", "%04x Error %d", addr, ret);
    return ret;
}

int vito_write(int addr, void* buffer, size_t size)
{
    int ret = vito_io(addr, VITO_WRITE, buffer, size);
    if (ret < 0) logText(1, "tx", "%04x Error %d", addr, ret);
    return ret;
}

