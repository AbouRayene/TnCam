#include "../globals.h"
#ifdef WITH_CARDREADER
#include "../oscam-time.h"
#include "icc_async.h"
#include "ifd_phoenix.h"
#include "io_serial.h"

#if defined(__CYGWIN__)
#undef OK
#undef ERROR
#undef LOBYTE
#undef HIBYTE
#endif

#define OK 0
#define ERROR 1
#define LOBYTE(w) ((unsigned char)((w) & 0xff))
#define HIBYTE(w) ((unsigned char)((w) >> 8))

#define DELAY 150

static int32_t smargo_set_settings(struct s_reader *reader, int32_t freq, unsigned char T, unsigned char inv, uint16_t Fi, unsigned char Di, unsigned char Ni) {
	uint16_t  freqk = (freq * 10);
	uchar data[4];
	struct termios term;

	tcgetattr(reader->handle, &term);
	term.c_cflag &= ~CSIZE;
	term.c_cflag |= CS5;
	tcsetattr(reader->handle, TCSANOW, &term);

	cs_sleepms(DELAY);

	rdr_debug_mask(reader, D_DEVICE, "Smargo: sending F=%04X (%d), D=%02X (%d), Freq=%04X (%d), N=%02X (%d), T=%02X (%d), inv=%02X (%d)",
		Fi, Fi, Di, Di, freqk, freqk, Ni, Ni, T, T, inv, inv);

	if (T!=14 || freq == 369) {
		data[0]=0x01;
		data[1]=HIBYTE(Fi);
		data[2]=LOBYTE(Fi);
		data[3]=Di;
		IO_Serial_Write(reader, 0, 1000000, 4, data);
	}

	data[0]=0x02;
	data[1]=HIBYTE(freqk);
	data[2]=LOBYTE(freqk);
	IO_Serial_Write(reader, 0, 1000000, 3, data);

	data[0]=0x03;
	data[1]=Ni;
	IO_Serial_Write(reader, 0, 1000000, 2, data);

	data[0]=0x04;
	data[1]=T;
	IO_Serial_Write(reader, 0, 1000000, 2, data);

	data[0]=0x05;
	data[1]=0; //always done by oscam
	IO_Serial_Write(reader, 0, 1000000, 2, data);

	cs_sleepms(DELAY);

	tcgetattr(reader->handle, &term);
	term.c_cflag &= ~CSIZE;
	term.c_cflag |= CS8;
	tcsetattr(reader->handle, TCSANOW, &term);

	return OK;
}

static int32_t smargo_writesettings(struct s_reader *reader, uint32_t UNUSED(ETU), uint32_t UNUSED(EGT), unsigned char UNUSED(P), unsigned char UNUSED(I), uint16_t Fi, unsigned char Di, unsigned char Ni) {
	smargo_set_settings(reader, reader->mhz, reader->protocol_type == 1 ? 0 : reader->protocol_type , reader->convention, Fi, Di, Ni);
	return OK;
}


static int32_t smargo_init(struct s_reader *reader) {
	rdr_log(reader, "smargo init");
	reader->handle = open (reader->device,  O_RDWR);
	if (reader->handle < 0) {
		rdr_log(reader, "ERROR: Opening device %s (errno=%d %s)",reader->device, errno, strerror(errno));
		return ERROR;
	}

	return OK;
}

bool IO_Serial_WaitToRead (struct s_reader * reader, uint32_t delay_ms, uint32_t timeout_ms);
int32_t smargo_Serial_Read(struct s_reader * reader, uint32_t timeout, uint32_t size, unsigned char * data, int32_t *read_bytes)
{
	unsigned char c;
	uint32_t count = 0;
	
	for (count = 0; count < size ; count++)
	{
		if (!IO_Serial_WaitToRead (reader, 0, timeout))
		{
			if (read (reader->handle, &c, 1) != 1)
			{
				int saved_errno = errno;
				rdr_ddump_mask(reader, D_DEVICE, data, count, "Receiving:");
				rdr_log(reader, "ERROR: %s (errno=%d %s)", __func__, saved_errno, strerror(saved_errno));
				return ERROR;
			}
		}
		else
		{
			rdr_ddump_mask(reader, D_DEVICE, data, count, "Receiving:");
			rdr_debug_mask(reader, D_DEVICE, "TIMEOUT in IO_Serial_Read");
			*read_bytes=count;
			return ERROR;
		}
		data[count] = c;
	}
	rdr_ddump_mask(reader, D_DEVICE, data, count, "Receiving:");
	return OK;
}


static int32_t smargo_reset(struct s_reader *reader, ATR *atr) {
	rdr_debug_mask(reader, D_IFD, "Smargo: Resetting card");
	int32_t ret=ERROR;
	int32_t i;
	unsigned char buf[ATR_MAX_SIZE];

	int32_t parity[4] = {PARITY_EVEN, PARITY_ODD, PARITY_NONE, PARITY_EVEN};

	int32_t mhz = 369;

	if(reader->mhz == reader->cardmhz && reader->cardmhz > 369)
		mhz = reader->cardmhz;

	for(i=0; i<4; i++) {
		if (i==3) // hack for irdeto cards
			smargo_set_settings(reader, 600, 1, 0, 618, 1, 0);
		else
			smargo_set_settings(reader, mhz, 0, 0, 372, 1, 0);

		call (IO_Serial_SetParity (reader, parity[i]));

		//IO_Serial_Flush(reader);

		IO_Serial_Read(reader, 0, 500000, ATR_MAX_SIZE, buf);

		IO_Serial_RTS_Set(reader);
		cs_sleepms(150);
		IO_Serial_RTS_Clr(reader);

		int32_t n=0;
		//while(n<ATR_MAX_SIZE && !IO_Serial_Read(reader, ATR_TIMEOUT, 1, buf+n))
		//	n++;

		smargo_Serial_Read(reader, ATR_TIMEOUT, ATR_MAX_SIZE, buf, &n);
		
		if(n==0 || buf[0]==0)
			continue;

		rdr_ddump_mask(reader, D_IFD, buf, n, "Smargo ATR: %d bytes", n);

		if((buf[0]!=0x3B && buf[0]!=0x03 && buf[0]!=0x3F) || (buf[1]==0xFF && buf[2]==0x00))
			continue; // this is not a valid ATR

		if (ATR_InitFromArray (atr, buf, n) != ERROR)
			ret = OK;

		if (ret == OK)
			break;
	}

	return ret;
}

static int32_t smargo_receive(struct s_reader *reader, unsigned char *data, uint32_t size, uint32_t delay, uint32_t timeout) {
	return Phoenix_Receive(reader, data, size, delay, timeout);
}

static int32_t smargo_transmit(struct s_reader *reader, unsigned char *sent, uint32_t size, uint32_t delay, uint32_t timeout) {
	return Phoenix_Transmit(reader, sent, size, delay, timeout);
}

void cardreader_smargo(struct s_cardreader *crdr) 
{
	crdr->desc		= "smargo";
	crdr->reader_init	= smargo_init;
	crdr->get_status	= Phoenix_GetStatus;
	crdr->activate	= smargo_reset;
	crdr->transmit	= smargo_transmit;
	crdr->receive		= smargo_receive;
	crdr->close		= Phoenix_Close;
	crdr->write_settings = smargo_writesettings;
	crdr->typ		= R_MOUSE;

	crdr->need_inverse	= 1;
}
#endif
