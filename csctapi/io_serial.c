   /*
    io_serial.c
    Serial port input/output functions

    This file is part of the Unix driver for Towitoko smartcard readers
    Copyright (C) 2000 2001 Carlos Prados <cprados@yahoo.com>

    This version is modified by doz21 to work in a special manner ;)

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "defines.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#ifdef OS_HPUX
#include <sys/modem.h>
#endif
#include <termios.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef HAVE_POLL
#include <sys/poll.h>
#else
#include <sys/signal.h>
#include <sys/types.h>
#endif
#include <sys/time.h>
#include <sys/ioctl.h>
#include <time.h>
#include "io_serial.h"
#include "mc_global.h"

#ifdef OS_LINUX
#include <linux/serial.h>
#endif

#define IO_SERIAL_FILENAME_LENGTH 	32

/*
 * Internal functions declaration
 */

static int IO_Serial_Bitrate(int bitrate);

static bool IO_Serial_WaitToRead (int hnd, unsigned delay_ms, unsigned timeout_ms);

static bool IO_Serial_WaitToWrite (IO_Serial *io, unsigned delay_ms, unsigned timeout_ms);

static void IO_Serial_DeviceName (unsigned com, bool usbserial, char * filename, unsigned length);

static bool IO_Serial_InitPnP (IO_Serial * io);

static void IO_Serial_Clear (IO_Serial * io);

static bool IO_Serial_GetPropertiesCache(IO_Serial * io, IO_Serial_Properties * props);

static void IO_Serial_SetPropertiesCache(IO_Serial * io, IO_Serial_Properties * props);

static void IO_Serial_ClearPropertiesCache (IO_Serial * io);

static int _in_echo_read = 0;
int io_serial_need_dummy_char = 0;

int fdmc=(-1);

#if defined(TUXBOX) && defined(PPC)
void IO_Serial_Ioctl_Lock(IO_Serial * io, int flag)
{
  extern int *oscam_sem;
  if ((io->com!=RTYP_DB2COM1) && (io->com!=RTYP_DB2COM2)) return;
  if (!flag)
    *oscam_sem=0;
  else while (*oscam_sem!=io->com)
  {
    while (*oscam_sem)
    usleep((io->com)*2000);
    *oscam_sem=io->com;
    usleep(1000);
  }
}

static bool IO_Serial_DTR_RTS_dbox2(int mcport, int dtr, int set)
{
  int rc;
  unsigned short msr;
  unsigned int mbit;
  unsigned short rts_bits[2]={ 0x10, 0x800};
  unsigned short dtr_bits[2]={0x100,     0};

#ifdef DEBUG_IO
printf("IO: multicam.o %s %s\n", dtr ? "dtr" : "rts", set ? "set" : "clear"); fflush(stdout);
#endif
  if ((rc=ioctl(fdmc, GET_PCDAT, &msr))>=0)
  {
    if (dtr)		// DTR
    {
      if (dtr_bits[mcport])
      {
        if (set)
          msr&=(unsigned short)(~dtr_bits[mcport]);
        else
          msr|=dtr_bits[mcport];
        rc=ioctl(fdmc, SET_PCDAT, &msr);
      }
      else
        rc=0;		// Dummy, can't handle using multicam.o
    }
    else		// RTS
    {
      if (set)
        msr&=(unsigned short)(~rts_bits[mcport]);
      else
        msr|=rts_bits[mcport];
      rc=ioctl(fdmc, SET_PCDAT, &msr);
    }
  }
  return((rc<0) ? FALSE : TRUE);
}
#endif

bool IO_Serial_DTR_RTS(IO_Serial * io, int dtr, int set)
{
	unsigned int msr;
	unsigned int mbit;

#if defined(TUXBOX) && defined(PPC)
	if ((io->com==RTYP_DB2COM1) || (io->com==RTYP_DB2COM2))
		return(IO_Serial_DTR_RTS_dbox2(io->com==RTYP_DB2COM2, dtr, set));
#endif

	mbit=(dtr) ? TIOCM_DTR : TIOCM_RTS;
#if defined(TIOCMBIS) && defined(TIOBMBIC)
	if (ioctl (io->fd, set ? TIOCMBIS : TIOCMBIC, &mbit) < 0)
		return FALSE;
#else
	if (ioctl(io->fd, TIOCMGET, &msr) < 0)
		return FALSE;
	if (set)
		msr|=mbit;
	else
		msr&=~mbit;
	return((ioctl(io->fd, TIOCMSET, &msr)<0) ? FALSE : TRUE);
#endif
}

/*
 * Public functions definition
 */

IO_Serial * IO_Serial_New (int reader_type)
{
	IO_Serial *io;
	
	io = (IO_Serial *) malloc (sizeof (IO_Serial));
	
	if (io != NULL)
		IO_Serial_Clear (io);
	
	io->reader_type=reader_type;
	
	return io;
}

bool IO_Serial_Init (IO_Serial * io, unsigned com, bool usbserial, bool pnp)
{
	char filename[IO_SERIAL_FILENAME_LENGTH];
	
	IO_Serial_DeviceName (com, usbserial, filename, IO_SERIAL_FILENAME_LENGTH);
	
#ifdef DEBUG_IO
	printf ("IO: Opening serial port %s\n", filename);
#endif
	
	if (com < 1)
		return FALSE;
	
	io->com = com;

#ifdef SCI_DEV
	if (com==RTYP_SCI)
		io->fd = open (filename, O_RDWR|O_NONBLOCK|O_NOCTTY);
	else
#endif

#ifdef OS_MACOSX
		io->fd = open (filename,  O_RDWR | O_NOCTTY | O_NDELAY );
#else
		io->fd = open (filename, O_RDWR | O_NOCTTY | O_SYNC);
#endif

	if (io->fd < 0)
		return FALSE;

#if defined(TUXBOX) && defined(PPC)
	if ((com==RTYP_DB2COM1) || (com==RTYP_DB2COM2))
		if ((fdmc = open(DEV_MULTICAM, O_RDWR)) < 0)
		{
			close(io->fd);
			return FALSE;
		}
#endif
	
	if (com!=RTYP_SCI)
		IO_Serial_InitPnP (io);
	
	io->usbserial=usbserial;
	
	if(io->com!=RTYP_SCI)
		IO_Serial_Flush(io);
		
	return TRUE;
}

bool IO_Serial_GetProperties (IO_Serial * io, IO_Serial_Properties * props)
{
	struct termios currtio;
	speed_t i_speed, o_speed;
	unsigned int mctl;

#ifdef SCI_DEV
	if(io->com==RTYP_SCI)
		return FALSE;
#endif
	
	if (IO_Serial_GetPropertiesCache(io, props))
		return TRUE;
	
	if (tcgetattr (io->fd, &currtio) != 0)
		return FALSE;

	o_speed = cfgetospeed (&currtio);
	
	switch (o_speed)
	{
#ifdef B0
		case B0:
			props->output_bitrate = 0;
			break;
#endif
#ifdef B50
		case B50:
			props->output_bitrate = 50;
			break;
#endif
#ifdef B75
		case B75:
			props->output_bitrate = 75;
			break;
#endif
#ifdef B110
		case B110:
			props->output_bitrate = 110;
			break;
#endif
#ifdef B134
		case B134:
			props->output_bitrate = 134;
			break;
#endif
#ifdef B150
		case B150:
			props->output_bitrate = 150;
			break;
#endif
#ifdef B200
		case B200:
			props->output_bitrate = 200;
			break;
#endif
#ifdef B300
		case B300:
			props->output_bitrate = 300;
			break;
#endif
#ifdef B600
		case B600:
			props->output_bitrate = 600;
			break;
#endif
#ifdef B1200
		case B1200:
			props->output_bitrate = 1200;
			break;
#endif
#ifdef B1800
		case B1800:
			props->output_bitrate = 1800;
			break;
#endif
#ifdef B2400
		case B2400:
			props->output_bitrate = 2400;
			break;
#endif
#ifdef B4800
		case B4800:
			props->output_bitrate = 4800;
			break;
#endif
#ifdef B9600
		case B9600:
			props->output_bitrate = 9600;
			break;
#endif
#ifdef B19200
		case B19200:
			props->output_bitrate = 19200;
			break;
#endif
#ifdef B38400
		case B38400:
			props->output_bitrate = 38400;
			break;
#endif
#ifdef B57600
		case B57600:
			props->output_bitrate = 57600;
			break;
#endif
#ifdef B115200
		case B115200:
			props->output_bitrate = 115200;
			break;
#endif
#ifdef B230400
		case B230400:
			props->output_bitrate = 230400;
			break;
#endif
		default:
			props->output_bitrate = 1200;
			break;
	}

	i_speed = cfgetispeed (&currtio);
	
	switch (i_speed)
	{
#ifdef B0
		case B0:
			props->input_bitrate = 0;
			break;
#endif
#ifdef B50
		case B50:
			props->input_bitrate = 50;
			break;
#endif
#ifdef B75
		case B75:
			props->input_bitrate = 75;
			break;
#endif
#ifdef B110
		case B110:
			props->input_bitrate = 110;
			break;
#endif
#ifdef B134
		case B134:
			props->input_bitrate = 134;
			break;
#endif
#ifdef B150
		case B150:
			props->input_bitrate = 150;
			break;
#endif
#ifdef B200
		case B200:
			props->input_bitrate = 200;
			break;
#endif
#ifdef B300
		case B300:
			props->input_bitrate = 300;
			break;
#endif
#ifdef B600
		case B600:
			props->input_bitrate = 600;
			break;
#endif
#ifdef B1200
		case B1200:
			props->input_bitrate = 1200;
			break;
#endif
#ifdef B1800
		case B1800:
			props->input_bitrate = 1800;
			break;
#endif
#ifdef B2400
		case B2400:
			props->input_bitrate = 2400;
			break;
#endif
#ifdef B4800
		case B4800:
			props->input_bitrate = 4800;
			break;
#endif
#ifdef B9600
		case B9600:
			props->input_bitrate = 9600;
			break;
#endif
#ifdef B19200
		case B19200:
			props->input_bitrate = 19200;
			break;
#endif
#ifdef B38400
		case B38400:
			props->input_bitrate = 38400;
			break;
#endif
#ifdef B57600
		case B57600:
			props->input_bitrate = 57600;
			break;
#endif
#ifdef B115200
		case B115200:
			props->input_bitrate = 115200;
			break;
#endif
#ifdef B230400
		case B230400:
			props->input_bitrate = 230400;
			break;
#endif
		default:
			props->input_bitrate = 1200;
			break;
	}
	
	switch (currtio.c_cflag & CSIZE)
	{
		case CS5:
			props->bits = 5;
			break;
		case CS6:
			props->bits = 6;
			break;
		case CS7:
			props->bits = 7;
			break;
		case CS8:
			props->bits = 8;
			break;
	}
	
	if (((currtio.c_cflag) & PARENB) == PARENB)
	{
		if (((currtio.c_cflag) & PARODD) == PARODD)
			props->parity = IO_SERIAL_PARITY_ODD;
		else
			props->parity = IO_SERIAL_PARITY_EVEN;
	}
	else
	{
		props->parity = IO_SERIAL_PARITY_NONE;
	}
	
	if (((currtio.c_cflag) & CSTOPB) == CSTOPB)
		props->stopbits = 2;
	else
		props->stopbits = 1;
	
	if (ioctl (io->fd, TIOCMGET, &mctl) < 0)
		return FALSE;
	
	props->dtr = ((mctl & TIOCM_DTR) ? IO_SERIAL_HIGH : IO_SERIAL_LOW);
	props->rts = ((mctl & TIOCM_RTS) ? IO_SERIAL_HIGH : IO_SERIAL_LOW);
	
	IO_Serial_SetPropertiesCache (io, props);
	
#ifdef DEBUG_IO
	printf("IO: Getting properties: %ld bps; %d bits/byte; %s parity; %d stopbits; dtr=%d; rts=%d\n", props->input_bitrate, props->bits, props->parity == IO_SERIAL_PARITY_EVEN ? "Even" : props->parity == IO_SERIAL_PARITY_ODD ? "Odd" : "None", props->stopbits, props->dtr, props->rts);
#endif
	
	return TRUE;
}

bool IO_Serial_SetProperties (IO_Serial * io, IO_Serial_Properties * props)
{
   struct termios newtio;
   unsigned int modembits;
	
#ifdef SCI_DEV
   if(io->com==RTYP_SCI)
      return FALSE;
#endif
   
   //	printf("IO: Setting properties: com%d, %ld bps; %d bits/byte; %s parity; %d stopbits; dtr=%d; rts=%d\n", io->com, props->input_bitrate, props->bits, props->parity == IO_SERIAL_PARITY_EVEN ? "Even" : props->parity == IO_SERIAL_PARITY_ODD ? "Odd" : "None", props->stopbits, props->dtr, props->rts);
   memset (&newtio, 0, sizeof (newtio));
   /* Set the bitrate */
   
   extern int mhz;
   extern int reader_irdeto_mode;

   if(io->reader_type==RTYP_SMART)
   {
#ifdef DEBUG_IO
      printf("IO: SMARTREADER .. switching to frequency to %2.2fMHz\n", (float)mhz/100.0);
#endif
      if(!IO_Serial_Set_Smartreader_Freq(io,mhz,reader_irdeto_mode))
      {
#ifdef DEBUG_IO
         printf("IO: SMARTREADER .. ERROR switching to 6MHz\n");
#endif
         return FALSE;
      }
   }

   if (mhz == 600)
   {
      
      /* for 6MHz */
      if (reader_irdeto_mode)
      {
         cfsetospeed(&newtio, IO_Serial_Bitrate(props->output_bitrate));
         cfsetispeed(&newtio, IO_Serial_Bitrate(props->input_bitrate));
      } 
      else 
      {
#ifdef OS_LINUX
         /* these structures are only available on linux as fas as we know so limit this code to OS_LINUX */
         struct serial_struct nuts;
         ioctl(io->fd, TIOCGSERIAL, &nuts);
         nuts.custom_divisor = nuts.baud_base / 9600 * 3.57 / 6;
         nuts.flags &= ~ASYNC_SPD_MASK;
         nuts.flags |= ASYNC_SPD_CUST;
         ioctl(io->fd, TIOCSSERIAL, &nuts);
         cfsetospeed(&newtio, IO_Serial_Bitrate(38400));
         cfsetispeed(&newtio, IO_Serial_Bitrate(38400));
#else
         cfsetospeed(&newtio, IO_Serial_Bitrate(props->output_bitrate));
         cfsetispeed(&newtio, IO_Serial_Bitrate(props->input_bitrate));
#endif
      }
   }
   else if (mhz == 357 || mhz == 358)
   {
      /* for 3.57 MHz */
      if (reader_irdeto_mode)
      {
#ifdef OS_LINUX
         /* these structures are only available on linux as fas as we know so limit this code to OS_LINUX */
         struct serial_struct nuts;
         ioctl(io->fd, TIOCGSERIAL, &nuts);
         nuts.custom_divisor = nuts.baud_base / 5713;
         nuts.flags &= ~ASYNC_SPD_MASK;
         nuts.flags |= ASYNC_SPD_CUST;
         ioctl(io->fd, TIOCSSERIAL, &nuts);
         cfsetospeed(&newtio, IO_Serial_Bitrate(38400));
         cfsetispeed(&newtio, IO_Serial_Bitrate(38400));
#else
         cfsetospeed(&newtio, IO_Serial_Bitrate(props->output_bitrate));
         cfsetispeed(&newtio, IO_Serial_Bitrate(props->input_bitrate));
#endif
      }
      else 
      {
         cfsetospeed(&newtio, IO_Serial_Bitrate(props->output_bitrate));
         cfsetispeed(&newtio, IO_Serial_Bitrate(props->input_bitrate));
      }
   }
   else if (mhz == 800)
   {
      
      /* for 8MHz */
      if (reader_irdeto_mode)
      {
         cfsetospeed(&newtio, IO_Serial_Bitrate(props->output_bitrate));
         cfsetispeed(&newtio, IO_Serial_Bitrate(props->input_bitrate));
      } 
      else 
      {
#ifdef OS_LINUX
         /* these structures are only available on linux as fas as we know so limit this code to OS_LINUX */
         struct serial_struct nuts;
         ioctl(io->fd, TIOCGSERIAL, &nuts);
         nuts.custom_divisor = nuts.baud_base / 9600 * 6 / 8;
         nuts.flags &= ~ASYNC_SPD_MASK;
         nuts.flags |= ASYNC_SPD_CUST;
         ioctl(io->fd, TIOCSSERIAL, &nuts);
         cfsetospeed(&newtio, IO_Serial_Bitrate(38400));
         cfsetispeed(&newtio, IO_Serial_Bitrate(38400));
#else
         cfsetospeed(&newtio, IO_Serial_Bitrate(props->output_bitrate));
         cfsetispeed(&newtio, IO_Serial_Bitrate(props->input_bitrate));
#endif
      }
   }
   else if (mhz == 1000)
   {
      
      /* for 10MHz */
      if (reader_irdeto_mode)
      {
         cfsetospeed(&newtio, IO_Serial_Bitrate(props->output_bitrate));
         cfsetispeed(&newtio, IO_Serial_Bitrate(props->input_bitrate));
      } 
      else 
      {
#ifdef OS_LINUX
         /* these structures are only available on linux as fas as we know so limit this code to OS_LINUX */
         struct serial_struct nuts;
         ioctl(io->fd, TIOCGSERIAL, &nuts);
         nuts.custom_divisor = nuts.baud_base / 9600 * 6 / 10;
         nuts.flags &= ~ASYNC_SPD_MASK;
         nuts.flags |= ASYNC_SPD_CUST;
         ioctl(io->fd, TIOCSSERIAL, &nuts);
         cfsetospeed(&newtio, IO_Serial_Bitrate(38400));
         cfsetispeed(&newtio, IO_Serial_Bitrate(38400));
#else
         cfsetospeed(&newtio, IO_Serial_Bitrate(props->output_bitrate));
         cfsetispeed(&newtio, IO_Serial_Bitrate(props->input_bitrate));
#endif
      }
   }
   else
   {
      /* invalid */
      return FALSE;
   }
        
   /* Set the character size */
   switch (props->bits)
   {
		case 5:
			newtio.c_cflag |= CS5;
			break;
		
		case 6:
			newtio.c_cflag |= CS6;
			break;
		
		case 7:
			newtio.c_cflag |= CS7;
			break;
		
		case 8:
			newtio.c_cflag |= CS8;
			break;
	}
	
	/* Set the parity */
	switch (props->parity)
	{
		case IO_SERIAL_PARITY_ODD:
			newtio.c_cflag |= PARENB;
			newtio.c_cflag |= PARODD;
			break;
		
		case IO_SERIAL_PARITY_EVEN:	
			newtio.c_cflag |= PARENB;
			newtio.c_cflag &= ~PARODD;
			break;
		
		case IO_SERIAL_PARITY_NONE:
			newtio.c_cflag &= ~PARENB;
			break;
	}
	
	/* Set the number of stop bits */
	switch (props->stopbits)
	{
		case 1:
			newtio.c_cflag &= (~CSTOPB);
			break;
		case 2:
			newtio.c_cflag |= CSTOPB;
			break;
	}
	
	/* Selects raw (non-canonical) input and output */
	newtio.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
	newtio.c_oflag &= ~OPOST;
#if 1
	newtio.c_iflag |= IGNPAR;
	/* Ignore parity errors!!! Windows driver does so why shouldn't I? */
#endif
	/* Enable receiber, hang on close, ignore control line */
	newtio.c_cflag |= CREAD | HUPCL | CLOCAL;
	
	/* Read 1 byte minimun, no timeout specified */
	newtio.c_cc[VMIN] = 1;
	newtio.c_cc[VTIME] = 0;

//	tcdrain(io->fd);
	if (tcsetattr (io->fd, TCSANOW, &newtio) < 0)
		return FALSE;
//	tcflush(io->fd, TCIOFLUSH);
//	if (tcsetattr (io->fd, TCSAFLUSH, &newtio) < 0)
//		return FALSE;

	IO_Serial_Ioctl_Lock(io, 1);
	IO_Serial_DTR_RTS(io, 0, props->rts == IO_SERIAL_HIGH);
	IO_Serial_DTR_RTS(io, 1, props->dtr == IO_SERIAL_HIGH);
	IO_Serial_Ioctl_Lock(io, 0);
	
	IO_Serial_SetPropertiesCache (io, props);
	
#ifdef DEBUG_IO
	printf("IO: Setting properties: com%d, %ld bps; %d bits/byte; %s parity; %d stopbits; dtr=%d; rts=%d\n", io->com, props->input_bitrate, props->bits, props->parity == IO_SERIAL_PARITY_EVEN ? "Even" : props->parity == IO_SERIAL_PARITY_ODD ? "Odd" : "None", props->stopbits, props->dtr, props->rts);
#endif
	return TRUE;
}

void IO_Serial_Flush (IO_Serial * io)
{
	BYTE b;
	while(IO_Serial_Read(io, 1000, 1, &b));
}


void IO_Serial_GetPnPId (IO_Serial * io, BYTE * pnp_id, unsigned *length)
{
	(*length) = io->PnP_id_size;
	memcpy (pnp_id, io->PnP_id, io->PnP_id_size);
}

unsigned IO_Serial_GetCom (IO_Serial * io)
{
	return io->com;
}


bool IO_Serial_Read (IO_Serial * io, unsigned timeout, unsigned size, BYTE * data)
{
	BYTE c;
	int count = 0;
	bool readed;
	struct timeval tv, tv_spent;
	
	if((io->com!=RTYP_SCI) && (io->wr>0))
	{
		BYTE buf[256];
		int n = io->wr;
		io->wr = 0;
	
		if(!IO_Serial_Read (io, timeout, n, buf))
		{
			return FALSE;
		}
	}
	
#ifdef DEBUG_IO
	printf ("IO: Receiving: ");
	fflush (stdout);
#endif

	for (count = 0; count < size * (_in_echo_read ? (1+io_serial_need_dummy_char) : 1); count++)
#ifdef SH4
	{
		gettimeofday(&tv,0);
		memcpy(&tv_spent,&tv,sizeof(struct timeval));
		readed=FALSE;
		while( (((tv_spent.tv_sec-tv.tv_sec)*1000) + ((tv_spent.tv_usec-tv.tv_usec)/1000L))<timeout )
		{
			if (read (io->fd, &c, 1) == 1)
			{
				readed=TRUE;
				break;
			}
			gettimeofday(&tv_spent,0);
		}
		if(!readed) return FALSE;
		
		data[_in_echo_read ? count/(1+io_serial_need_dummy_char) : count] = c;	
			
#ifdef DEBUG_IO
		printf ("%X ", c);
		fflush (stdout);
#endif
	}
	
#else //no SH4 
	{
		if (IO_Serial_WaitToRead (io->fd, 0, timeout))
		{
			if (read (io->fd, &c, 1) != 1)
			{
#ifdef DEBUG_IO
				printf ("ERROR\n");
				fflush (stdout);
#endif
				return FALSE;
			}
			data[_in_echo_read ? count/(1+io_serial_need_dummy_char) : count] = c;
			
#ifdef DEBUG_IO
			printf ("%X ", c);
			fflush (stdout);
#endif
		}
		else
		{
#ifdef DEBUG_IO
			printf ("TIMEOUT\n");
			fflush (stdout);
#endif
			tcflush (io->fd, TCIFLUSH);
			return FALSE;
		}
	}
#endif //endif else SH4	
    _in_echo_read = 0;

#ifdef DEBUG_IO
	printf ("\n");
	fflush (stdout);
#endif
	
	return TRUE;
}




bool IO_Serial_Write (IO_Serial * io, unsigned delay, unsigned size, BYTE * data)
{
	unsigned count, to_send;
    BYTE data_w[512];
    int i_w;
#ifdef DEBUG_IO
	unsigned i;
	
	printf ("IO: Sending: ");
	fflush (stdout);
#endif
	/* Discard input data from previous commands */
//	tcflush (io->fd, TCIFLUSH);
	
	for (count = 0; count < size; count += to_send)
	{
//		if(io->com==RTYP_SCI)
//			to_send = 1;
//		else
			to_send = (delay? 1: size);
		
		if (IO_Serial_WaitToWrite (io, delay, 1000))
		{
            for (i_w=0; i_w < to_send; i_w++) {
            data_w [(1+io_serial_need_dummy_char)*i_w] = data [count + i_w];
            if (io_serial_need_dummy_char) {
              data_w [2*i_w+1] = 0x00;
              }
            }
            unsigned int u = write (io->fd, data_w, (1+io_serial_need_dummy_char)*to_send);
            _in_echo_read = 1;
            if (u != (1+io_serial_need_dummy_char)*to_send)
			{
#ifdef DEBUG_IO
				printf ("ERROR\n");
				fflush (stdout);
#endif
				if(io->com!=RTYP_SCI)
					io->wr += u;
				return FALSE;
			}
			
			if(io->com!=RTYP_SCI)
				io->wr += to_send;
			
#ifdef DEBUG_IO
			for (i=0; i<(1+io_serial_need_dummy_char)*to_send; i++)
				printf ("%X ", data_w[count + i]);
			fflush (stdout);
#endif
		}
		else
		{
#ifdef DEBUG_IO
			printf ("TIMEOUT\n");
			fflush (stdout);
#endif
//			tcflush (io->fd, TCIFLUSH);
			return FALSE;
		}
	}
	
#ifdef DEBUG_IO
	printf ("\n");
	fflush (stdout);
#endif
	
	return TRUE;
}

bool IO_Serial_Close (IO_Serial * io)
{
	char filename[IO_SERIAL_FILENAME_LENGTH];
	
	IO_Serial_DeviceName (io->com, io->usbserial, filename, IO_SERIAL_FILENAME_LENGTH);
	
#ifdef DEBUG_IO
	printf ("IO: Clossing serial port %s\n", filename);
#endif
	
#if defined(TUXBOX) && defined(PPC)
	close(fdmc);
#endif
	if (close (io->fd) != 0)
		return FALSE;
	
	IO_Serial_ClearPropertiesCache (io);
	IO_Serial_Clear (io);
	
	return TRUE;
}

void IO_Serial_Delete (IO_Serial * io)
{
	if (io->props != NULL)
		free (io->props);
	
	free (io);
}

/*
 * Internal functions definition
 */

static int IO_Serial_Bitrate(int bitrate)
{
#ifdef B230400
	if ((bitrate)>=230400) return B230400;
#endif
#ifdef B115200
	if ((bitrate)>=115200) return B115200;
#endif
#ifdef B57600
	if ((bitrate)>=57600) return B57600;
#endif
#ifdef B38400
	if ((bitrate)>=38400) return B38400;
#endif
#ifdef B19200
	if ((bitrate)>=19200) return B19200;
#endif
#ifdef B9600
	if ((bitrate)>=9600) return B9600;
#endif
#ifdef B4800
	if ((bitrate)>=4800) return B4800;
#endif
#ifdef B2400
	if ((bitrate)>=2400) return B2400;
#endif
#ifdef B1800
	if ((bitrate)>=1800) return B1800;
#endif
#ifdef B1200
	if ((bitrate)>=1200) return B1200;
#endif
#ifdef B600
	if ((bitrate)>=600) return B600;
#endif
#ifdef B300
	if ((bitrate)>=300) return B300;
#endif
#ifdef B200
	if ((bitrate)>=200) return B200;
#endif
#ifdef B150
	if ((bitrate)>=150) return B150;
#endif
#ifdef B134
	if ((bitrate)>=134) return B134;
#endif
#ifdef B110
	if ((bitrate)>=110) return B110;
#endif
#ifdef B75
	if ((bitrate)>=75) return B75;
#endif
#ifdef B50
	if ((bitrate)>=50) return B50;
#endif
#ifdef B0
	if ((bitrate)>=0) return B0;
#endif
	return 0;	/* Should never get here */
}

static bool IO_Serial_WaitToRead (int hnd, unsigned delay_ms, unsigned timeout_ms)
{
   fd_set rfds;
   fd_set erfds;
   struct timeval tv;
   int select_ret;
   int in_fd;
   
   if (delay_ms > 0)
   {
#ifdef HAVE_NANOSLEEP
      struct timespec req_ts;
      
      req_ts.tv_sec = delay_ms / 1000;
      req_ts.tv_nsec = (delay_ms % 1000) * 1000000L;
      nanosleep (&req_ts, NULL);
#else
      usleep (delay_ms * 1000L);
#endif
   }
   
   in_fd=hnd;
   
   FD_ZERO(&rfds);
   FD_SET(in_fd, &rfds);
   
   FD_ZERO(&erfds);
   FD_SET(in_fd, &erfds);
   
   tv.tv_sec = timeout_ms/1000;
   tv.tv_usec = (timeout_ms % 1000) * 1000L;
   select_ret = select(in_fd+1, &rfds, NULL,  &erfds, &tv);
   if(select_ret==-1)
   {
      printf("select_ret=%i\n" , select_ret);
      printf("errno =%d\n", errno);
      fflush(stdout);
      return (FALSE);
   }

   if (FD_ISSET(in_fd, &erfds))
   {
      printf("fd is in error fds\n");
      printf("errno =%d\n", errno);
      fflush(stdout);
      return (FALSE);
   }

   return(FD_ISSET(in_fd,&rfds));
}

static bool IO_Serial_WaitToWrite (IO_Serial *io, unsigned delay_ms, unsigned timeout_ms)
{
   fd_set wfds;
   fd_set ewfds;
   struct timeval tv;
   int select_ret;
   int out_fd;
   
#ifdef SCI_DEV
   if(io->com==RTYP_SCI)
      return TRUE;
#endif
		
   if (delay_ms > 0)
	{
#ifdef HAVE_NANOSLEEP
      struct timespec req_ts;
      
      req_ts.tv_sec = delay_ms / 1000;
      req_ts.tv_nsec = (delay_ms % 1000) * 1000000L;
      nanosleep (&req_ts, NULL);
#else
      usleep (delay_ms * 1000L);
#endif
   }

   out_fd=io->fd;
    
   FD_ZERO(&wfds);
   FD_SET(out_fd, &wfds);
   
   FD_ZERO(&ewfds);
   FD_SET(out_fd, &ewfds);
   
   tv.tv_sec = timeout_ms/1000L;
   tv.tv_usec = (timeout_ms % 1000) * 1000L;

   select_ret = select(out_fd+1, NULL, &wfds, &ewfds, &tv);

   if(select_ret==-1)
   {
      printf("select_ret=%d\n" , select_ret);
      printf("errno =%d\n", errno);
      fflush(stdout);
      return (FALSE);
   }

   if (FD_ISSET(out_fd, &ewfds))
   {
      printf("fd is in ewfds\n");
      printf("errno =%d\n", errno);
      fflush(stdout);
      return (FALSE);
   }

   return(FD_ISSET(out_fd,&wfds));
    
}

static void IO_Serial_Clear (IO_Serial * io)
{
	io->fd = -1;
	io->props = NULL;
	io->com = 0;
	memset (io->PnP_id, 0, IO_SERIAL_PNPID_SIZE);
	io->PnP_id_size = 0;
	io->usbserial = FALSE;
	io->wr = 0;
}

static void IO_Serial_SetPropertiesCache(IO_Serial * io, IO_Serial_Properties * props)
{
#ifdef SCI_DEV
	if(io->com==RTYP_SCI)
		return;
#endif
		
	if (io->props == NULL)
		io->props = (IO_Serial_Properties *) malloc (sizeof (IO_Serial_Properties));
#ifdef DEBUG_IO
	printf ("IO: Catching properties\n");
#endif
	
	memcpy (io->props, props, sizeof (IO_Serial_Properties)); 
}

static bool IO_Serial_GetPropertiesCache(IO_Serial * io, IO_Serial_Properties * props)
{
	if (io->props != NULL)
	{
		memcpy (props, io->props, sizeof (IO_Serial_Properties)); 
#if 0
#ifdef DEBUG_IO
		printf("IO: Getting properties (catched): %ld bps; %d bits/byte; %s parity; %d stopbits; dtr=%d; rts=%d\n", props->input_bitrate, props->bits, props->parity == IO_SERIAL_PARITY_EVEN ? "Even" : props->parity == IO_SERIAL_PARITY_ODD ? "Odd" : "None", props->stopbits, props->dtr, props->rts);
#endif
#endif
		return TRUE;
	}
	
	return FALSE;
}

static void IO_Serial_ClearPropertiesCache (IO_Serial * io)
{
#ifdef DEBUG_IO
	printf ("IO: Clearing properties cache\n");
#endif
	if (io->props != NULL)
	{
		free (io->props);
		io->props = NULL;
	}
}

static void IO_Serial_DeviceName (unsigned com, bool usbserial, char * filename, unsigned length)
{
	extern char oscam_device[];
   snprintf (filename, length, "%s", oscam_device);
//	if(com==1)
//		snprintf (filename, length, "/dev/tts/%d", com - 1);
//	else
//		snprintf (filename, length, "/dev/sci%d", com - 2);
}

static bool IO_Serial_InitPnP (IO_Serial * io)
{
	IO_Serial_Properties props;
	int i = 0;
	props.input_bitrate = 1200;
	props.output_bitrate = 1200;
	props.parity = IO_SERIAL_PARITY_NONE;
	props.bits = 7;
	props.stopbits = 1;
	props.dtr = IO_SERIAL_HIGH;
//	props.rts = IO_SERIAL_HIGH;
	props.rts = IO_SERIAL_LOW;
	
	if (!IO_Serial_SetProperties (io, &props))
		return FALSE;

	while ((i < IO_SERIAL_PNPID_SIZE) && IO_Serial_Read (io, 200, 1, &(io->PnP_id[i])))
      i++;

	io->PnP_id_size = i;
		return TRUE;
}


bool IO_Serial_Set_Smartreader_Freq(IO_Serial * io, int freq, int irdeto_mode)
 {
   struct termios term;
   struct termios orig;
   unsigned int u;
   unsigned char fi_di[4]={0x01, 0x01, 0x74, 0x01};
   unsigned char fr[3]={0x02, 0x00, 0x00};
   unsigned char nn[2]={0x03, 0x00};
   unsigned char pr[2]={0x04, 0x00};
   unsigned char in[2]={0x05, 0x00};
   
   orig=term;
     
   /* set smartreader in CMD mode */
   tcgetattr(io->fd, &term);
   
   term.c_cflag &= ~CSIZE;
   term.c_cflag |= CS5;
   cfsetospeed(&term, 9600);
   cfsetispeed(&term, 9600);
   tcsetattr(io->fd, TCSANOW, &term);
   
   // our freq comes in as 358, 357 or 600 so it needs this to be in KHz for the FR command
   freq*=10;
   fr[1]=(unsigned char)((freq & 0xff00)>>8);
   fr[2]=(unsigned char)(freq & 0x00ff);

   // Irdeto card supposedly need NN set to 1 .. to be confirmed
   if(irdeto_mode)
      nn[1]=0x01;
      
   // send the commands
   IO_Serial_Write (io, 0, 4, fi_di);
   IO_Serial_Flush(io);
   
   IO_Serial_Write (io, 0, 3, fr);
   IO_Serial_Flush(io);

   IO_Serial_Write (io, 0, 2, nn);
   IO_Serial_Flush(io);

   IO_Serial_Write (io, 0, 2, pr);
   IO_Serial_Flush(io);

   IO_Serial_Write (io, 0, 2, in);
   IO_Serial_Flush(io);
      
   /* set smartreader in DATA mode */
   tcgetattr(io->fd, &orig);
   orig.c_cflag &= ~CSIZE;
   orig.c_cflag |= CS8;
   tcsetattr(io->fd, TCSANOW, &orig);

   return TRUE;
 }
 
