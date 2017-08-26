/*
	Copyright (C) 2010-2015 DeSmuME team

	This file is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	This file is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with the this software.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "slot1comp_protocol.h"

#include <time.h>

#include "armcpu.h"
#include "MMU.h"
#include "../slot1.h"
#include "../NDSSystem.h"
#include "../emufile.h"

extern armcpu_t NDS_ARM7;
extern armcpu_t NDS_ARM9;

//#define virtual_fat

class Slot1_AK2i : public ISlot1Interface, public ISlot1Comp_Protocol_Client
{
private:
	Slot1Comp_Protocol protocol;
	//Acekard 2i stuff
	int ak_idx = 0;
	unsigned int ak_data[512 / 4];
	unsigned int mapTable[0x0F];
	unsigned char ak2_flash[0x80000];
	unsigned int LBASectorMap[4];
	unsigned int startLogicSector = 0xFFFFFFFF;
	unsigned int sd_stat = 0x00;
	unsigned int sdClusterShift = 0x00;
	unsigned int norOffset = 0;
	int push_len = 0;
	int ak_flash_idx = 0;
	int ak_map = 0;
	unsigned int write_addr;
#ifdef virtual_fat
	EMUFILE *img;
#else
	FILE *img;
#endif
	FILE *cDump;

#ifdef EMULATE_SLOW_SD
	unsigned int sdDelay = 0x1000;
	unsigned int slowDown = 0;
	unsigned int new_sd_stat = 0x00;
#endif

public:
	Slot1_AK2i()
		: img(NULL)
		, sd_stat(0)
		, sdClusterShift(0)
		, norOffset(0)
		, startLogicSector(0xFFFFFFFF)
		, push_len(0)
		, ak_flash_idx(0)
		, ak_map(0)
		, write_addr(0)
		, cDump(NULL)
	{
	}

	virtual Slot1Info const* info()
	{
		static Slot1InfoSimple info("AK2i", "Slot1 Acekard 2i emulation", NDS_SLOT1_AK2I);
		return &info;
	}
    
    void ak_data_set(unsigned char *buf, int len)
    {
        ak_idx = 0;
        if (len <= 0)
        {
            ZeroMemory(ak_data, sizeof(ak_data));
            return;
        }
        ZeroMemory(ak_data, 7);
        unsigned char *ak_data8 = (unsigned char*)ak_data;
        for (int i = 7, j = 0, b = 0; i < 512; i ++, b >>= 1)
        {
            if (b == 0)
            {
                b = 0x80;
                if (j <= 0) j = len;
                j --;
            }
            ak_data8[i] |= (buf[j] & b) ? 0x80 : 0x00;
        }
    }

	//called once when the emulator starts up, or when the device springs into existence
	virtual bool init()
	{
		//strange to do this here but we need to make sure its done at some point
		srand(time(NULL));
		return true;
	}
	
	virtual void connect()
	{
        _MMU_write32<ARMCPU_ARM9>(0x27ff800,0xFC2);
        _MMU_write32<ARMCPU_ARM9>(0x27ff804,0xFC2);
        _MMU_write32<ARMCPU_ARM9>(0x27FFC00,0xFC2);
        _MMU_write32<ARMCPU_ARM9>(0x27FFC04,0xFC2);

        _MMU_write32<ARMCPU_ARM7>(0x27ff800,0xFC2);
        _MMU_write32<ARMCPU_ARM7>(0x27ff804,0xFC2);
        _MMU_write32<ARMCPU_ARM7>(0x27FFC00,0xFC2);
        _MMU_write32<ARMCPU_ARM7>(0x27FFC04,0xFC2);
    
		if (!img)
#ifdef virtual_fat
			img = slot1_GetFatImage();
#else
			img = fopen("DLDI_AK2S.img", "r+b");
#endif

		FILE *f = fopen("AK2.bin","rb");
        if(f)
        {
            fread(&ak2_flash,1,0x40000,f);
            fclose(f);
        }
        else
        {
            INFO("AK2.bin not found\n");
            T1WriteLong(ak2_flash,0x200C,0x4B454341); //ACEK
        }
        
        if(!cDump)
            cDump = fopen("cDump.bin", "w+b"); //cDump = fopen("cDump.bin", "r+b");

		if(!img)
			INFO("slot1 fat not successfully mounted\n");

		protocol.reset(this);
		protocol.chipId = 0xFC2;
		protocol.gameCode = T1ReadLong((u8*)gameInfo.header.gameCode,0);
	}
    
    unsigned int lookupSector(unsigned int map, unsigned int src)
    {
        unsigned int r0,r1,r2,r3,r4;
        r1 = startLogicSector;
        r3 = src;
        r4 = sdClusterShift;

        r2 = r1 + 3;
        src >>= r4;

        if(src < r1 || src >= r2)
        {
            //INFO("Stop SD2!\n");
            //sd_stat = 0x00;
            //write_addr = 0;
            //ak_map = 0;

            startLogicSector = src;
            src <<= 2;
            src += mapTable[map];

            LBASectorMap[0] = T1ReadLong(ak2_flash,src);
            LBASectorMap[1] = T1ReadLong(ak2_flash,src+4);
            LBASectorMap[2] = T1ReadLong(ak2_flash,src+8);
            LBASectorMap[3] = T1ReadLong(ak2_flash,src+12);
        }

        r1 = startLogicSector;
        src = r3 >> r4;

        r0 = 32 - r4;
        src -= r1;

        src = LBASectorMap[src];

        r2 = r3 << r0;
        r2 >>= r0;

        /*if(sdIsSDHC)
        {
            r2 >>= 9;
        }*/

        src += r2;

        return src;
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

	virtual u32 write32_preGCROMCTRL(u8 PROCNUM, u32 val)
	{
		if (push_len > 0)
		{
			push_len--;
			if (img)
			{
				GC_Command rawcmd = *(GC_Command*)&MMU.MMU_MEM[PROCNUM][0x40][0x1A8];
				u32 org_addr = (rawcmd.bytes[0] | (rawcmd.bytes[1] << 8) | (rawcmd.bytes[2] << 16) | (rawcmd.bytes[3] << 24));
				u32 org_data = (rawcmd.bytes[4] | (rawcmd.bytes[5] << 8) | (rawcmd.bytes[6] << 16) | (rawcmd.bytes[7] << 24));
				//INFO("write %08X at %08X\n",org_addr,ftell(img));
				//INFO("write %08X at %08X\n",org_data,ftell(img)+4);
#ifdef virtual_fat
				img->fwrite(&org_addr, 4);
				img->fwrite(&org_data, 4);
				img->fflush();
#else
				fwrite(&org_addr, 1, 4, img);
				fwrite(&org_data, 1, 4, img);
				fflush(img);
#endif
			}
			val &= 0x7FFFFFFF;
			//val |= 0x00800000; //Set ready flag
			T1WriteLong(MMU.MMU_MEM[PROCNUM][0x40], 0x1A4, val);
			return 0x01020304; //Hack
		}
		return -1;
	}

	virtual void slot1client_startOperation(eSlot1Operation operation)
	{
		//if(operation != eSlot1Operation_Unknown)
		//	return;

		u32 address;
        bool log = true;
		int cmd = protocol.command.bytes[0];
        
        switch(cmd)
        {
            case 0x00:
            case 0xB7:
            case 0xB8:
            case 0xC0:
            case 0xC2:
            case 0xD0:
            case 0xD1:
            case 0xD4:
            case 0x97: case 0xAB: case 0xD5:
            case 0xD7:
            case 0xD8:
            case 0xA5: case 0xDA: //R4i Gold 3DS
                log=false;
                break;
        }

        if(log)
        //if(protocol.command.bytes[3] != 0x29 && protocol.command.bytes[3] != 0x37 && protocol.command.bytes[0] != 0xB7 && protocol.command.bytes[0] != 0xB8)
        {
            INFO("WRITE CARD command: %02X%02X%02X%02X% 02X%02X%02X%02X  ",
                                protocol.command.bytes[0], protocol.command.bytes[1], protocol.command.bytes[2], protocol.command.bytes[3],
                                protocol.command.bytes[4], protocol.command.bytes[5], protocol.command.bytes[6], protocol.command.bytes[7]);
            INFO("FROM: %08X  LR: %08X\n", NDS_ARM9.instruct_adr, NDS_ARM9.R[14]);
        }
        
		switch(cmd)
		{
			//AK2i stuff
            case 0xC0:
            case 0xC2:
                break;

            //M3-Plus Clone
            case 0xDE:
                break;

            //R4i Gold 3DS
            case 0xA5:
                fseek(cDump, (protocol.command.bytes[1]<<16) |(protocol.command.bytes[2]<<8) | (protocol.command.bytes[3]), SEEK_SET);
                break;

            case 0xD0:
                {
                    mapTable[(protocol.command.bytes[5])&0x0F] = protocol.command.bytes[4] | (protocol.command.bytes[3] << 8) | (protocol.command.bytes[2] << 16) | (protocol.command.bytes[1] << 24);
                    INFO("mapTable[%d] = %08X\n",(protocol.command.bytes[5])&0x0F,mapTable[(protocol.command.bytes[5])&0x0F]);
                    protocol.address = 0;
                }
                break;
            case 0xD1: //AK2i hardware version
                break;
            case 0xD4: //Flash erase/write
                switch (protocol.command.bytes[7])
                {
                    case 0x35: //Erase
                        //INFO("erase[%08X] = %02X\n",(protocol.command.bytes[1]<<16) |(protocol.command.bytes[2]<<8) | (protocol.command.bytes[3]),0x30);
                        T1WriteByte(ak2_flash,(protocol.command.bytes[1]<<16) |(protocol.command.bytes[2]<<8) | (protocol.command.bytes[3]),0x30);
                        break;
                    case 0x63: //Write
                        //INFO("write[%08X] = %02X\n",(protocol.command.bytes[1]<<16) |(protocol.command.bytes[2]<<8) | (protocol.command.bytes[3]),protocol.command.bytes[4]);
                        T1WriteByte(ak2_flash,(protocol.command.bytes[1]<<16) |(protocol.command.bytes[2]<<8) | (protocol.command.bytes[3]),protocol.command.bytes[4]);
                        //fseek(cDump, (protocol.command.bytes[1]<<16) |(protocol.command.bytes[2]<<8) | (protocol.command.bytes[3]), SEEK_SET);
                        //fwrite(&protocol.command.bytes[4], 1, 1, cDump);
                        //fflush(cDump);
                        break;
                    case 0xE3: //Write to commercial rom
                        //INFO("write[%08X] = %02X\n",(protocol.command.bytes[1]<<16) |(protocol.command.bytes[2]<<8) | (protocol.command.bytes[3]),protocol.command.bytes[4]);
                        fseek(cDump, (protocol.command.bytes[1]<<16) |(protocol.command.bytes[2]<<8) | (protocol.command.bytes[3]), SEEK_SET);
                        fwrite(&protocol.command.bytes[4], 1, 1, cDump);
                        fflush(cDump);
                        break;
                }
                sd_stat = 0x00;// required for r4i gold
                break;

            case 0x97: //M3-Plus
            case 0xAB:
            case 0xD5:

    #ifdef EMULATE_SLOW_SD
                slowDown=0;
    #endif
                switch (protocol.command.bytes[3])
                {
                    case 0x0C:
                        sd_stat = 0x00;
                        write_addr = 0;
                        ak_map = 0;
                        //INFO("Stop SD!\n");
                        break;

                    case 0x0D:
                        ak_data_set((unsigned char*)"\x00\x08\x00\x00", 4);
                        break;

                    case 0x11:
                    case 0x12:
                    {
                        u32 adr = (protocol.command.bytes[4]<<24) | (protocol.command.bytes[5]<<16) | (protocol.command.bytes[6]<<8) | protocol.command.bytes[7];
                        ak_map = (protocol.command.bytes[1]>>4) & 0x0F;
                        sd_stat = 0x77; //Required for ingame code else it infinite loops :D
						adr <<= 9; //r4igold.cc
    #ifdef EMULATE_SLOW_SD
                        slowDown=sdDelay;
    #endif
                        INFO("Read from Map %d at adr %08X\n",ak_map,adr);
                        switch (ak_map)
                        {
                            case 0: break;
                            /*case 1:
                                adr = lookupSector(ak_map,adr);
                                break;*/

                            default:
                                //INFO("Read from Map %d at adr %08X",ak_map,adr);
                                adr = lookupSector(ak_map,adr);
                                //INFO(" real adr %08X\n",adr);
                                break;
                        }

                        if (img)
                        {
                            #ifdef virtual_fat
                                img->fseek(adr, SEEK_SET);
                            #else
                                fseek(img, adr, SEEK_SET);
                            #endif
                        }
                    }
                    break;

                    case 0x18:
                    {
                        u32 adr = (protocol.command.bytes[4]<<24) | (protocol.command.bytes[5]<<16) | (protocol.command.bytes[6]<<8) | protocol.command.bytes[7];
                        ak_map = (protocol.command.bytes[1]>>4) & 0x0F;
                        sd_stat = 0x00;
                        push_len = 512/8;
						adr <<= 9; //r4igold.cc
                        INFO("Write from Map %d at adr %08X",ak_map,adr);
                        switch (ak_map)
                        {
                            case 0: break;
                            case 1:
                                adr = lookupSector(ak_map,adr);
                                break;

                            default:
                                //INFO("Write from Map %d at adr %08X",ak_map,adr);
                                adr = lookupSector(ak_map,adr);
                                //INFO(" real adr %08X\n",adr);
                                break;
                        }
                        //INFO(" real adr %08X\n",adr);

                        if (img)
                        {
                            #ifdef virtual_fat
                                img->fseek(adr, SEEK_SET);
                            #else
                                fseek(img, adr, SEEK_SET);
                            #endif
                        }
                    }
                    break;

                    case 0x19:
                        sd_stat = 0xEE;
                        push_len = 512/8;
                        if(write_addr)
                        {
                            write_addr += 512;
                        }
                        else
                        {
                            write_addr = (protocol.command.bytes[4]<<24) | (protocol.command.bytes[5]<<16) | (protocol.command.bytes[6]<<8) | protocol.command.bytes[7];
                        }

                        if (img)
                        {
                            #ifdef virtual_fat
                                img->fseek(write_addr, SEEK_SET);
                            #else
                                fseek(img, write_addr, SEEK_SET);
                            #endif
                        }
                        ak_map = (protocol.command.bytes[1]>>4) & 0x0F;
                        break;

                    case 0x29:
                        ak_data_set((unsigned char*)"\x00\x00\x00\x80", 4);
                        break;
                    default:
                        ak_data_set(NULL, 0);
                        break;
                }
                //INFO("SD CMD: %08X\n", protocol.command.bytes[3]);
                break;

            case 0xD7:
                sdClusterShift = 9 + protocol.command.bytes[6];
                INFO("ClusterShift = %d\n",sdClusterShift);
                //INFO("WRITE CARD command: %02X%02X%02X%02X% 02X%02X%02X%02X RET: %08X  ",
                //			protocol.command.bytes[0], protocol.command.bytes[1], protocol.command.bytes[2], protocol.command.bytes[3],
                //			protocol.command.bytes[4], protocol.command.bytes[5], protocol.command.bytes[6], protocol.command.bytes[7],
                //			val);
                //INFO("FROM: %08X  LR: %08X\n", (PROCNUM?NDS_ARM9:NDS_ARM7).instruct_adr, NDS_ARM9.R[14]);
                break;

            // Data read
            case 0xD8:
                break;

            //R4i Gold 3DS
            case 0xDA: //Write to commercial rom
                //INFO("write[%08X] = %02X\n",(protocol.command.bytes[1]<<16) |(protocol.command.bytes[2]<<8) | (protocol.command.bytes[3]),protocol.command.bytes[4]);
                fseek(cDump, (protocol.command.bytes[1]<<16) |(protocol.command.bytes[2]<<8) | (protocol.command.bytes[3]), SEEK_SET);
                fwrite(&protocol.command.bytes[4], 1, 1, cDump);
                fflush(cDump);
                break;

            case 0x00:
            case 0xB7:
                {
                    u32 adr = 0;
                    sd_stat = 0x77;
                    ak_flash_idx = 0;
                    //INFO("count = %08X\n",card.transfer_count);
					protocol.address = 	(protocol.command.bytes[1] << 24) | (protocol.command.bytes[2] << 16) | (protocol.command.bytes[3] << 8) | protocol.command.bytes[4];
                    // Make sure any reads below 0x8000 redirect to 0x8000+(adr&0x1FF) as on real cart
                    /*if((protocol.command.bytes[5] == 0x00) && (card.address < 0x8000))
                    {
                        INFO("Read below 0x8000 (0x%04X) from: ARM%s 0x%08X\n",card.address, "9", NDS_ARM9.instruct_adr);
                        card.address = (0x8000 + (card.address&0x1FF));
                        //adr = lookupSector(1,card.address);
                        //INFO("0x%08X realAdr 0x%08X\n", card.address, adr);
                        //fseek(img, adr, SEEK_SET);
                    }*/
                    break;
                }
            default:
				protocol.address = 0;
                break;
		}
	}

	virtual u32 slot1client_read_GCDATAIN(eSlot1Operation operation)
	{
		//if(operation != eSlot1Operation_Unknown)
		//	return 0;

		u32 val;
		int cmd = protocol.command.bytes[0];
        
        bool log = true;

        switch(cmd)
        {
            case 0x00:
            case 0xB7:
            case 0xB8:
            case 0xC0:
            case 0xC2:
            case 0xD0:
            case 0xD1:
            case 0xD4:
            case 0x97: case 0xAB: case 0xD5:
            case 0xD7:
            case 0xD8:
            case 0xA5: case 0xDA: //R4i Gold 3DS
                log=false;
                break;
        }
        
		switch(cmd)
		{
			//AK2i stuff
            case 0xC0: //SD status
    #ifdef EMULATE_SLOW_SD
                if(!slowDown) new_sd_stat = sd_stat<<4;
                val = new_sd_stat;
                if(slowDown) slowDown--;
    #else
                val = sd_stat<<4;
    #endif
                break;

            case 0xD1: //Hardware version
                val = 0x81818181; //AK2i hardware version
                //val = 0xA6A6A6A6; //R4i Gold 3DS Clone
                break;

            case 0x97: //M3-Plus
            case 0xAB:
            case 0xD5:
                val = ak_data[ak_idx++&511];
                break;

            // Data read
            case 0xC2:
                val = 0;
                break;

			//r4igold.cc
			case 0xC5:
				val = 0xFFFFFFFF;
				break;

            //R4i Gold Clone
            case 0xC7:
                //val = 0xA79BCA95;//0x9BCA95A7;//0x95A79BCA;
                val = 0x9BCA95A7;
                break;

            //R4i Gold 3DS
            case 0xA5:
                fread(&val, 1, 4, cDump);
                break;

            //M3-Plus Clone
            case 0xDE:
                {
                    unsigned int address = (protocol.command.bytes[3] | (protocol.command.bytes[2] << 8) | (protocol.command.bytes[1] << 16));
                    if(address == 0x81BBA5) val = 0xEFEFEFEF;
                    else if(address == 0x8189FF) val = 0x02020202;
                    else val = 0;
                    break;
                }

            case 0x00:
            case 0xB7:
                {
                    switch (protocol.command.bytes[5])
                    {
                        case 0x00:
                        {
                            unsigned int address = mapTable[0] + ((protocol.command.bytes[4]) | (protocol.command.bytes[3] << 8) | (protocol.command.bytes[2] << 16) | (protocol.command.bytes[1] << 24)) + ak_flash_idx;
                            val = T1ReadLong(ak2_flash,address);
                            //INFO("read0[%08X] = %08X\n",address,val);
                            if(ak_flash_idx<512) ak_flash_idx+=4; else ak_flash_idx=0;
                            break;
                        }
                        case 0x10:
                        case 0x12:
                        {
                            unsigned int address = mapTable[0] + ((protocol.command.bytes[4]) | (protocol.command.bytes[3] << 8) | (protocol.command.bytes[2] << 16) | (protocol.command.bytes[1] << 24)) + ak_flash_idx;
                            val = T1ReadLong(ak2_flash,address);
                            //INFO("read[%08X] = %08X\n",address,val);
                            if(ak_flash_idx<512) ak_flash_idx+=4; else ak_flash_idx=0;
                            break;
                        }
                        case 0x11:
                        case 0x13:
                            #ifdef virtual_fat
                                img->fread(&val, 4);
                            #else
                                fread(&val, 1, 4, img);
                            #endif
                            break;
						//case 0x15: //r4igold.cc
						//	val = 0x2A2A2A2A;
						//	break;
                        default:
                            INFO("B7 read with param 0x%02X\n",protocol.command.bytes[5]);
                            break;
                    }
                    //return val;
                }
                break;

            //Get ROM chip ID
            case 0x90:
            case 0xB8:
                {
                    // Note: the BIOS stores the chip ID in main memory
                    // Most games continuously compare the chip ID with
                    // the value in memory, probably to know if the card
                    // was removed.
                    // As DeSmuME boots directly from the game, the chip
                    // ID in main mem is zero and this value needs to be
                    // zero too.

                    //staff of kings verifies this (it also uses the arm7 IRQ 20)
                    if(nds.cardEjected) //TODO - handle this with ejected card slot1 device (and verify using this case)
                        val = 0xFFFFFFFF;
                    else val = 0xFC2;
                }
                break;

            default:
                val = 0;
                break;
		}
        
        if(log)
        //if(protocol.command.bytes[3] != 0x29 && protocol.command.bytes[3] != 0x37 && protocol.command.bytes[0] != 0xB7 && protocol.command.bytes[0] != 0xB8)
        {
            INFO("READ  CARD command: %02X%02X%02X%02X% 02X%02X%02X%02X RET: %08X  ",
                                protocol.command.bytes[0], protocol.command.bytes[1], protocol.command.bytes[2], protocol.command.bytes[3],
                                protocol.command.bytes[4], protocol.command.bytes[5], protocol.command.bytes[6], protocol.command.bytes[7],
                                val);
            INFO("FROM: %08X  LR: %08X\n", NDS_ARM9.instruct_adr, NDS_ARM9.R[14]);
        }

		return val;
	}

	void slot1client_write_GCDATAIN(eSlot1Operation operation, u32 val)
	{
		if(operation != eSlot1Operation_Unknown)
			return;

		int cmd = protocol.command.bytes[0];
		switch(cmd)
		{
			/*case 0xBB:
			{
				if(write_count && write_enabled)
				{
					img->fwrite(&val, 4);
					img->fflush();
					write_count--;
				}
				break;
			}*/
			default:
				break;
		}
	}

	virtual void post_fakeboot(int PROCNUM)
	{
		// The BIOS leaves the card in NORMAL mode
		protocol.mode = eCardMode_NORMAL;
	}

	void write32_GCDATAIN(u32 val)
	{
		bool log = false;
		if(log)
		{
			INFO("WRITE GCDATAIN CARD command: %02X%02X%02X%02X%02X%02X%02X%02X\t", 
							protocol.command.bytes[0], protocol.command.bytes[1], protocol.command.bytes[2], protocol.command.bytes[3],
							protocol.command.bytes[4], protocol.command.bytes[5], protocol.command.bytes[6], protocol.command.bytes[7]);
			INFO("FROM: %08X\t", NDS_ARM9.instruct_adr);
			INFO("VAL: %08X\n", val);
		}
	}

};

ISlot1Interface* construct_Slot1_AK2i() { return new Slot1_AK2i(); }
