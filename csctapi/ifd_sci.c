/*
		ifd_sci.c
		This module provides IFD handling functions for SCI internal reader.
*/

#include "../globals.h"

#ifdef WITH_CARDREADER

#include "../oscam-time.h"

#include "atr.h"
#include "ifd_sci.h"
#include "io_serial.h"
#include "sci_global.h"
#include "sci_ioctl.h"

#undef ATR_TIMEOUT
#define ATR_TIMEOUT   800

#define OK 		0 
#define ERROR 1

int32_t Sci_GetStatus (struct s_reader * reader, int32_t * status)
{
	ioctl(reader->handle, IOCTL_GET_IS_CARD_PRESENT, status);
	return OK;
}

int32_t Sci_Reset(struct s_reader * reader, ATR * atr)
{
	int32_t ret;

	rdr_debug_mask(reader, D_IFD, "Reset internal cardreader!");
	SCI_PARAMETERS params;
	
	memset(&params,0,sizeof(SCI_PARAMETERS));
	
	params.ETU = 372; //initial ETU (in iso this parameter F)
	params.EGT = 3; //initial guardtime should be 0 (in iso this is parameter N)
	params.fs = 5; //initial cardmhz should be 1 (in iso this is parameter D)
	params.T = 0;
	if (reader->mhz > 2000) { // PLL based reader
		params.ETU = 372;
		params.EGT = 0;
		params.fs = (int32_t) (reader->mhz / 100.0 + 0.5); /* calculate divider for 1 MHz  */
		params.T = 0;
	}
	if (reader->mhz == 8300) { /* PLL based reader DM7025 */
		params.ETU = 372;
		params.EGT = 0;
		params.fs = 16; /* read from table setting for 1 MHz:
		params.fs = 6 for cardmhz = 5.188 Mhz
		params.fs = 7 for cardmhz = 4.611 MHz
		params.fs = 8 for cardmhz = 3.953 MHz
		params.fs = 9 for cardmhz = 3.609 MHz
		params.fs = 10 for cardmhz = 3.192 MHz
		params.fs = 11 for cardmhz = 2.965 MHz
		params.fs = 12 for cardmhz = 2.677 MHz
		params.fs = 13 for cardmhz = 2.441 MHz
		params.fs = 14 for cardmhz = 2.306 MHz
		params.fs = 15 for cardmhz = 2.128 MHz
		params.fs = 16 for cardmhz = 1.977 MHz */
		params.T = 0;
	}
	ioctl(reader->handle, IOCTL_SET_PARAMETERS, &params);
	ioctl(reader->handle, IOCTL_SET_RESET, 1);
	ret = Sci_Read_ATR(reader, atr);
	ioctl(reader->handle, IOCTL_SET_ATR_READY, 1);
	return ret;
}

int32_t Sci_Read_ATR(struct s_reader * reader, ATR * atr) // reads ATR on the fly: reading and some low levelchecking at the same time
{
	uint32_t timeout = ATR_TIMEOUT;
	unsigned char buf[SCI_MAX_ATR_SIZE];
	int32_t n = 0, statusreturn =0;
	do {
		ioctl(reader->handle, IOCTL_GET_ATR_STATUS, &statusreturn);
		rdr_debug_mask(reader, D_IFD, "Waiting for card ATR Response...");
	}
		while (statusreturn);
	
	if (reader->mhz > 2000)           // pll readers use timings in us
		timeout = timeout * 1000;
	if (IO_Serial_Read(reader, timeout, 1, buf+n)){ //read first char of atr
		rdr_debug_mask(reader, D_IFD, "ERROR: no characters found in ATR");
		return ERROR;
	}
	int32_t inverse = 0; // card is using inversion?
	if (buf[0] == 0x03){
		inverse = 1;
		buf[n] = ~(INVERT_BYTE (buf[n]));
	}
	n++;
	if (IO_Serial_Read(reader, timeout, 1, buf+n)){
		rdr_debug_mask(reader, D_IFD, "ERROR: only 1 character found in ATR");
		return ERROR;
	}
	if (inverse) buf[n] = ~(INVERT_BYTE (buf[n]));
	int32_t TDi = buf[n];
	int32_t historicalbytes = TDi&0x0F;
	rdr_debug_mask(reader, D_ATR, "ATR historicalbytes should be: %d", historicalbytes);
	rdr_debug_mask(reader, D_ATR, "Fetching global interface characters for protocol T0"); // protocol T0 always aboard!
	n++;
	
	int32_t protocols = 1, tck = 0, protocol, protocolnumber;	// protocols = total protocols on card, tck = checksum byte present, protocol = mandatory protocol
	int32_t D = 0;												// protocolnumber = TDi uses protocolnumber
	
	while (n < SCI_MAX_ATR_SIZE){
		if (TDi&0x10){  //TA Present: 							   //The value of TA(i) is always interpreted as XI || UI if i > 2 and T = 15 ='F'in TD(i�1)
			if (IO_Serial_Read(reader, timeout, 1, buf+n)) break;  //In this case, TA(i) contains the clock stop indicator XI, which indicates the logical
			if (inverse) buf[n] = ~(INVERT_BYTE (buf[n]));		   //state the clockline must assume when the clock is stopped, and the class indicator UI,
			rdr_debug_mask(reader, D_ATR, "TA%d: %02X",protocols,buf[n]);      //which specifies the supply voltage class.
			if ((protocols >2) && ((TDi&0x0F)==0x0F)){  // Protocol T15 does not exists, it means mandatory on all ATRs
				if((buf[n]&0xC0) == 0xC0) rdr_debug_mask(reader, D_ATR, "Clockline low or high on clockstop");
				if((buf[n]&0xC0) == 0x00) rdr_debug_mask(reader, D_ATR, "Clockline not supported on clockstop");
				if((buf[n]&0xC0) == 0x40) rdr_debug_mask(reader, D_ATR, "Clockline should be low on clockstop");
				if((buf[n]&0xC0) == 0x80) rdr_debug_mask(reader, D_ATR, "Clockline should be high on clockstop");
				if((buf[n]&0x3F) == 0x01) rdr_debug_mask(reader, D_ATR, "Voltage class A 4.5~5.5V");
				if((buf[n]&0x3F) == 0x02) rdr_debug_mask(reader, D_ATR, "Voltage class B 2.7~3.3V");
				if((buf[n]&0x3F) == 0x03) rdr_debug_mask(reader, D_ATR, "Voltage class A 4.5~5.5V and class B 2.7~3.3V");
				if((buf[n]&0x3F) == 0x04) rdr_debug_mask(reader, D_ATR, "Voltage RFU");
			}
			if ((protocols >2) && ((TDi&0x0F)==0x01)){  // Protocol T1 specfic (There is always an obsolete T0 protocol!)
				int32_t ifsc = buf[n];
				if (ifsc == 0x00) ifsc = 32; //default is 32
				rdr_debug_mask(reader, D_ATR, "Maximum information field length this card can receive is %d bytes (IFSC)", ifsc);
			}
			else {
				int32_t FI = (buf[n]>>4); // FI is high nibble                  ***** work ETU = (1/D)*(Frequencydivider/cardfrequency) (in seconds!)
				int32_t F = atr_f_table[FI]; // lookup the frequency divider
				float fmax = atr_fs_table[FI]; // lookup the max frequency      ***** initial ETU = 372 / initial frequency during atr  (in seconds!)
				
				int32_t DI = (buf[n]&0x0F); // DI is low nibble
				D = atr_d_table[DI]; // lookup the bitrate adjustment (yeah there are floats in it, but in iso only integers!?)
				rdr_debug_mask(reader, D_ATR, "Advertised max cardfrequency is %.2f (Fmax), frequency divider is %d (F)", fmax/1000000L, F); // High nibble TA1 contains cardspeed
				rdr_debug_mask(reader, D_ATR, "Bitrate adjustment is %d (D)", D); // Low nibble TA1 contains Bitrateadjustment
				rdr_debug_mask(reader, D_ATR, "Work ETU = %ld nanoseconds", (long int) ((1/D)*(F/fmax)*1000000L)); // And display it...
				rdr_debug_mask(reader, D_ATR, "Initial ETU = %ld nanoseconds", (long int) (372/fmax)*1000000L); // And display it... since D=1 and frequency during ATR fetch might be different!
			} 
			if (protocols == 2){
				if((buf[n]&0x80)==0x80) rdr_debug_mask(reader, D_ATR, "Switching between negotiable mode and specific mode is not possible");
				else { 
					rdr_debug_mask(reader, D_ATR, "Switching between negotiable mode and specific mode is possible");
					// int32_t PPS = 1; Stupid compiler, will need it later on eventually
				}
				if((buf[n]&0x01)==0x01) rdr_debug_mask(reader, D_ATR, "Transmission parameters implicitly defined in the interface characters.");
				else rdr_debug_mask(reader, D_ATR, "Transmission parameters explicitly defined in the interface characters.");
				
				protocol = buf[n]&0x0F;
				if(protocol) rdr_debug_mask(reader, D_ATR, "Protocol T = %d is to be used!", protocol);
			}
			n++; // next interface character
		}
		if (TDi&0x20){	 //TB Present
			if (IO_Serial_Read(reader, timeout, 1, buf+n)) break;
			if (inverse) buf[n] = ~(INVERT_BYTE (buf[n]));
			rdr_debug_mask(reader, D_ATR, "TB%d: %02X",protocols,buf[n]);
			if ((protocols >2) && ((TDi&0x0F)==0x01)){  // Protocol T1 specfic (There is always an obsolete T0 protocol!)
				int32_t CWI = (buf[n]&0x0F); // low nibble contains CWI code for the character waiting time CWT
				int32_t BWI = (buf[n]>>4); // high nibble contains BWI code for the block waiting time BWT
				int32_t CWT = (1<<CWI) + 11; // in work etu  *** 2^CWI + 11 work etu *** 
				rdr_debug_mask(reader, D_ATR, "Protocol T1: Character waiting time is %d work etu (CWT)", CWT);
				int32_t BWT = (int) ((1<<BWI) * 960L * 372L); // divided by frequency and add with 11 work etu *** 2^BWI*960*372/f + 11 work etu ***
				rdr_debug_mask(reader, D_ATR, "Protocol T1: Block waiting time is %d divided by actual cardfrequency + 11 work etu (BWI)", BWI);
				rdr_debug_mask(reader, D_ATR, "Protocol T1: BWT: %d", BWT);
			}
			
			n++; // next interface character
		}
		if (TDi&0x40){	 //TC Present
			if (IO_Serial_Read(reader, timeout, 1, buf+n)) break;
			if (inverse) buf[n] = ~(INVERT_BYTE (buf[n]));
			rdr_debug_mask(reader, D_ATR, "TC%d: %02X",protocols, buf[n]);
			if ((protocols == 2) && ((TDi&0x0F)==0x00)){
				int32_t WI = buf[n];
				rdr_debug_mask(reader, D_ATR, "Protocol T0: work wait time is %d work etu (WWT)", (int) (960*D*WI));
			}
			if ((protocols == 2) && ((TDi&0x0F)==0x01)){
				if(buf[n]&0x01) rdr_debug_mask(reader, D_ATR, "Protocol T1: CRC is used to compute the error detection code"); 
				else rdr_debug_mask(reader, D_ATR, "Protocol T1: LRC is used to compute the error detection code"); 
			}
			if(buf[n]<0xFF) rdr_debug_mask(reader, D_ATR, "Extra guardtime of %d ETU (N)", (int) buf[n]);
			if(buf[n]==0xFF) rdr_debug_mask(reader, D_ATR, "Protocol T1: Standard 2 ETU guardtime is lowered to 1 ETU");
			
			n++; // next interface character
		}
		if (TDi&0x80){	//TD Present? Get next TDi there will be a next protocol
			if (IO_Serial_Read(reader, timeout, 1, buf+n)) break;
			if (inverse) buf[n] = ~(INVERT_BYTE (buf[n]));
			rdr_debug_mask(reader, D_ATR, "TD%d %02X",protocols,buf[n]);
			TDi = buf[n];
			protocolnumber = TDi&0x0F;
			if (protocolnumber == 0x00 || protocolnumber == 0x0E) tck = 0; // T0 and T14 protocol do not use tck byte  (TCK = checksum byte!)
			if (protocolnumber == 0x01) tck = 1; // T1 protocol tck byte is mandatory, BTW: this code doesnt calculate if the TCK is valid jet... 
			rdr_debug_mask(reader, D_ATR, "Fetching global interface characters for protocol T%d:", (TDi&0x0F)); // lower nibble contains protocol number
			protocols++; // there is always 1 protocol T0 in every ATR as per iso defined, max is 16 (numbered 0..15)
			
			n++; // next interface character
		}
		else break;
	}
	int32_t atrlength = 0;
	atrlength += n;
	atrlength += historicalbytes;
	rdr_debug_mask(reader, D_ATR, "Total ATR Length including %d historical bytes should be %d",historicalbytes,atrlength);
	rdr_debug_mask(reader, D_ATR, "Total protocols in this ATR is %d",protocols);

	while(n < atrlength + tck){ // read all the rest and mandatory tck byte if other protocol than T0 is used.
		if (IO_Serial_Read(reader, timeout, 1, buf+n)) break;	
		if (inverse) buf[n] = ~(INVERT_BYTE (buf[n]));
		n++;
	}

	if (n!=atrlength+tck) cs_log("Warning reader %s: Total ATR characters received is: %d instead of expected %d", reader->label, n, atrlength+tck);

	if ((buf[0] !=0x3B) && (buf[0] != 0x3F) && (n>9 && !memcmp(buf+4, "IRDETO", 6))) //irdeto S02 reports FD as first byte on dreambox SCI, not sure about SH4 or phoenix
		buf[0] = 0x3B;

	statusreturn = ATR_InitFromArray (atr, buf, n); // n should be same as atrlength but in case of atr read error its less so do not use atrlenght here!

	if (statusreturn == ATR_MALFORMED) cs_log("Warning reader %s: ATR is malformed, you better inspect it with a -d2 log!", reader->label);

	if (statusreturn == ERROR){
		cs_log("Warning reader %s: ATR is invalid!", reader->label);
		return ERROR;
	}
	return OK; // return OK but atr might be softfailing!
}

int32_t Sci_WriteSettings (struct s_reader * reader, unsigned char T, uint32_t fs, uint32_t ETU, uint32_t WWT, uint32_t BWT, uint32_t CWT, uint32_t EGT, unsigned char P, unsigned char I)
{
	//int32_t n;
	SCI_PARAMETERS params;
	//memset(&params,0,sizeof(SCI_PARAMETERS));
	ioctl(reader->handle, IOCTL_GET_PARAMETERS, &params);
	params.T = T;
	params.fs = fs;

	//for Irdeto T14 cards, do not set ETU
	if (ETU)
		params.ETU = ETU;
	params.EGT = EGT;
	params.WWT = WWT;
	params.BWT = BWT;
	params.CWT = CWT;
	if (P)
		params.P = P;
	if (I)
		params.I = I;

	rdr_debug_mask(reader, D_IFD, "Setting reader T=%d fs=%d ETU=%d WWT=%d CWT=%d BWT=%d EGT=%d clock=%d check=%d P=%d I=%d U=%d",
		(int)params.T, params.fs, (int)params.ETU, (int)params.WWT,
		(int)params.CWT, (int)params.BWT, (int)params.EGT,
		(int)params.clock_stop_polarity, (int)params.check,
		(int)params.P, (int)params.I, (int)params.U);

	ioctl(reader->handle, IOCTL_SET_PARAMETERS, &params);
	return OK;
}

#if defined(__SH4__)
#define __IOCTL_CARD_ACTIVATED IOCTL_GET_IS_CARD_PRESENT
#else
#define __IOCTL_CARD_ACTIVATED IOCTL_GET_IS_CARD_ACTIVATED
#endif

int32_t Sci_Activate (struct s_reader * reader)
{
	rdr_debug_mask(reader, D_IFD, "Activating card");
	uint32_t in = 1;
	rdr_debug_mask(reader, D_IFD, "Is card activated?");
	ioctl(reader->handle, IOCTL_GET_IS_CARD_PRESENT, &in);
	ioctl(reader->handle, __IOCTL_CARD_ACTIVATED, &in);
	return OK;
}

int32_t Sci_Deactivate (struct s_reader * reader)
{
	rdr_debug_mask(reader, D_IFD, "Deactivating card");
	ioctl(reader->handle, IOCTL_SET_DEACTIVATE);	
	return OK;
}

int32_t Sci_FastReset (struct s_reader *reader, ATR * atr)
{
	int32_t ret;
	ioctl(reader->handle, IOCTL_SET_RESET, 1);
	ret = Sci_Read_ATR(reader, atr);
	ioctl(reader->handle, IOCTL_SET_ATR_READY, 1);

	return ret;
}
#endif
