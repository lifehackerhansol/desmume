/*
	Copyright (C) 2017 DeSmuME team

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

//do not compile this file unless IFDEF HAVE_POWERSAVES

//tested on model AS233

#include <hidapi/hidapi.h>

#include "../slot1.h"
#include "../NDSSystem.h"
#include "slot1comp_protocol.h"

#include "../encrypt.h"
#include "../utils/decrypt/decrypt.h"

static _KEY1 key1((const u8*)arm7_key);
extern _KEY2 key2; //defined in MMU.cpp

//utilities cribbed from powerslaves library
//https://github.com/kitling/powerslaves
namespace powerslaves
{
	enum CommandType
	{
		TEST = 0x02,
		SWITCH_MODE = 0x10,
		NTR_MODE = 0x11,
		NTR = 0x13,
		CTR = 0x14,
		SPI = 0x15
	};

	// Takes a buffer and reads len bytes into it.
	void readData(uint8_t *buf, unsigned len);

	// Takes a command type, command buffer with length, and the length of the expected response.
	bool sendMessage(enum CommandType type, const uint8_t *cmdbuf, uint8_t len, uint16_t response_len);

	//for reference
	//void simpleNTRcmd(uint8_t command, uint8_t *buf, unsigned len); // Convience function that takes a single byte for the command buffer and reads data into the buffer.
	//#define sendGenericMessage(type) sendMessage(type, NULL, 0x00, 0x00)
	//#define sendGenericMessageLen(type, response_length) sendMessage(type, NULL, 0x00, response_length)
	//#define sendNTRMessage(cmdbuf, response_length) sendMessage(NTR, cmdbuf, 0x08, response_length)
	//#define sendSPIMessage(cmdbuf, len, response_length) sendMessage(SPI, cmdbuf, len, response_length)
	//#define readHeader(buf, len) simpleNTRcmd(0x00, buf, len)
	//#define readChipID(buf) simpleNTRcmd(0x90, buf, 0x4)
	//#define dummyData(buf, len) simpleNTRcmd(0x9F, buf, len)

	static hid_device* getPowersaves() {
		static hid_device *device = NULL;
		if (!device) {
printf("hid_open\n");
			device = hid_open(0x1C1A, 0x03D5, NULL);
printf("device = %X\n", device);
			if (device == NULL) {
printf("hid_enumerate\n");
				struct hid_device_info *enumeration = hid_enumerate(0, 0);
				if (!enumeration) {
					printf("No HID devices found! Try running as root/admin?");
					return NULL;
				}
				free(enumeration);
				printf("No PowerSaves device found!");
				return NULL;
			}
		}

		return device;
	}


	void readData(uint8_t *buf, unsigned len) {
		if (!buf) return;
		unsigned iii = 0;
		while (iii < len) {
			iii += hid_read(getPowersaves(), buf + iii, len - iii);
			// printf("Bytes read: 0x%x\n", iii);
		}
	}

	bool sendMessage(CommandType type, const uint8_t *cmdbuf, uint8_t cmdlen, uint16_t response_len)
	{
		//apparently this should be this exact size, is it because of USB or HID or the powersaves device protocol?
		static const int kMsgbufSize = 65;
		u8 msgbuf[kMsgbufSize];

		//dont overflow this buffer (not sure how the size was picked, but I'm not questioning it now)
		if(cmdlen + 6 > kMsgbufSize) return false;
		memcpy(msgbuf + 6, cmdbuf, cmdlen);

		//fill powersaves protocol header:
		msgbuf[0] = 0; //reserved
		msgbuf[1] = type; //powersaves command type
		msgbuf[2] = cmdlen; //length of cmdbuf
		msgbuf[3] = 0x00; //reserved
		msgbuf[4] = (uint8_t)(response_len & 0xFF); //length of response (LSB)
		msgbuf[5] = (uint8_t)((response_len >> 8) & 0xFF); //length of response (MSB)

		hid_write(getPowersaves(), msgbuf, kMsgbufSize);
		return true;
	}

	static void cartInit()
	{
		sendMessage(SWITCH_MODE, NULL, 0, 0);
		sendMessage(NTR_MODE, NULL, 0, 0);
	
		//is this really needed?
		u8 junkbuf[0x40];
		sendMessage(TEST, NULL, 0, 0x40);
		readData(junkbuf, 0x40);
	}

} //namespace powerslaves

class Slot1_PowerSaves : public ISlot1Interface
{
private:
	u8 buf[0x4000+0x2000]; //0x4000 is largest, and 0x2000 for highest potential keylength
	u32 bufwords, bufidx;

    //the major operational mode. the protocol shifts modes and interprets commands into operations differently depending on the mode
    eCardMode mode;

    //gameCode used by the protocol KEY1 crypto
    u32 gameCode;

    //KEY1 gap1 length  (0-1FFFh) (forced min 08F8h by BIOS) (leading gap)
    u32 key1_length;

    //KEY1 gap2 length  (0-3Fh)   (forced min 18h by BIOS) (200h-byte gap)
    u32 key1_gap;

    //KEY1 index
    u32 key1_index;

    //Data Block size   (0=None, 1..6=100h SHL (1..6) bytes, 7=4 bytes)
    u32 blocksize;

public:
	Slot1_PowerSaves()
	{
	}

	virtual Slot1Info const* info()
	{
		static Slot1InfoSimple info("PowerSaves", "PowerSaves Card Reader", 0x05);
		return &info;
	}

	//called once when the emulator starts up, or when the device springs into existence
	virtual bool init()
	{
        mode = eCardMode_RAW;
        gameCode = 0;
        bufwords = 0;
        bufidx = 0;
        key1_length = 0x8F8;
        key1_gap = 0x18;
		return true;
	}
	
	virtual void connect()
	{
		powerslaves::cartInit();
		//
		//u8 junkbuf[0x2000];
		//powerslaves::simpleNTRcmd(0x9F,powerslaves::junkbuf, 0x2000); //needed?
	}

	//called when the emulator disconnects the device
	virtual void disconnect()
	{
	}
	
	//called when the emulator shuts down, or when the device disappears from existence
	virtual void shutdown()
	{ 
	}

	virtual void write_command(u8 PROCNUM, GC_Command command)
	{
        u16 reply = 0;

        u32 romctrl = T1ReadLong(MMU.MMU_MEM[PROCNUM][0x40], 0x1A4);
        u32 blocksize_field = (romctrl >> 24) & 7;
        static const u32 blocksize_table[] = { 0,0x200,0x400,0x800,0x1000,0x2000,0x4000,4 };
        
        blocksize = blocksize_table[blocksize_field];
        key1_gap = (romctrl >> 16) & 0x3F;
        key1_length = (romctrl & 0x1FFF); //key1length high gcromctrl[21:16] ??

        reply = blocksize + key1_length;
        switch(mode)
        {
            case eCardMode_KEY1:
            {
                //printf("[GC] KEY1 Length: %d (%08X)\n", reply, reply);
                //reply += 0x910;
                //printf("[GC] KEY1 Length2: %d (%08X)\n", reply, reply);
                u32 temp = (key1_gap * (blocksize / 0x200));
                GCLOG("[GC] KEY1 Padding: %d (%08X)\n", temp, temp);
                reply += temp;
                break;
            }

            case eCardMode_NORMAL:
                reply = blocksize;
                GCLOG("[GC] Pre KEY2: "); command.print();
                for(int i=0; i<8; i++)
                {
                    command.bytes[i] = key2.apply(command.bytes[i]);
                }

                /*printf("[GC] Post KEY2: "); command.print();

                key2.applySeed(1);
                for(int i = 0; i<8; i++)
                {
                    command.bytes[i] = key2.apply(command.bytes[i]);
                }
                printf("[GC] Reversed KEY2: "); command.print();*/
                break;


        }

        GCLOG("[GC] Sent CMD: "); command.print();
        GCLOG("[GC] doing IO of %d\n", reply, command.bytes[0]);

        powerslaves::sendMessage(powerslaves::NTR, command.bytes, 8, reply);
        powerslaves::readData(buf, reply);

        bufidx = 0;
        bufwords = reply / 4;

        switch(mode)
        {
            case eCardMode_RAW:
                write_command_RAW(command);
                break;

            case eCardMode_KEY1:
                write_command_KEY1(command);
                break;

            case eCardMode_NORMAL:
                write_command_NORMAL(command);
                break;
        }
	}
	virtual void write_GCDATAIN(u8 PROCNUM, u32 val)
	{
        GCLOG("[GC] Write to CART %08X\n", val);
	}
	virtual u32 read_GCDATAIN(u8 PROCNUM)
	{
		if(bufidx==bufwords) return 0; //buffer exhausted. is there additional stuff to emulate? we could read more from the card, and what does it return?

        u32 adr = bufidx;
        if(mode == eCardMode_KEY1)
        {
            if((blocksize >= 0x200) && ((adr % 0x80) == 0))
            {
                key1_index += key1_gap / 4;
                GCLOG("[GC] KEY1 Index increase to %08X\n", key1_index);
            }
            adr += key1_length / 4; //skip dummy data
            adr += key1_index;
        }

        u32 val = T1ReadLong(buf, adr * 4);
        GCLOG("VAL[0x%04X / 0x%04X] = %08X\n", adr, bufidx, val);
        bufidx++;
		return val;
	}

    //transfers a byte to the slot-1 device via auxspi, and returns the incoming byte
    //cpu is provided for diagnostic purposes only.. the slot-1 device wouldn't know which CPU it is.
    virtual u8 auxspi_transaction(int PROCNUM, u8 value)
	{
        //GCLOG("[GC] SPI %02X\n", value);
        //return 0x00;
        return MMU_new.backupDevice.data_command(value, PROCNUM);
	}

    //called when the auxspi burst is ended (SPI chipselect in is going low)
    virtual void auxspi_reset(int PROCNUM)
	{
        //GCLOG("[GC] SPI reset\n");
        MMU_new.backupDevice.reset_command();
	}

	virtual void post_fakeboot(int PROCNUM)
	{
		//supporting this will be tricky!
		//we may need to send a complete reboot/handshake sequence to the powersaves device
	}

    void write_command_RAW(GC_Command command)
    {
        int cmd = command.bytes[0];
        if(cmd == 0x3C)
        {
            //switch to KEY1
            mode = eCardMode_KEY1;

            //defer initialization of KEY1 until we know we need it, just to save some CPU time.
            //TODO - some information about these parameters
            //level == 2
            //modulo == 8
            key1.init(gameCode, 2, 0x08);
            GCLOG("[GC] KEY1 ACTIVATED\n");
        }
        if(cmd == 0x00)
        {
            gameCode = T1ReadLong(buf, 0x0C);
            GCLOG("[GC] gameCode = %08X\n", gameCode);
        }
    }

    void write_command_KEY1(GC_Command command)
    {
        //move bufidx forward past the dummy data
        //bufidx += 0x910 / 4;

        key1_index = 0;

        //decrypt the KEY1-format command
        u32 temp[2];
        command.toCryptoBuffer(temp);
        key1.decrypt(temp);
        command.fromCryptoBuffer(temp);
        GCLOG("[GC] (key1-decrypted):"); command.print();

        //decrypt response
        for(int i = 0; i<bufwords * 4; i++)
        {
            buf[i] = key2.apply(buf[i]);
        }

        //and process it:
        int cmd = command.bytes[0];
        switch(cmd & 0xF0)
        {
            case 0x40:
                //switch to KEY2
                //well.. not really... yet.
                GCLOG("[GC] KEY2 ACTIVATED\n");
                break;

            case 0x60:
                //KEY2 disable? any info?
                break;

            case 0xA0:
                mode = eCardMode_NORMAL;
                GCLOG("[GC] NORMAL MODE ACTIVATED\n");
                break;

        }
    }

    void write_command_NORMAL(GC_Command command)
    {
        //decode response?
        for(int i = 0; i<bufwords*4; i++)
        {
            buf[i] = key2.apply(buf[i]);
        }

        switch(command.bytes[0])
        {
            default:
                //operation = eSlot1Operation_Unknown;
                //client->slot1client_startOperation(operation);
                break;
        }
    }

};

ISlot1Interface* construct_Slot1_PowerSaves() { return new Slot1_PowerSaves(); }
