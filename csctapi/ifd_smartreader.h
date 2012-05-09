/*
    ifd_smartreader.h
    Header file for Argolis smartreader+.
*/
#if defined(LIBUSB)
#ifndef __SMARTREADER__
#define __SMARTREADER__

#include <memory.h>
#if defined(__FreeBSD__)
#include <libusb.h>
#else
#include <libusb-1.0/libusb.h>
#endif

#include "atr.h"

#include "smartreader_types.h"

int32_t SR_Init (struct s_reader *reader);
int32_t SR_GetStatus (struct s_reader *reader,int32_t * in);
int32_t SR_Reset (struct s_reader *reader, ATR * atr);
int32_t SR_Transmit (struct s_reader *reader, BYTE * buffer, uint32_t size);
int32_t SR_Receive (struct s_reader *reader, BYTE * buffer, uint32_t size);
int32_t SR_SetBaudrate (struct s_reader *reader);
int32_t SR_SetParity (struct s_reader *reader, uint16_t parity);
int32_t SR_Close (struct s_reader *reader);
int32_t SR_FastReset(struct s_reader *reader, int32_t delay);
int32_t SR_FastReset_With_ATR(struct s_reader *reader, ATR *atr);

#endif // __SMARTREADER__
#endif // HAVE_LIBUSB
