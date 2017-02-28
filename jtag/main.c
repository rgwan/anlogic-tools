#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <libusb-1.0/libusb.h>
 #include <unistd.h>


#define VENDOR_ID	 0x0547   // Anlogic cable 's PID/VID
#define PRODUCT_ID	 0x1002   

#define JTAG_MODE	1
#define TEST_MODE	2

#define SPD_6M		0
#define SPD_3M		0x4
#define SPD_2M		0x8
#define SPD_1M		0x14
#define SPD_600K	0x24
#define SPD_400K	0x38
#define SPD_200K	0x70
#define SPD_100K	0xE8
#define SPD_90K		0xFF


static struct libusb_device_handle *devh = NULL;


static int ep_in_addr  = 0x82;
static int ep_out_addr = 0x06;

int write_to_cable(unsigned char *data, int size)
{
	/* To send data.to the cable
	 */
	int actual_length;
	int rc = libusb_bulk_transfer(devh, ep_out_addr, data, size,
							 &actual_length, 4000);
	if (rc < 0) {
		fprintf(stderr, "Error while bulking out: %s\n",  libusb_error_name(rc));
	}
	return actual_length;
}

int read_from_cable(unsigned char *data, int size)
{
	/* To receive data from device 's bulk endpoint
	 */
	int actual_length;
	int rc = libusb_bulk_transfer(devh, ep_in_addr, data, size, &actual_length,
								  4000);
	if (rc == LIBUSB_ERROR_TIMEOUT) {
		printf("timeout (%d)\n", actual_length);
		return -1;
	} else if (rc < 0) {
		fprintf(stderr, "Error while bulking in: %s\n",  libusb_error_name(rc));
		return -1;
	}

	return actual_length;
}

void hexdump(unsigned char *data, int size)
{
	int i;
	for(i = 0; i < size; i++)
	{
		printf("0x%02X ", data[i]);
		if((i + 1) % 8 == 0)
			putchar('\n');
	}
	putchar('\n');
}

int mode_switch(uint8_t mode, uint8_t speed)
{
	uint8_t outbuffer[2] = {mode, speed};
	int len;
	int rc = libusb_bulk_transfer(devh, 0x08, outbuffer, 2,
								 &len, 0);
	if (rc < 0) {
		fprintf(stderr, "Mode switch FAILED: %s \n", libusb_error_name(rc));
		return 0;
	}	
}

int jtag_frame_process(int frame_length, FILE *fp_in, FILE *fp_out, _Bool silence)
{
	int i,j;
	unsigned char line_buf1[100],line_buf2[100];
	unsigned char line_out[50];
	unsigned char char_tmp[6];   
	uint8_t outBuffer[512];
	uint8_t refBuffer[512];
	uint8_t maskBuffer[512];
	uint8_t inBuffer[512];
	
	int type = 0;  //write only

	for (i=0; i<frame_length/2; i++)     
	{
		fgets(line_buf1, 10, fp_in);
		fgets(line_buf2, 10, fp_in);
		for (j=0; j<6; j++)	char_tmp[j] = 0; 

		for (j=0; j<3; j++)
		{
			if ((line_buf2[j] == '1') || (line_buf2[j] == 'H')) // "1"
			char_tmp[0] |= (1<<(2-j));
		}
			char_tmp[0] = char_tmp[0]<<4;
		for (j=0; j<3; j++)
		{
			if ((line_buf1[j] == '1') || (line_buf1[j] == 'H')) // "1"
			char_tmp[0] |= (1<<(2-j));
		}


		if ((line_buf1[3] == '1') || (line_buf1[3] == 'H') || (line_buf1[3] == 'x') || (line_buf1[3] == 'X')) // "1
			char_tmp[1] = 1;
		if ((line_buf2[3] == '1') || (line_buf2[3] == 'H') || (line_buf2[3] == 'x') || (line_buf2[3] == 'X')) // "1
			char_tmp[2] = 1;

		if ((line_buf1[3] == 'x') || (line_buf1[3] == 'X')) // "1
			char_tmp[3] = 1;

		if ((line_buf2[3] == 'x') || (line_buf2[3] == 'X')) // "1
			char_tmp[4] = 1;

		outBuffer[i]    = char_tmp[0];
		refBuffer[2*i]  = char_tmp[1];
		refBuffer[2*i+1]  = char_tmp[2];
		maskBuffer[2*i]   = char_tmp[3];
		maskBuffer[2*i+1] = char_tmp[4];
	}  
	if(frame_length/2 != 512)     //最后不凑齐512部分用最后一个数据补齐
	{
		char_tmp[0] = char_tmp[0]&0xf0;
		char_tmp[0] = char_tmp[0]|(char_tmp[0]>>4);
		
		for (i=frame_length/2+1; i<512; i++) 
			{
				outBuffer[i]		= char_tmp[0];
				refBuffer[2*i]		= char_tmp[1];
				refBuffer[2*i+1]	= char_tmp[2];
				maskBuffer[2*i]		= char_tmp[3];
				maskBuffer[2*i+1]	= char_tmp[4];
			}
	}

	printf("Out Buffer\n");
	hexdump(outBuffer, 512);
	
	int actual_length;
	int rc = libusb_bulk_transfer(devh, ep_out_addr, outBuffer, frame_length,
							 &actual_length, 4000);
	if (rc < 0) {
		fprintf(stderr, "Error while bulking out: %s\n",  libusb_error_name(rc));
	}
	
	rc = libusb_bulk_transfer(devh, ep_in_addr, inBuffer, frame_length, &actual_length,
								  4000);
							
	if (rc == LIBUSB_ERROR_TIMEOUT) {
		printf("timeout (%d)\n", actual_length);
		return -1;
	} else if (rc < 0) {
		fprintf(stderr, "Error while bulking in: %s\n",  libusb_error_name(rc));
		return -1;
	}	

	printf("In Buffer\n");
	hexdump(inBuffer, 512);
		
	if(silence==0)
	{	
		for (i=0; i<frame_length/2 ; i=i+1)
		{
			char_tmp[0] = inBuffer[i]&0x01;
			char_tmp[1] = (inBuffer[i]>>4)&0x01;	

		//	printf("\ntest: line %d read = 0x%02x  ", 2*i, inBuffer[i]);
			
			for(j=0;j<2;j++)
			{
				if(maskBuffer[2*i+j]==1)
				{
					if(char_tmp[j])  
						fprintf(fp_out,"1\n");
					else
						fprintf(fp_out,"0\n");
				}
				else
				{
					if (char_tmp[j] != refBuffer[2*i+j])
					{
						printf("\nError: line %d read = 0x%02x   ref = 0x%02X", 2*i+j, char_tmp[j], refBuffer[2*i+j]);
						fprintf(fp_out,"N   Error: line %d read = 0x%02X   ref = 0x%02X\n", 2*i+j, inBuffer[2*i+j], refBuffer[2*i+j]);
					}
   					else if(char_tmp[j])  
						fprintf(fp_out,"1\n");
					else
						fprintf(fp_out,"0\n");
				}
			}
	    }	 
	}
}

int main(int argc, char **argv)
{
	int rc;
	FILE *fp;
	FILE *fp_o;

	/* Initialize libusb
	 */
	rc = libusb_init(NULL);
	if (rc < 0) {
		fprintf(stderr, "Error initializing libusb: %s\n", libusb_error_name(rc));
		exit(1);
	}

	/* Set debugging output to max level.
	 */
	libusb_set_debug(NULL, 3);

	/* Look for a specific device and open it.
	 */
	devh = libusb_open_device_with_vid_pid(NULL, VENDOR_ID, PRODUCT_ID);
	if (!devh) {
		fprintf(stderr, "Error finding USB device\n");
		goto out;
	}
	/* line:tck tdi tms tdo */
	mode_switch(JTAG_MODE, SPD_90K);
	fp = fopen("idcode.avc", "r");
	fp_o = fopen("out.avc", "w+");
	
	jtag_frame_process(512, fp, fp_o, 0);
		
	fclose(fp);
	fclose(fp_o);

out:
	if (devh)
		libusb_close(devh);
	libusb_exit(NULL);
	return rc;
}

