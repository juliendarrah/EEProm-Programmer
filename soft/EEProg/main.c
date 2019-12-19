/*
 * Assuming libftd2xx.so is in /usr/local/lib, build with:
 * 
 *     gcc -o bitmode main.c -L. -lftd2xx -Wl,-rpath /usr/local/lib
 * 
 * and run with:
 * 
 *     sudo ./spimode [port number]
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../FTDI/ftd2xx.h"

#define EEPROM_WRITE_DELAY_USEC			15000
#define EEPROM_WRITE_PAGE_SIZE			64
#define MAX_DATA_BUFFER_SIZE			4096

FT_STATUS FTDI_InitSerial(FT_HANDLE *handle, int portNumber);
FT_STATUS EEPROM_WriteByte(FT_HANDLE handle, int memAddress, unsigned char memData);
FT_STATUS EEPROM_ReadByte(FT_HANDLE handle, int memAddress, unsigned char *memData);
FT_STATUS EEPROM_SetAddress(FT_HANDLE handle, int memAddress);

void usage(char *name);
void showHelp(char *name);

int main(int argc, char *argv[])
{
	FILE 			*file;
	FT_STATUS		ftStatus = FT_OK;
	FT_HANDLE		ftHandle;
	int 			opt;
	int 			verbose = 0;
	int 			loadAddress = 0x0000;
	int         	portNumber = 0;
	int				addressIndex;
	unsigned int 	b;
	unsigned char	OutputBuffer[MAX_DATA_BUFFER_SIZE];


	while ((opt = getopt(argc, argv, "hvl:p:")) != -1)
	{
        switch (opt) 
		{
			case 'v':
				verbose = 1;
				break;
			case 'l':
				loadAddress = strtol(optarg, 0, 0);
				break;
			case 'p':
				portNumber = strtol(optarg, 0, 0);
				break;
			case 'h':
				showHelp(argv[0]);
				exit(EXIT_SUCCESS);
			default:
				usage(argv[0]);
				exit(EXIT_FAILURE);
        }
    }

	if (argc != optind + 1)
	{
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

	/* Open Binary file to program EEPROM */
	file = fopen(argv[optind], "rb");
    if (file == NULL) 
	{
        fprintf(stderr, "%s: Unable to open '%s'\n", argv[0], argv[optind]);
        return 1;
    }

	/* Init FTDI USB Device */
	ftStatus = FTDI_InitSerial(&ftHandle, portNumber);
	if (ftStatus != FT_OK) 
	{
		return 1;
	}
	
	addressIndex = 0;

	printf("Start Flash at address 0x%04X\n", loadAddress);
	while( fread(&b, 1, 1, file) == 1 )
	{
		EEPROM_WriteByte(ftHandle, loadAddress+addressIndex, b);
		usleep(EEPROM_WRITE_DELAY_USEC);
		/* Check to see if it's time to write, bounded to 64bytes */
		/* if addressIndex = 0, it our first time we do not want to wait for programming delay */
		if( (((loadAddress+addressIndex) % EEPROM_WRITE_PAGE_SIZE) == (EEPROM_WRITE_PAGE_SIZE-1)) && (addressIndex != 0))
		{
				//usleep(EEPROM_WRITE_DELAY_USEC);
				printf(".");
				/* Force printf to display */
				fflush(stdout);
		}

#if(0)
		if( (((loadAddress+addressIndex) % 16) == 0) || (addressIndex == 0) )
		{	
			printf("\n");
			printf("%04X: ", loadAddress+addressIndex);
		}
		printf("%02X ", (unsigned char)b);
#endif

		addressIndex++;
	}
	usleep(EEPROM_WRITE_DELAY_USEC);
	printf("Done!\n");
	printf("End Flash at address 0x%04X\n", loadAddress+addressIndex-1);
	printf("\n");

		



	/* Return chip to default (UART) mode. */
	(void)FT_SetBitMode(ftHandle, 
	                    0, /* ignored with FT_BITMODE_RESET */
	                    FT_BITMODE_RESET);


	/* Close FTDI USB */
	(void)FT_Close(ftHandle);
	/* Close File */
	fclose(file);

	return 0;
}

FT_STATUS FTDI_InitSerial(FT_HANDLE *handle, int portNumber)
{
	FT_STATUS	ftStatus = FT_OK;
	DWORD       bytesWritten = 0;
	unsigned char serialDataOut[20];	

	ftStatus = FT_Open(portNumber, handle);
	if (ftStatus != FT_OK) 
	{
		/* FT_Open can fail if the ftdi_sio module is already loaded. */
		printf("FT_Open(%d) failed (error %d).\n", portNumber, (int)ftStatus);
		printf("Use lsmod to check if ftdi_sio (and usbserial) are present.\n");
		printf("If so, unload them using rmmod, as they conflict with ftd2xx.\n");
		return 1;
	}

	/* Reset MPSSE
	 */
	printf("RESET MPSSE Mode.\n");	
	ftStatus = FT_SetBitMode(*handle, 
	                         0xFF, /* sets all 8 pins as outputs */
	                         FT_BITMODE_RESET);
	if (ftStatus != FT_OK) 
	{
		printf("FT_BITMODE_RESET failed (error %d).\n", (int)ftStatus);
		return ftStatus;
	}


	/* Enable MPSSE Mode
	 */
	printf("Selecting MPSSE Mode.\n");	
	ftStatus = FT_SetBitMode(*handle, 
	                         0xFF, /* sets all 8 pins as outputs */
	                         FT_BITMODE_MPSSE);
	if (ftStatus != FT_OK) 
	{
		printf("FT_BITMODE_MPSSE failed (error %d).\n", (int)ftStatus);
		return ftStatus;
	}

	/* Set Clock Rate
	 */
	serialDataOut[0] = 0x86; /* Set Clk Divisor Command */ 
	serialDataOut[1] = 0x04; /* Divisor LSB */
	serialDataOut[2] = 0x00; /* Divisor MSB */

	ftStatus = FT_Write(*handle, &serialDataOut, 3, &bytesWritten);
	if (ftStatus != FT_OK)
	{
		printf("FT_Write failed (error %d).\n", (int)ftStatus);
		return ftStatus;
	}

	/* Set GPIO High and Low Directions 
		AD0 -> MCLK
		AD1 -> MOSI
		AD2 -  NC
		AD3 -  NC
		AD4 -> WE_b
		AD5 -> OE_b
		AD6 -> RST_b Shift Registers
		AD7 -> ADDRESS_LATCH Shift Registers (Active High)

		AC0 to AC7 <-> D0 to D7

	 */
	serialDataOut[0] = 0x80; /* Command GPIO LOW (ADBUS)*/ 
	serialDataOut[1] = 0x7F; /* Default Value */
	serialDataOut[2] = 0xFB; /* Direction 0=Input, 1=Output */
	serialDataOut[3] = 0x82; /* Command GPIO HIGH (ACBUS)*/ 
	serialDataOut[4] = 0xFF; /* Default Value */
	serialDataOut[5] = 0xFF; /* Direction 0=Input, 1=Output */

	ftStatus = FT_Write(*handle, &serialDataOut, 6, &bytesWritten);
	if (ftStatus != FT_OK)
	{
		printf("FT_Write failed (error %d).\n", (int)ftStatus);
		return ftStatus;
	}

	/* Reset Shift Registers 
	 */
	serialDataOut[0] = 0x80; /* Command GPIO LOW (ADBUS)*/ 
	serialDataOut[1] = 0x3F; /* Default Value */
	serialDataOut[2] = 0xFB; /* Direction 0=Input, 1=Output */
	serialDataOut[3] = 0x80; /* Command GPIO LOW (ADBUS)*/ 
	serialDataOut[4] = 0x7F; /* Default Value */
	serialDataOut[5] = 0xFB; /* Direction 0=Input, 1=Output */

	ftStatus = FT_Write(*handle, &serialDataOut, 6, &bytesWritten);
	if (ftStatus != FT_OK)
	{
		printf("FT_Write failed (error %d).\n", (int)ftStatus);
		return ftStatus;
	}


return FT_OK;
}

FT_STATUS EEPROM_WriteByte(FT_HANDLE handle, int memAddress, unsigned char memData)
{
	FT_STATUS	ftStatus = FT_OK;
	DWORD       bytesWritten = 0;
	unsigned char serialDataOut[20];

	/* Set Memory Address */
	ftStatus = EEPROM_SetAddress(handle, memAddress);
	if (ftStatus != FT_OK)
	{
		printf("FT_Write failed (error %d).\n", (int)ftStatus);
		return ftStatus;
	}

	/* Disable Output Enable */
	serialDataOut[0] = 0x80; /* Command GPIO LOW (ADBUS)*/ 
	serialDataOut[1] = 0x7F; /* OE_b = 1, WE_b = 1 */
	serialDataOut[2] = 0xFB; /* Direction 0=Input, 1=Output */
	/* Write the Data on the Data Bus
	 */
	serialDataOut[3] = 0x82; /* Command GPIO HIGH (ACBUS)*/ 
	serialDataOut[4] = memData; /* Default Value */
	serialDataOut[5] = 0xFF; /* Direction 0=Input, 1=Output */
	/* Pulse the WE_b Line */
	serialDataOut[6] = 0x80; /* Command GPIO LOW (ADBUS)*/ 
	serialDataOut[7] = 0x6F; /* WE_b = 0 */
	serialDataOut[8] = 0xFB; /* Direction 0=Input, 1=Output */
	serialDataOut[9] = 0x80; /* Command GPIO LOW (ADBUS)*/ 
	serialDataOut[10] = 0x7F; /* WE_b = 1 */
	serialDataOut[11] = 0xFB; /* Direction 0=Input, 1=Output */

	ftStatus = FT_Write(handle, &serialDataOut, 12, &bytesWritten);
	if (ftStatus != FT_OK)
	{
		printf("FT_Write failed (error %d).\n", (int)ftStatus);
		return ftStatus;
	}

return FT_OK;
	
}

FT_STATUS EEPROM_ReadByte(FT_HANDLE handle, int memAddress, unsigned char *memData)
{
	FT_STATUS	ftStatus = FT_OK;
	DWORD       bytesWritten = 0;
	unsigned char serialDataOut[20];

	/* Set Memory Address */
	ftStatus = EEPROM_SetAddress(handle, memAddress);
	if (ftStatus != FT_OK)
	{
		printf("FT_Write failed (error %d).\n", (int)ftStatus);
		return ftStatus;
	}	

	/* Read the Data on the Data Bus - Set BUS Direction to Input
	 */

	serialDataOut[0] = 0x82; /* Command GPIO HIGH (ACBUS)*/ 
	serialDataOut[1] = 0xFF; /* Default Value */
	serialDataOut[2] = 0x00; /* Direction 0=Input, 1=Output */
	/* Set Momory Output Enable */
	serialDataOut[3] = 0x80; /* Command GPIO LOW (ADBUS)*/ 
	serialDataOut[4] = 0x7F; /* OE_b = 1 */
	serialDataOut[5] = 0xFB; /* Direction 0=Input, 1=Output */
	serialDataOut[6] = 0x80; /* Command GPIO LOW (ADBUS)*/ 
	serialDataOut[7] = 0x5F; /* OE_b = 0 */
	serialDataOut[8] = 0xFB; /* Direction 0=Input, 1=Output */

	ftStatus = FT_Write(handle, &serialDataOut, 9, &bytesWritten);
	if (ftStatus != FT_OK)
	{
		printf("FT_Write failed (error %d).\n", (int)ftStatus);
		return ftStatus;
	}

	serialDataOut[0] = 0x83; /* Read the Data Bus */

	ftStatus = FT_Write(handle, &serialDataOut, 1, &bytesWritten);
	if (ftStatus != FT_OK)
	{
		printf("FT_Write failed (error %d).\n", (int)ftStatus);
		return ftStatus;
	}

	ftStatus = FT_Read(handle, memData, 1, &bytesWritten);
	if (ftStatus != FT_OK)
	{
		printf("FT_Read failed (error %d).\n", (int)ftStatus);
		return ftStatus;
	}

	/* Disable the Output Enable */
	serialDataOut[0] = 0x82; /* Command GPIO LOW (ADBUS)*/ 
	serialDataOut[1] = 0x7F; /* OE_b = 1 */
	serialDataOut[2] = 0xFB; /* Direction 0=Input, 1=Output */

	ftStatus = FT_Write(handle, &serialDataOut, 3, &bytesWritten);
	if (ftStatus != FT_OK)
	{
		printf("FT_Write failed (error %d).\n", (int)ftStatus);
		return ftStatus;
	}

return FT_OK;
	
}

FT_STATUS EEPROM_SetAddress(FT_HANDLE handle, int memAddress)
{
	FT_STATUS	ftStatus = FT_OK;
	DWORD       bytesWritten = 0;
	unsigned char serialDataOut[20];

	/* Send Data on Clock Data Bytes Out on -ve clock edge LSB first (no read)
	 */
	serialDataOut[0] = 0x11; /* Set Command */ 
	serialDataOut[1] = 0x01; /* Length LSB Note: Size Minus ONE*/
	serialDataOut[2] = 0x00; /* Length MSB */
	serialDataOut[3] = (unsigned char)((memAddress >> 8) & 0xFF); /* Data Address MSB */
	serialDataOut[4] = (unsigned char)(memAddress & 0xFF); /* Data Address LSB */
	/* Latch Memory Address from Shift Registers 
	 */
	serialDataOut[5] = 0x80; /* Command GPIO LOW (ADBUS)*/ 
	serialDataOut[6] = 0x7F; /* Set Latch Low */
	serialDataOut[7] = 0xFB; /* Direction 0=Input, 1=Output */
	serialDataOut[8] = 0x80; /* Command GPIO LOW (ADBUS)*/ 
	serialDataOut[9] = 0xFF; /* Set Latch High */
	serialDataOut[10] = 0xFB; /* Direction 0=Input, 1=Output */
	serialDataOut[11] = 0x80; /* Command GPIO LOW (ADBUS)*/ 
	serialDataOut[12] = 0x7F; /* Set Latch Low */
	serialDataOut[13] = 0xFB; /* Direction 0=Input, 1=Output */	

	ftStatus = FT_Write(handle, &serialDataOut, 14, &bytesWritten);
	if (ftStatus != FT_OK)
	{
		printf("FT_Write failed (error %d).\n", ftStatus);
		return ftStatus;
	}

return FT_OK;
}

/* print command usage */
void usage(char *name) {
    fprintf(stderr, "usage: %s [-h] [-v] [-l <LoadAddress>] [-p <port number>] <Filename>\n", name);
}

/* Show help info */
void showHelp(char *name)
{
    usage(name);
    fprintf(stderr,
            "\n-h  Show help info and exit.\n"
            "-v  Show verbose output.\n"
			"-p <port number>  Specify the FTDI USB Port Number (default to 0)\n"
            "-l <LoadAddress>  Specify beginning load address (defaults to 0x0000).\n"
            "Addresses can be specified in decimal or hex (prefixed with 0x). A\n\n\n");
}