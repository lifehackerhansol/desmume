/*  Copyright (C) 2010 DeSmuME team

    This file is part of DeSmuME

    DeSmuME is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    DeSmuME is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with DeSmuME; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

#include "slot1comp_protocol.h"

#include <stdio.h>
#include <time.h>

#include "armcpu.h"
#include "MMU.h"
#include "../slot1.h"
#include "../NDSSystem.h"
#include "../emufile.h"

extern armcpu_t NDS_ARM7;
extern armcpu_t NDS_ARM9;

class Slot1_dstt : public ISlot1Interface, public ISlot1Comp_Protocol_Client
{
private:
	Slot1Comp_Protocol protocol;
	FILE *img;
	u32 write_count;
	u32 write_enabled;

	u32 sd_mode;
	u32 read_count;
	u32 dstt_sram[0x80000/4];
	u32 dstt_flash[0x80000/4];

public:
	Slot1_dstt()
		: img(NULL)
		, write_count(0)
		, write_enabled(0)
		, sd_mode(0)
		, read_count(0)
	{}

	virtual Slot1Info const* info() {
		static Slot1InfoSimple info("DSTT", "Slot1 DSTT Emulation", NDS_SLOT1_DSTT);
        return &info;
	}

	virtual bool init()
	{ 
		//strange to do this here but we need to make sure its done at some point
		srand(time(NULL));
		return true;
	}

	virtual void connect() {
		if (!img)
			img = fopen("DLDI_DSTT.img", "r+b");

		if(!img)
		{
			INFO("DLDI_DSTT.img not found\n");
		}

		protocol.reset(this);
	}

    //called when the emulator disconnects the device
    virtual void disconnect()
    {
        img = NULL;
    }

    //called when the emulator shuts down, or when the device disappears from existence
    virtual void shutdown()
    {
    }

	virtual void write_command(u8 PROCNUM, GC_Command command)
	{
		//protocol.write_command(command);
		protocol.command = command;
		slot1client_startOperation(eSlot1Operation_Unknown);
	}
	virtual void write_GCDATAIN(u8 PROCNUM, u32 val)
	{
		protocol.operation = eSlot1Operation_Unknown;
		protocol.write_GCDATAIN(PROCNUM, val);
	}
	virtual u32 read_GCDATAIN(u8 PROCNUM)
	{
		protocol.operation = eSlot1Operation_Unknown;
		return protocol.read_GCDATAIN(PROCNUM);
	}

	virtual void slot1client_startOperation(eSlot1Operation operation)
	{
		bool log=true;

		switch(protocol.command.bytes[0])
		{
			//case 0x00: // ?
			case 0x50: // busy wait?
			case 0x51: // set mode?
			case 0x52: // end?
			case 0x56: // write wait?
			case 0x80: // read wait?
			//case 0x86: // flash write enable?
			//case 0x87: // flash write 1 byte
			//case 0xB7: // read flash rom
			case 0x70: // read sram
			case 0x71: // write sram
			case 0xB8: // ?
				log = false;
				break;

			case 0x53: // read seek
			case 0x54: // multi read seek
				log = false;
				break;
			case 0x81: // read
			case 0x82: // write
				log = false;
				break;
		}

		switch(protocol.command.bytes[0])
		{
			case 0x00: // read flash rom
			case 0x50: // busy wait?
			case 0x52: // end?
			case 0x56: // write wait?
			case 0x80: // read wait?
			case 0x86: // flash write enable?
			case 0x87: // flash write 1 byte
			case 0xB7: // read flash rom
			case 0xB8: // ?
				break;

			case 0x51: // set mode?
			{
				sd_mode = protocol.command.bytes[6];
				//INFO("MODE = %d\n",sd_mode);
				switch(protocol.command.bytes[5])
				{
					case 0x00:
						break;
					case 0x0C:
						write_enabled = 0;
						break;
					case 0x11: //single sector read
					case 0x12: //multi sector read
					case 0x18: //single sector write
					case 0x19: //multi sector write
						protocol.address = (protocol.command.bytes[1] << 24) | (protocol.command.bytes[2] << 16) | (protocol.command.bytes[3] << 8) | protocol.command.bytes[4];
						//INFO("WRITE(%02X) %08X\n",protocol.command.bytes[0],protocol.address);
						if(img) fseek(img,protocol.address,SEEK_SET);
						INFO("CMD%02X %08X (MODE %d)\n",protocol.command.bytes[5],protocol.address,protocol.command.bytes[6]);
						//log = true;
						break;
					default:
						protocol.address = (protocol.command.bytes[1] << 24) | (protocol.command.bytes[2] << 16) | (protocol.command.bytes[3] << 8) | protocol.command.bytes[4];
						INFO("CMD%02X %08X (MODE %d)\n",protocol.command.bytes[5],protocol.address,protocol.command.bytes[6]);
						break;
				}
				//protocol.address = (protocol.command.bytes[1] << 24) | (protocol.command.bytes[2] << 16) | (protocol.command.bytes[3] << 8) | protocol.command.bytes[4];
				//INFO("CMD%02X %08X (MODE %d)\n",protocol.command.bytes[5],protocol.address,protocol.command.bytes[6]);
				break;
			}

			case 0x70: // read sram
				read_count = 0x80;
				break;
			case 0x71: // write sram
				write_enabled = 1;
				write_count = 0x80;
				break;

			case 0x53: // read seek
			case 0x54: // multi read seek
				protocol.address = (protocol.command.bytes[1] << 24) | (protocol.command.bytes[2] << 16) | (protocol.command.bytes[3] << 8) | protocol.command.bytes[4];
				INFO("SEEK(%02X) %08X\n",protocol.command.bytes[0],protocol.address);
				if(img) fseek(img,protocol.address,SEEK_SET);
				INFO("CMD%02X %08X (MODE %d)\n",protocol.command.bytes[0],protocol.address,sd_mode);
				//log = true;
				break;
			case 0x81: // read
				break;

			case 0x8F: // random
				break;

			case 0x82: // write
				write_enabled = 1;
				write_count = 0x80;
				break;
		}

		if(log)
		{
			INFO("WRITE CARD command: %02X%02X%02X%02X%02X%02X%02X%02X\t", 
							protocol.command.bytes[0], protocol.command.bytes[1], protocol.command.bytes[2], protocol.command.bytes[3],
							protocol.command.bytes[4], protocol.command.bytes[5], protocol.command.bytes[6], protocol.command.bytes[7]);
			INFO("FROM: %08X\t", (0 ? NDS_ARM7:NDS_ARM9).instruct_adr);
			//INFO("VAL: %08X\n", val);
		}
	}

	void slot1client_write_GCDATAIN(eSlot1Operation operation, u32 val)
	{
		bool log=false;

		switch(protocol.command.bytes[0])
		{
			case 0x71: // write sram
			{
				if(write_count && write_enabled)
				{
					protocol.address = (protocol.command.bytes[1] << 24) | (protocol.command.bytes[2] << 16) | (protocol.command.bytes[3] << 8) | protocol.command.bytes[4];
					dstt_sram[protocol.address+(0x80-write_count)] = val;
					write_count--;
				}
				break;
			}
			case 0x82:
			{
				//INFO("%08X && %08X\n",write_count,write_enabled);
				if(write_count && write_enabled)
				{
					//INFO("CMD%02X: %08X = %08X\n",protocol.command.bytes[0],ftell(img),val);
					fwrite(&val, 1, 4, img);
					fflush(img);
					write_count--;
				}
				break;
			}
			default:
				break;
		}

		if(write_count==0)
		{
			write_enabled = 0;

			// transfer is done
			//T1WriteLong(MMU.MMU_MEM[PROCNUM][0x40], 0x1A4,val & 0x7F7FFFFF);

			// if needed, throw irq for the end of transfer
			//if(MMU.AUX_SPI_CNT & 0x4000)
			//	NDS_makeIrq(PROCNUM, IRQ_BIT_GC_TRANSFER_COMPLETE);
		}

		if(log)
		{
			INFO("WRITE CARD command: %02X%02X%02X%02X%02X%02X%02X%02X\t", 
							protocol.command.bytes[0], protocol.command.bytes[1], protocol.command.bytes[2], protocol.command.bytes[3],
							protocol.command.bytes[4], protocol.command.bytes[5], protocol.command.bytes[6], protocol.command.bytes[7]);
			INFO("FROM: %08X\t", NDS_ARM9.instruct_adr);
			INFO("VAL: %08X\n", val);
		}
	}

	virtual u32 slot1client_read_GCDATAIN(eSlot1Operation operation)
	{
		bool log=true;
		
		u32 val=0;

		switch(protocol.command.bytes[0])
		{
			case 0x00: // read flash rom
			case 0xB7: // read flash rom

				protocol.address = (protocol.command.bytes[1] << 24) | (protocol.command.bytes[2] << 16) | (protocol.command.bytes[3] << 8) | protocol.command.bytes[4];
				val = dstt_flash[protocol.address+(0x80-read_count)];
				read_count--;
				log=true;
				break;
			//Get ROM chip ID
			case 0x90:
			case 0xB8:
				val = 0xFC2;
				log=false;
				break;

			case 0x50: // busy wait?
			case 0x51: // set mode?
			case 0x52: // end?
				{
					/*if(protocol.command.bytes[5]==0x29) {
						val = rand() & 0x80;
					} else {
						val = 0;
					}*/
					val = rand() & 0x1;
					log=false;
				}
				break;
			case 0x70: // read sram
				protocol.address = (protocol.command.bytes[1] << 24) | (protocol.command.bytes[2] << 16) | (protocol.command.bytes[3] << 8) | protocol.command.bytes[4];
				val = dstt_sram[protocol.address+(0x80-read_count)];
				read_count--;
				log=false;
				break;

			case 0x53: // read seek
			case 0x54: // multi read seek
			case 0x56: // write wait?
			case 0x80: // read wait?
			case 0x86: // flash write enable?
			case 0x87: // flash write 1 byte
				val = 0;
				log=false;
				break;

			case 0x81: // read
				{
					if(img)
					{
						INFO("Read from sd at adr %08X ",ftell(img));
						fread(&val, 1, 4, img);
						INFO("val %08X\n",val);
					}
					log=false;
				}
				break;
			case 0x82: // write
				break;

			case 0x8F: // random
			{
				if(protocol.command.bytes[1]==0x01) {
					val = 0x5A720000;
				} else if(protocol.command.bytes[1]==0x02) {
					val = 0xFFFFFFFF;
				} else if (protocol.command.bytes[1]==0x04) {
					val = rand() & 0x80;
				} else {
					val = 0;
				}
				break;
			}

			default:
				val = 0;
				break;
		}

		if(log)
		{
			INFO("READ CARD command: %02X%02X%02X%02X% 02X%02X%02X%02X RET: %08X  ", 
								protocol.command.bytes[0], protocol.command.bytes[1], protocol.command.bytes[2], protocol.command.bytes[3],
								protocol.command.bytes[4], protocol.command.bytes[5], protocol.command.bytes[6], protocol.command.bytes[7],
								val);
			INFO("FROM: %08X  LR: %08X\n", NDS_ARM9.instruct_adr, NDS_ARM9.R[14]);
			//INFO("FROM: %08X  LR: %08X\n", NDS_ARM9.instruct_adr, _MMU_read32<0>(NDS_ARM9.R[13]+0x14));
		}

		return val;
	} //read32_GCDATAIN

};
ISlot1Interface* construct_Slot1_dstt() { return new Slot1_dstt(); }
