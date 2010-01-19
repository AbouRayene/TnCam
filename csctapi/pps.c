/*
    pps.c
    Protocol Parameters Selection
  
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

#include "pps.h"
#include "atr.h"
#include "protocol_t0.h"
#include "protocol_t1.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "ifd.h"
#include "../globals.h"

/*
 * Not exported constants definition
 */

#define PPS_DEFAULT_PROTOCOL	0x00

/*
 * Not exported macros definition
 */

#define PPS_HAS_PPS1(block)	((block[1] & 0x10) == 0x10)
#define PPS_HAS_PPS2(block)	((block[1] & 0x20) == 0x20)
#define PPS_HAS_PPS3(block)	((block[1] & 0x40) == 0x40)

/*
 * Not exported funtions declaration
 */

static int PPS_Exchange (BYTE * params, unsigned *length);

static bool PPS_Match (BYTE * request, unsigned len_request, BYTE * reply, unsigned len_reply);

static unsigned PPS_GetLength (BYTE * block);

static int PPS_InitICC ();

static int PPS_InitProtocol ();

static BYTE PPS_GetPCK (BYTE * block, unsigned length);

/*
 * Exported functions definition
 */

PPS * PPS_New ()
{
	PPS *pps;
	
	pps = (PPS *) malloc (sizeof (PPS));
	
	if (pps != NULL)
	{
		protocol = NULL;
		parameters.t = PPS_DEFAULT_PROTOCOL;
		parameters.FI = ATR_DEFAULT_FI;
		parameters.d = ATR_DEFAULT_D;
		parameters.n = ATR_DEFAULT_N;
	}
	
	return pps;
}

int PPS_Perform (PPS * pps, BYTE * params, unsigned *length)
{
	//Performs PPS Exchange on command when *length >0
	//If unsuccesfull, or when length <= 0
	//Gets parametes from ATR
	//If necessary perform PPS session
	//
	//Output is pps->params.FI,n,d,t set with correct values
	//and switched SC-device conform these values to correct baudrate
	//
	//We need to store FI instread of F, because SCI_DEV works with FI
	//and it is easier to overclock then
	//also from FI -> F is easy, other way around not
	
	int ret;
	bool PPS_success; 
	
	/* Perform PPS Exchange if requested by command */
	if ((*length) > 0)
	{
		ret = PPS_Exchange (params, length);
		
		/* Get parameters from PPS handsake */
		if (ret == PPS_OK)
		{
			parameters.t = params[1] & 0x0F;
			
			if (PPS_HAS_PPS1 (params))
			{
				parameters.FI = (params[2] >> 4);
				parameters.d = atr_d_table[(params[2] & 0x0F)];
			}
			
/*			
			ret  = PPS_InitICC(pps);
			
			if (ret != PPS_OK)
				return ret;
*/
		}
/*		
		else
		{
			return ret;
		}
*/
	}
	PPS_success = PPS_OK;
	int protocol_selected = 0; //stores which TAi,TBi etc. bytes must be used 0 means not set
	if ((*length) <= 0 || !PPS_success) // If not by command, or PPS Exchange by command failed: Try PPS Exchange by ATR or Get parameters from ATR
	{
		int numprot = atr->pn;
		//if there is a trailing TD, this number is one too high
		BYTE tx;
		if (ATR_GetInterfaceByte (atr, numprot-1, ATR_INTERFACE_BYTE_TD, &tx) == ATR_OK)
			if ((tx & 0xF0) == 0)
				numprot--;
	  cs_debug("ATR reports %i protocol lines:",numprot);
		int i,point;
		char txt[50];
		bool OffersT[3]; //T14 stored as T2
		for (i = 0; i <= 2; i++)
			OffersT[i] = FALSE;
		for (i=1; i<= numprot; i++) {
			point = 0;
			if (ATR_GetInterfaceByte (atr, i, ATR_INTERFACE_BYTE_TA, &tx) == ATR_OK) {
			  sprintf((char *)txt+point,"TA%i=%02X ",i,tx);
				point +=7;
			}
			if (ATR_GetInterfaceByte (atr, i, ATR_INTERFACE_BYTE_TB, &tx) == ATR_OK) {
			  sprintf((char *)txt+point,"TB%i=%02X ",i,tx);
				point +=7;
			}
			if (ATR_GetInterfaceByte (atr, i, ATR_INTERFACE_BYTE_TC, &tx) == ATR_OK) {
			  sprintf((char *)txt+point,"TC%i=%02X ",i,tx);
				point +=7;
			}
			if (ATR_GetInterfaceByte (atr, i, ATR_INTERFACE_BYTE_TD, &tx) == ATR_OK) {
			  sprintf((char *)txt+point,"TD%i=%02X ",i,tx);
				point +=7;
				tx &= 0X0F;
			  sprintf((char *)txt+point,"(T%i)",tx);
				if (tx == 14)
					OffersT[2] = TRUE;
				else
					OffersT[tx] = TRUE;
			}
			else {
				sprintf((char *)txt+point,"no TD%i means T0",i);
				OffersT[0] = TRUE;
			}
			cs_debug("%s",txt);
		}
    
		int numprottype = 0;
		for (i = 0; i <= 2; i++)
			if (OffersT[i])
				numprottype ++;
		cs_debug("%i protocol types detected. Historical bytes: %s",numprottype, cs_hexdump(1,atr->hb,atr->hbn));

//If more than one protocol type and/or TA1 parameter values other than the default values and/or N equeal to 255 is/are indicated in the answer to reset, the card shall know unambiguously, after having sent the answer to reset, which protocol type or/and transmission parameter values (FI, D, N) will be used. Consequently a selection of the protocol type and/or the transmission parameters values shall be specified.
		ATR_GetParameter (atr, ATR_PARAMETER_N, &(parameters.n));
		ATR_GetProtocolType(atr,1,&(parameters.t)); //get protocol from TD1
		BYTE TA2;
		bool SpecificMode = (ATR_GetInterfaceByte (atr, 2, ATR_INTERFACE_BYTE_TA, &TA2) == ATR_OK); //if TA2 present, specific mode, else negotiable mode
		if (SpecificMode) {
			parameters.t = TA2 & 0x0F;
			if ((TA2 & 0x10) != 0x10) { //bit 5 set to 0 means F and D explicitly defined in interface characters
				BYTE TA1;
				if (ATR_GetInterfaceByte (atr, 1 , ATR_INTERFACE_BYTE_TA, &TA1) == ATR_OK) {
					parameters.FI = TA1 >> 4;
					ATR_GetParameter (atr, ATR_PARAMETER_D, &(parameters.d));
				}
				else {
					parameters.FI = ATR_DEFAULT_FI;
					parameters.d = ATR_DEFAULT_D;
				}
			}
			else {
				cs_log("Specific mode: speed 'implicitly defined', not sure how to proceed, assuming default values");
				parameters.FI = ATR_DEFAULT_FI;
				parameters.d = ATR_DEFAULT_D;
			}
			cs_debug("Specific mode: T%i, F=%.0f, D=%.6f, N=%.0f\n", parameters.t, (double) atr_f_table[parameters.FI], parameters.d, parameters.n);
		}
		else { //negotiable mode

			bool NeedsPTS = ((parameters.t != 14) && (numprottype > 1 || (atr->ib[0][ATR_INTERFACE_BYTE_TA].present == TRUE && atr->ib[0][ATR_INTERFACE_BYTE_TA].value != 0x11) || parameters.n == 255)); //needs PTS according to ISO 7816 , SCI gets stuck on our PTS
			if (NeedsPTS) {
				//             PTSS  PTS0  PTS1  PTS2  PTS3  PCK
				//             PTSS  PTS0  PTS1  PCK
				BYTE req[] = { 0xFF, 0x10, 0x00, 0x00 }; //we currently do not support PTS2, standard guardtimes
				req[1]=0x10 | parameters.t; //PTS0 always flags PTS1 to be sent always
				if (ATR_GetInterfaceByte (atr, 1, ATR_INTERFACE_BYTE_TA, &req[2]) != ATR_OK)  //PTS1 
					req[2] = 0x11; //defaults FI and DI to 1
	  		//req[3]=PPS_GetPCK(req,sizeof(req)-1); will be set by PPS_Exchange
				unsigned int len = sizeof(req);
				ret = PPS_Exchange (req, &len);
		  	if (ret == PPS_OK) {
					parameters.FI = req[2] >> 4;
					BYTE DI = req[2] & 0x0F;
					parameters.d = (double) (atr_d_table[DI]);
					PPS_success = TRUE;
					cs_debug("PTS Succesfull, selected protocol: T%i, F=%.0f, D=%.6f, N=%.0f\n", parameters.t, (double) atr_f_table[parameters.FI], parameters.d, parameters.n);
				}
				else
					cs_ddump(req,4,"PTS Failure, response:");
			}

			//FIXME Currently InitICC sets baudrate to 9600 for all T14 cards (=no switching); 
			//When for SCI, T14 protocol, TA1 is obeyed, this goes OK for mosts devices, but somehow on DM7025 Sky S02 card goes wrong when setting ETU (ok on DM800/DM8000)
			if (!PPS_success) {//last PPS not succesfull
				BYTE TA1;
				if (ATR_GetInterfaceByte (atr, 1 , ATR_INTERFACE_BYTE_TA, &TA1) == ATR_OK) {
					parameters.FI = TA1 >> 4;
					ATR_GetParameter (atr, ATR_PARAMETER_D, &(parameters.d));
				}
				else { //do not obey TA1
					parameters.FI = ATR_DEFAULT_FI;
					parameters.d = ATR_DEFAULT_D;
				}
				ATR_GetProtocolType (atr, 1, &(parameters.t));
				protocol_selected = 1;
	
				if (NeedsPTS) { 
					if ((parameters.d == 32) || (parameters.d == 12) || (parameters.d == 20))
						parameters.d = 0; //behave conform "old" atr_d_table; viaccess cards that fail PTS need this
				}
				/////Here all non-ISO behaviour
				/////End  all non-ISO behaviour

				cs_debug("No PTS %s, selected protocol T%i, F=%.0f, D=%.6f, N=%.0f\n", NeedsPTS?"happened":"needed", parameters.t, (double) atr_f_table[parameters.FI], parameters.d, parameters.n);
			}
		}//end negotiable mode
	}//end length<0
		
	//make sure no zero values
	double F =  (double) atr_f_table[parameters.FI];
	if (!F) {
		parameters.FI = ATR_DEFAULT_FI;
		cs_log("Warning: F=0 is invalid, forcing FI=%d", parameters.FI);
	}
	if (!parameters.d) {
		parameters.d = ATR_DEFAULT_D;
		cs_log("Warning: D=0 is invalid, forcing D=%.0f",parameters.d);
	}

	protocol_type = parameters.t;
	
#ifdef DEBUG_PROTOCOL
	printf("PPS: T=%i, F=%.0f, D=%.6f, N=%.0f\n", 
	parameters.t, 
	F, 
	parameters.d, 
	parameters.n);
#endif

	ret  = PPS_InitICC(pps);
			
	if (ret != PPS_OK)
		return ret;
	
	/* Initialize selected protocol with selected parameters */
	return PPS_InitProtocol (); 
}

void * PPS_GetProtocol ()
{
	return protocol;
}

PPS_ProtocolParameters *PPS_GetProtocolParameters ()
{
	/* User must Remember not to reference this struct after removing PPS */
	return &(parameters);
}

void PPS_Delete (PPS * pps)
{
	free (pps);
}

/*
 * Not exported funtions definition
 */

static int PPS_Exchange (BYTE * params, unsigned *length)
{
	BYTE confirm[PPS_MAX_LENGTH];
	unsigned len_request, len_confirm;
	int ret;
#ifdef DEBUG_PROTOCOL
	int i;
#endif
	
	len_request = PPS_GetLength (params);
	params[len_request - 1] = PPS_GetPCK(params, len_request - 1);
	
#ifdef DEBUG_PROTOCOL
	printf ("PPS: Sending request: ");
	for (i = 0; i < len_request; i++)
		printf ("%X ", params[i]);
	printf ("\n");
#endif

	cs_debug("PTS: Sending request: %s", cs_hexdump(1, params, len_request));

	/* Send PPS request */
#ifdef COOL
	//unsigned char ptsAck[10];
	//u_int8 ptsLen = len_request;
	unsigned short int ptsLen = len_request;
	int Status = cnxt_smc_start_pps(handle, params, confirm, &ptsLen, TRUE);
	printf ("cnxt_smc_start_pps Status=%i\n", Status);
	len_confirm = ptsLen;
#ifdef DEBUG_PROTOCOL
	printf("COOL: confirm: \n");
	for (i = 0; i < ptsLen; i++)
		printf ("%02X", confirm[i]);
	printf ("\n");
	fflush(stdout);
	printf("COOL: req: \n");
	for (i = 0; i < len_request; i++)
		printf ("%02X", params[i]);
	printf ("\n");
	fflush(stdout);
#endif
	if (Status)
		return PPS_HANDSAKE_ERROR;
#else
	if (ICC_Async_Transmit (len_request, params) != ICC_ASYNC_OK)
		return PPS_ICC_ERROR;
	
	/* Get PPS confirm */
	if (ICC_Async_Receive (2, confirm) != ICC_ASYNC_OK)
		return PPS_ICC_ERROR;
	
	len_confirm = PPS_GetLength (confirm);
	
	if (ICC_Async_Receive (len_confirm - 2, confirm + 2) != ICC_ASYNC_OK)
		return PPS_ICC_ERROR;
	
#ifdef DEBUG_PROTOCOL
	printf ("PPS: Receivig confirm: ");
	for (i = 0; i < len_confirm; i++)
		printf ("%X ", confirm[i]);
	printf ("\n");
#endif
	
	cs_debug("PTS: Receiving confirm: %s", cs_hexdump(1, confirm, len_confirm));
	
	if (!PPS_Match (params, len_request, confirm, len_confirm))
		ret = PPS_HANDSAKE_ERROR;
	else
		ret = PPS_OK;
#endif
	
	/* Copy PPS handsake */
	memcpy (params, confirm, len_confirm);
	(*length) = len_confirm;
	
	return ret;
}

static bool PPS_Match (BYTE * request, unsigned len_request, BYTE * confirm, unsigned len_confirm)
{
	/* See if the reply differs from request */
	if ((len_request != len_confirm) || (memcmp (request, confirm, len_request)))
	{
		/* See if the card specifies other than default FI and D */
		//if ((PPS_HAS_PPS1 (confirm)) && (confirm[2] != request[2]))
			return FALSE;
	}
	
	return TRUE;
}

static unsigned PPS_GetLength (BYTE * block)
{
	unsigned length = 3;
	
	if (PPS_HAS_PPS1 (block))
	length++;
	
	if (PPS_HAS_PPS2 (block))
	length++;
	
	if (PPS_HAS_PPS3 (block))
	length++;
	
	return length;
}

static int PPS_InitICC ()
{
#ifdef SCI_DEV
#include <sys/ioctl.h>
#include "sci_global.h"
#include "sci_ioctl.h"
	if(reader[ridx].typ == R_INTERNAL)
	{
		int n;
		SCI_PARAMETERS params;
		//memset(&params,0,sizeof(SCI_PARAMETERS));
		if (ioctl(reader[ridx].handle, IOCTL_GET_PARAMETERS, &params) < 0 )
			return PPS_ICC_ERROR;

		params.T = parameters.t;
		params.fs = atr_fs_table[parameters.FI] / 1000000;
		double F =  (double) atr_f_table[parameters.FI];
		//for Irdeto T14 cards, do not set ETU
    if (!(atr->hbn >= 6 && !memcmp(atr->hb, "IRDETO", 6) && params.T == 14))
		  params.ETU = F / parameters.d;
		if (parameters.n == 255) //only for T0 or also for T1?
			params.EGT = 0;
		else
			params.EGT = parameters.n;

		double a;
		ATR_GetParameter(atr, ATR_PARAMETER_P, &a);
		params.P=(unsigned char)a;
		ATR_GetParameter(atr, ATR_PARAMETER_I, &a);
		params.I=(unsigned char)a;

		cs_debug("Setting T=%d fs=%lu mhz ETU=%d WWT=%d CWT=%d BWT=%d EGT=%d clock=%d check=%d P=%d I=%d U=%d", (int)params.T, params.fs, (int)params.ETU, (int)params.WWT, (int)params.CWT, (int)params.BWT, (int)params.EGT, (int)params.clock_stop_polarity, (int)params.check, (int)params.P, (int)params.I, (int)params.U);

		if (ioctl(reader[ridx].handle, IOCTL_SET_PARAMETERS, &params)!=0)
			return PPS_ICC_ERROR;
		
	}
#elif COOL
	if(reader[ridx].typ == R_INTERNAL) {
		int mhz = atr_fs_table[parameters.FI] / 10000;
		if (!Cool_SetBaudrate(mhz))
			return PPS_ICC_ERROR;
#ifdef DEBUG_PROTOCOL
		printf("Coolstream: set clock to %i * 10kHz\n", mhz);
#endif
		return PPS_OK;
	}
#endif
	{
	unsigned long baudrate;
	double F =  (double) atr_f_table[parameters.FI];
	if (parameters.t == 14)
		baudrate = 9600;
	else
		baudrate = parameters.d * ICC_Async_GetClockRate () / F; 

#ifdef DEBUG_PROTOCOL
	printf ("PPS: Baudrate = %d\n", (int)baudrate);
#endif
	

	if (ICC_Async_SetBaudrate (baudrate) != ICC_ASYNC_OK)
		return PPS_ICC_ERROR;
	
	return PPS_OK;
	}
}

static int PPS_InitProtocol ()
{
	int ret;
	
	if (parameters.t == ATR_PROTOCOL_TYPE_T0)
	{
			ret = Protocol_T0_Init (&(parameters));
			
			if (ret != PROTOCOL_T0_OK)
			{
				protocol = NULL;
				return PPS_PROTOCOL_ERROR;
			}
			
			return PPS_OK;
	}
	else if (parameters.t == ATR_PROTOCOL_TYPE_T1)
	{
			ret = Protocol_T1_Init ();
			
			if (ret != PROTOCOL_T1_OK)
			{
				protocol = NULL;
				return PPS_PROTOCOL_ERROR;
			}
			
			return PPS_OK;
	}
	else if (parameters.t == ATR_PROTOCOL_TYPE_T14)
	{
			ret = Protocol_T14_Init (&(parameters));
			
			if (ret != PROTOCOL_T14_OK)
			{
				protocol = NULL;
				return PPS_PROTOCOL_ERROR;
			}
			
			return PPS_OK;
	}
	else
	{
		protocol = NULL;
	}
	
	return PPS_PROTOCOL_ERROR;
}

static BYTE PPS_GetPCK (BYTE * block, unsigned length)
{
	BYTE pck;
	unsigned i;
	
	pck = block[0];
	for (i = 1; i < length; i++)
		pck ^= block[i];
	
	return pck;
}
