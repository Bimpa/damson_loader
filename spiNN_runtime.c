/******************************************************************************************
 * SPINNAKER Host side API
 * Copyright (c) 2011, University of Sheffield
 *
 * Contributors:
 *     Paul Richmond - initial development
 *     Jim Garside - initial design documentation
 ******************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <limits.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>


#include "spiNN_runtime.h"

#define DEFAULT_LOAD_ADDRESS 0xf5000000

#define SPINNAKER_CMD_PORT 17893
#define SPINNAKER_BOOT_PORT 54321
#define SPINNAKER_DEBUG_OUTPUT_PORT 17892

#define SPINNAKER_BOOT_FILE "boot.bin"
#define SPINNAKER_MAX_BOOT_SIZE (SPINNAKER_BOOT_DATA_MAX*32)
#define SPINNAKER_BOOT_DATA_MAX 1024
#define SDP_DATA_MAX 256

#define SPINNAKER_CMD_DELAY 10000
#define TIMEOUT_SEC 1

#define SPINNAKER_BOOT_CMD_START 1
#define SPINNAKER_BOOT_CMD_DATA 3
#define SPINNAKER_BOOT_CMD_END 5

#define SDP_HDR_SIZE 		sizeof(sdp_hdr)
#define BOOT_HDR_SIZE 		sizeof(boot_hdr)
#define CMD_RESP_HDR_SIZE 	sizeof(sdp_cmd_resp_hdr)
#define SVER_SIZE 			sizeof(sver)

#define CMD_SVER 0
#define CMD_IPTAG 18
#define CMD_P2PC 13
#define CMD_READ 2
#define CMD_WRITE 3
#define CMD_APLX 4

#define IPTAG_CLR 3
#define IPTAG_AUTO 4

#define TYPE_BYTE 0
#define TYPE_HALF 1
#define TYPE_WORD 2

//avoid aligned packing of structs !!
#pragma pack(push, 1)

typedef struct{
	unsigned char tto;				//timeout ??
	unsigned char undef;			//0 ??

	unsigned char flags;			// 0x87
	unsigned char tag;				// 255
	unsigned char dst_core_id;		//destination chip
	unsigned char src_core_id;		//source chip
	unsigned short dst_cpu;			//destination cpu
	unsigned short src_cpu;			//source cpu

	unsigned short cmd;
	unsigned short cmd_flags;
	unsigned long arg1;
	unsigned long arg2;
	unsigned long arg3;
} sdp_hdr;

typedef struct{
	unsigned short prot_ver;
	unsigned long op;
	unsigned long a1;
	unsigned long a2;
	unsigned long a3;
}boot_hdr;

typedef struct{
	unsigned short pad;
	unsigned char flags;
	unsigned char tag;
	unsigned char dst_core_id;
	unsigned char src_core_id;
	unsigned short dst_cpu;
	unsigned short src_cpu;
	unsigned short rc;
	unsigned short cmd_flags;
}sdp_cmd_resp_hdr;

typedef struct{
	unsigned char v_cpu;
	unsigned char p_cpu;
	unsigned char chip_y;
	unsigned char chip_x;
	unsigned short size;
	unsigned short ver_num;
	unsigned long time;
}sver;

#pragma pack(pop)

//global variables
unsigned int spiNN_sock;													//SpiNN socket handle
unsigned int debug_sock;													//SpiNN socket handle
spiNN_error last_error = SPINN_NO_ERROR;									//last error code
void (*error_handler)(void) = &spiNN_print_error;							//error handler function (default is print_last_error)
void (*debug_handler)(SpiNN_address, char*) = &spiNN_handle_debug_message;	//debug message handler function (default is spiNN_recieve_debug_message)
struct sockaddr_in spiNN_addr;												//spiNN address
pthread_t debug_thread;														//thread handle for debug thread (blocks on socket recv)


//private prototypes
int check_SpiNN_address(SpiNN_address* address);							//checks range of core_id
int connect_sdp(char* device_ip, unsigned int port);						//connects SpiNNaker command/sdp socket
int connect_debug();														//connects debug socket
int send_cmd(sdp_hdr* hdr, const char* data, int data_length, sdp_cmd_resp_hdr* response, char* rsp_data);	//sends command via sdp (checks for response)
int send_boot_pkt(unsigned int boot_sock, struct sockaddr_in *boot_addr, boot_hdr* hdr, const char* data, int data_length);
int boot(char* device_ip);																	//sends the boot image to spinnaker

void* listen_debug(void* null);												//debug listener function


int spiNN_init_port(char* device_ip, int x_dimension, int y_dimension, unsigned int port)
{
	sdp_hdr hdr;
	sdp_cmd_resp_hdr resp_hdr;
	char resp_data[SDP_DATA_MAX];
	int id;




	//common hdr values
	id = 2;
	memset(&hdr, 0, SDP_HDR_SIZE);
	hdr.tto = 8;
	hdr.flags = 0x87;
	hdr.tag = 255;
	hdr.src_core_id = 255;

	if (!connect_sdp(device_ip, port))	//connect to SpiNNaker
		return SPINN_FAILURE;

	//send boot image
	if (!boot(device_ip))
		return SPINN_FAILURE;

	if (!spiNN_test_connection())
		return SPINN_FAILURE;

	//connect debug
	if (!connect_debug())
		return SPINN_FAILURE;

	//clear iptag
	hdr.cmd = CMD_IPTAG;
	hdr.arg1 = (IPTAG_CLR << 16);
	if (!send_cmd(&hdr, "", 0, &resp_hdr, resp_data))
		return SPINN_FAILURE;

	//set iptag auto
	hdr.cmd = CMD_IPTAG;
	hdr.arg1 = (IPTAG_AUTO << 16);
	hdr.arg2 = SPINNAKER_DEBUG_OUTPUT_PORT;
	if (!send_cmd(&hdr, "", 0, &resp_hdr, resp_data))
		return SPINN_FAILURE;


	//set p2p communication
	hdr.cmd = CMD_P2PC;
	hdr.arg1 = (0x00 << 24) + (0x3e << 16) + (0x00 << 8) + id;
	hdr.arg2 = (x_dimension << 24) + (y_dimension << 16) + (0x00 << 8) + 0x00;
	hdr.arg3 = (0x00 << 24) + (0x00 << 16) + (0x3f << 8) + 0xf8;
	if (!send_cmd(&hdr, "", 0, &resp_hdr, resp_data))
		return SPINN_FAILURE;

	//start debug listener thread
	pthread_create(&debug_thread, NULL, listen_debug, 0);


	return SPINN_SUCCESS;
}

int spiNN_init(char* device_ip, int x_dimension, int y_dimension)
{
	return spiNN_init_port(device_ip, x_dimension, y_dimension, SPINNAKER_CMD_PORT);
}

int spiNN_test_connection()
{
	sdp_hdr hdr;
	sdp_cmd_resp_hdr resp_hdr;
	char resp_data[SDP_DATA_MAX];
	sver ver;

	//common hdr values
	memset(&hdr, 0, SDP_HDR_SIZE);
	hdr.tto = 8;
	hdr.flags = 0x87;
	hdr.tag = 255;
	hdr.src_core_id = 255;

	//send version query and get response
	memset(&resp_hdr, 0 , CMD_RESP_HDR_SIZE);
	hdr.cmd = CMD_SVER;

	if (!send_cmd(&hdr, "", 0, &resp_hdr, resp_data))
		return SPINN_FAILURE;

	memcpy(&ver, resp_data, SVER_SIZE);
	printf("Connected to SpiNNaker version 0.%i\n", ver.ver_num);

	return SPINN_SUCCESS;
}

void spiNN_exit()
{
	pthread_cancel(debug_thread);
	close(debug_sock);
	//pthread_exit(0);
	close(spiNN_sock);
}

int spiNN_load_application(SpiNN_chip_address chip, char* filename)
{
	SpiNN_address address;
	address.x = chip.x;
	address.y = chip.y;
	address.core_id = 0;
	return spiNN_load_application_at(address, filename, DEFAULT_LOAD_ADDRESS);
}

int spiNN_load_application_at(SpiNN_address address, char* filename, unsigned int device_address)
{
	unsigned int mem_addr;
	int len;
	sdp_hdr hdr;
	sdp_cmd_resp_hdr resp_hdr;
	char resp_data[SDP_DATA_MAX];
	FILE *fp;
	char f_buffer[SDP_DATA_MAX];

	mem_addr = device_address;
	//common hdr values
	memset(&hdr, 0, SDP_HDR_SIZE);
	hdr.tto = 8;
	hdr.flags = 0x87;
	hdr.tag = 255;
	hdr.src_core_id = 255;
	hdr.dst_cpu = (address.x << 8) + address.y;
	hdr.dst_core_id = address.core_id;

	//send version query and get response
	memset(&resp_hdr, 0 , CMD_RESP_HDR_SIZE);

	//open binary file for reading
	fp = fopen(filename, "rb");
	if (fp == NULL)
	{
		last_error = SPINN_ERROR_LOAD_FILE_OPEN;
		error_handler();
		return SPINN_FAILURE;
	}

	while (1){
		len = fread(f_buffer, 1, SDP_DATA_MAX, fp);
		if (len<=0)
			break;
		hdr.cmd = CMD_WRITE;
		hdr.arg1 = mem_addr;
		hdr.arg2 = len;
		hdr.arg3 = TYPE_BYTE;

		if (!send_cmd(&hdr, f_buffer, len, &resp_hdr, resp_data))
			return SPINN_FAILURE;

		mem_addr += len;
	}

	fclose(fp);

	return SPINN_SUCCESS;
}

int spiNN_start_application(SpiNN_address address)
{
	return spiNN_start_application_at(address, DEFAULT_LOAD_ADDRESS);
}

int spiNN_start_application_at(SpiNN_address address, unsigned int device_address)
{
	sdp_hdr hdr;
	sdp_cmd_resp_hdr resp_hdr;
	char resp_data[SDP_DATA_MAX];

	if (check_SpiNN_address(&address) == SPINN_FAILURE)
		return SPINN_FAILURE;

	if (address.core_id == 0)
	{
		last_error = SPINN_ERROR_START_APP_ON_MONITOR;
		error_handler();
		return SPINN_FAILURE;
	}

	//common hdr values
	memset(&hdr, 0, SDP_HDR_SIZE);
	hdr.tto = 8;
	hdr.flags = 0x87;
	hdr.tag = 255;
	hdr.src_core_id = 255;

	//send version query and get response
	//memset(resp_data, 0, SDP_DATA_MAX);
	memset(&resp_hdr, 0 , CMD_RESP_HDR_SIZE);
	hdr.dst_core_id = address.core_id;
	hdr.dst_cpu = (address.x << 8) + address.y;

	hdr.cmd = CMD_APLX;
	hdr.arg1 = device_address;

	if (!send_cmd(&hdr, "", 0, &resp_hdr, resp_data))
				return SPINN_FAILURE;

	//need to sleep to give the device time to actually start.
	usleep(SPINNAKER_CMD_DELAY);


	return SPINN_SUCCESS;
}



int spiNN_read_memory(SpiNN_address address, char* host_destination, unsigned int device_address, unsigned int size)
{
	int len;
	int remaining;
	sdp_hdr hdr;
	sdp_cmd_resp_hdr resp_hdr;
	char resp_data[SDP_DATA_MAX];

	if (check_SpiNN_address(&address) == SPINN_FAILURE)
		return SPINN_FAILURE;

	//common hdr values
	memset(&hdr, 0, SDP_HDR_SIZE);
	hdr.tto = 8;
	hdr.flags = 0x87;
	hdr.tag = 255;
	hdr.src_core_id = 255;
	hdr.dst_cpu = (address.x << 8) + address.y;
	hdr.dst_core_id = address.core_id;

	//send version query and get response
	memset(&resp_hdr, 0 , CMD_RESP_HDR_SIZE);

	int offset = 0;

	while (1){
		remaining = size-offset;
		len = (remaining > SDP_DATA_MAX)? SDP_DATA_MAX : remaining;

		hdr.cmd = CMD_READ;
		hdr.arg1 = device_address+offset;
		hdr.arg2 = len;
		hdr.arg3 = TYPE_BYTE;
		if (!send_cmd(&hdr, "", 0, &resp_hdr, resp_data))
			return SPINN_FAILURE;

		//copy into host memory
		memcpy(&host_destination[offset], resp_data, len);

		offset += len;

		if (offset == size)
			return SPINN_SUCCESS;
	}

	//should never get here!
	return SPINN_FAILURE;
}

int spiNN_write_memory(SpiNN_address address, char* host_destination, unsigned int device_address, unsigned int size)
{
	int len;
	int remaining;
	sdp_hdr hdr;
	char pkt_data[SDP_DATA_MAX];
	sdp_cmd_resp_hdr resp_hdr;
	char resp_data[SDP_DATA_MAX];

	if (size == 0)
		return SPINN_SUCCESS;

	if (check_SpiNN_address(&address) == SPINN_FAILURE)
		return SPINN_FAILURE;

	//common hdr values
	memset(&hdr, 0, SDP_HDR_SIZE);
	hdr.tto = 8;
	hdr.flags = 0x87;
	hdr.tag = 255;
	hdr.src_core_id = 255;
	hdr.dst_cpu = (address.x << 8) + address.y;
	hdr.dst_core_id = address.core_id;

	//send version query and get response
	memset(&resp_hdr, 0 , CMD_RESP_HDR_SIZE);

	int offset = 0;

	while (1){
		remaining = (size-offset);
		len = (remaining > SDP_DATA_MAX)? SDP_DATA_MAX : remaining;

		hdr.cmd = CMD_WRITE;
		//hdr.cmd_flags = 0;
		hdr.arg1 = device_address+offset;
		hdr.arg2 = len;
		hdr.arg3 = TYPE_BYTE;

		//copy into packet data
		memcpy(pkt_data, &host_destination[offset], len);

		if (!send_cmd(&hdr, pkt_data, len, &resp_hdr, resp_data))
			return SPINN_FAILURE;

		offset += len;
		if (offset == size)
			return SPINN_SUCCESS;
	}

	//should never get here!
	return SPINN_FAILURE;
}

int spiNN_writenonzero_memory(SpiNN_address address, char* host_destination, unsigned int device_address, unsigned int size)
{
	int i;
	int len;
	int remaining;
	sdp_hdr hdr;
	char pkt_data[SDP_DATA_MAX];
	sdp_cmd_resp_hdr resp_hdr;
	char resp_data[SDP_DATA_MAX];

	if (size == 0)
		return SPINN_SUCCESS;

	if (check_SpiNN_address(&address) == SPINN_FAILURE)
		return SPINN_FAILURE;

	//common hdr values
	memset(&hdr, 0, SDP_HDR_SIZE);
	hdr.tto = 8;
	hdr.flags = 0x87;
	hdr.tag = 255;
	hdr.src_core_id = 255;
	hdr.dst_cpu = (address.x << 8) + address.y;
	hdr.dst_core_id = address.core_id;

	//send version query and get response
	memset(&resp_hdr, 0 , CMD_RESP_HDR_SIZE);

	int offset = 0;


	while (1){
		//skip until non zero value
		if (host_destination[offset] == 0){
			offset++;
			if (offset == size)
				return SPINN_SUCCESS;
			continue;
		}

		remaining = (size-offset);
		len = (remaining > SDP_DATA_MAX)? SDP_DATA_MAX : remaining;

		//check packet data and cut short if 0 value is found
		for (i=offset; i<offset+len; i++){
			if (host_destination[i] == 0){
				len = i-offset;
				break;
			}
		}

		hdr.cmd = CMD_WRITE;
		hdr.arg1 = device_address+offset;
		hdr.arg2 = len;
		hdr.arg3 = TYPE_BYTE;

		//copy into packet data
		memcpy(pkt_data, &host_destination[offset], len);

		if (!send_cmd(&hdr, pkt_data, len, &resp_hdr, resp_data))
			return SPINN_FAILURE;

		offset += len;
		if (offset == size)
			return SPINN_SUCCESS;
	}

	//should never get here!
	return SPINN_FAILURE;
}



int spiNN_send_SDP_message(SpiNN_address address, char virtual_port, char* message, unsigned int message_len)
{
	sdp_hdr hdr;

	if (check_SpiNN_address(&address) == SPINN_FAILURE)
		return SPINN_FAILURE;

	if ((virtual_port<1)||(virtual_port>7))
	{
		last_error = SPINN_ERROR_SDP_VIRTUAL_PORT_RANGE;
		error_handler();
		return SPINN_FAILURE;
	}

	//hdr values
	memset(&hdr, 0, SDP_HDR_SIZE);
	hdr.tto = 1;
	hdr.flags = 0x87;		//no reply expected
	hdr.tag = 255;
	hdr.src_core_id = 255;
	hdr.dst_core_id = address.core_id + (virtual_port<<5);
	hdr.dst_cpu = (address.x << 8) + address.y;

	//send version query and get response
	if (message_len>SDP_DATA_MAX)
	{
		last_error = SPINN_ERROR_SDP_DATA_SIZE;
		error_handler();
		return SPINN_FAILURE;
	}

	//create the packet to be transmit (header plus the data part)
	char* packet = (char*)malloc(SDP_HDR_SIZE+message_len);
	memcpy(packet, &hdr, SDP_HDR_SIZE);
	memcpy(&packet[SDP_HDR_SIZE], message, message_len);

	int sent = sendto(spiNN_sock, packet, SDP_HDR_SIZE+message_len, 0, (struct sockaddr*)&spiNN_addr, sizeof(spiNN_addr));

	free(packet);

	if (sent<0){
		last_error = SPINN_ERROR_SDP_SEND;
		error_handler();
		return SPINN_FAILURE;
	}


	return SPINN_SUCCESS;
}

int spiNN_receive_SDP_message(SpiNN_address* source, char* virtual_port, char* message, int message_len)
{
	sdp_hdr resp_hdr;
	struct sockaddr from;
	unsigned int from_len;
	fd_set socks;
	struct timeval t;


	if (message_len>SDP_DATA_MAX)
	{
		last_error = SPINN_ERROR_SDP_DATA_SIZE;
		error_handler();
		return SPINN_FAILURE;
	}


	memset(&resp_hdr, 0 , SDP_HDR_SIZE);
	char* packet = (char*)malloc(SDP_HDR_SIZE+message_len);
	from_len = sizeof(from);

	//check for timeout
	FD_ZERO(&socks);
	FD_SET(spiNN_sock, &socks);
	t.tv_sec = TIMEOUT_SEC;
	if (!select(spiNN_sock+1, &socks, NULL, NULL, &t))
	{
		last_error = SPINN_ERROR_SDP_TIMEOUT;
		error_handler();
		return SPINN_FAILURE;
	}

	//receive packet
	int received = recvfrom(spiNN_sock, packet, SDP_HDR_SIZE+message_len, 0, (struct sockaddr*)&from, &from_len);
	if (received < 0){
		last_error = SPINN_ERROR_SDP_RECEIVE;
		error_handler();
		printf("an error: %s\n", strerror(errno));

		return SPINN_FAILURE;
	}

	//copy into hdr and data
	memcpy(&resp_hdr, packet, SDP_HDR_SIZE);
	memcpy(message, &packet[SDP_HDR_SIZE], message_len);
	free(packet);

	//update from details and virtual port from
	source->core_id = resp_hdr.src_core_id & 31;
	source->x = resp_hdr.src_cpu>>8;
	source->y = resp_hdr.src_cpu & 255;
	*virtual_port = resp_hdr.src_core_id >> 5;

	return SPINN_SUCCESS;
}


void spiNN_debug_message_callback(void (*receiveMessage)(SpiNN_address, char*))
{
	debug_handler = receiveMessage;
}

void spiNN_handle_debug_message(SpiNN_address address, char* message)
{
	int msg_len;

	//remove new line as this is enforced
	msg_len = strlen(message);
	if (message[msg_len-1] == '\n')
		message[msg_len-1] = '\0';

	printf("SpiNN Debug:%i,%i,%i> %s\n", address.x, address.y, address.core_id, message);
}



void spiNN_set_error_callback(void (*error_callback)(void))
{
	error_handler = error_callback;
}


spiNN_error spiNN_get_error(){
	return last_error;
}

const char* spiNN_get_error_string(spiNN_error error)
{
	switch(last_error)
	{
	case(SPINN_NO_ERROR):
			return "No Errors";
			break;
	case(SPINN_ERROR_CONNECTION_SOCKET_CREATION):
			return "Error creating comms socket during SpiNNaker connection";
			break;
	case(SPINN_ERROR_BOOT_SOCKET_CREATION):
			return "Error creating boot socket during SpiNNaker connection";
			break;
	case(SPINN_ERROR_BOOT_FILE_NOT_FOUND):
			return "Error opening boot file \"boot.bin\"";
			break;

	case(SPINN_ERROR_BOOT_FILE_TOO_LARGE):
			return "Error opening boot file \"boot.bin\" file too large";
			break;
	case(SPINN_ERROR_CONNECTION_SERVER_ADDRESS):
			return "Error in format of ip4 SpiNNaker device address";
			break;
	case(SPINN_ERROR_CONNECTION_DEBUG_SOCKET_CREATION):
			return "Error creating debug socket during SpiNNaker connection";
			break;
	case(SPINN_ERROR_CONNECTION_DEBUG_SOCKET_BIND):
			return "Error binding debug socket during SpiNNaker connection";
			break;
	case(SPINN_ERROR_DEBUG_LISTENER_RECEIVE):
			return "Error receiving data from debug socket";
			break;
	case(SPINN_ERROR_SDP_DATA_SIZE):
			return "Error SDP data size exceeds maximum data size";
			break;
	case(SPINN_ERROR_SDP_SEND):
			return "Error sending SDP message to SpiNNaker";
			break;
	case(SPINN_ERROR_SDP_RECEIVE):
			return "Error receiving SDP message response from SpiNNaker";
			break;
	case(SPINN_ERROR_SDP_TIMEOUT):
			return "No response from SpiNNaker (Target timeout).";
			break;
	case(SPINN_ERROR_SDP_CMD_SEND):
			return "Error sending command message to SpiNNaker";
			break;
	case(SPINN_ERROR_SDP_CMD_RECEIVE):
			return "Error receiving command message response from SpiNNaker";
			break;
	case(SPINN_ERROR_SDP_CMD_TIMEOUT):
			return "No response from SpiNNaker (Target timeout sending command).";
			break;
	case(SPINN_ERROR_BOOT_PKT_SEND):
			return "Error sending boot image to SpiNNaker";
			break;
	case(SPINN_ERROR_BOOT_PKT_TIMEOUT):
			return "No response from SpiNNaker (Target timeout sending boot image).";
			break;
	case(SPINN_ERROR_LOAD_FILE_OPEN):
			return "Error Failed to open file during load";
			break;
	case(SPINN_ERROR_START_APP_ON_MONITOR):
			return "Cannot start application on monitor core.";
			break;
	case(SPINN_ERROR_SDP_VIRTUAL_PORT_RANGE):
		return "Virtual port not within range 1-7 (0 reserved).";
		break;
	case(SPINN_ERROR_ADDRESS_CORE_ID):
		return "SpiNN_address core_id value outside the range of [0-(MAX_CORES_PERCHIP-1)].";
		break;
	default:
		return "No Error Description Found";
		break;
	}
}


void spiNN_print_error(void)
{
	if (last_error != SPINN_NO_ERROR)
		printf("ERROR(%i): %s\n", last_error, spiNN_get_error_string(last_error));
}

/* ------------------------------------------------------------------------------------------------
 * Private Function Declarations
 * ------------------------------------------------------------------------------------------------
 */

int check_SpiNN_address(SpiNN_address* address)
{
	if (address->core_id > MAX_CORES_PER_CHIP)
	{
		last_error = SPINN_ERROR_ADDRESS_CORE_ID;
		error_handler();
		return SPINN_FAILURE;
	}
	else
		return SPINN_SUCCESS;
}

int connect_sdp(char* device_ip, unsigned int port)
{
	in_addr_t addr;

	//create new UDP socket
	spiNN_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (spiNN_sock == -1){
		last_error = SPINN_ERROR_CONNECTION_SOCKET_CREATION;
		error_handler();
		return SPINN_FAILURE;
	}

	addr = inet_addr(device_ip);
	if (addr<0)
	{
		last_error = SPINN_ERROR_CONNECTION_SERVER_ADDRESS;
		error_handler();
		return SPINN_FAILURE;
	}
	//set all struct values to 0
	spiNN_addr.sin_family = AF_INET;
	spiNN_addr.sin_addr.s_addr = addr;
	spiNN_addr.sin_port = htons(port);		// network byte order

	return SPINN_SUCCESS;
}

int connect_debug()
{
	struct sockaddr_in 		debug_addr;

	//create new UDP socket
	debug_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (debug_sock == -1){
		last_error = SPINN_ERROR_CONNECTION_DEBUG_SOCKET_CREATION;
		error_handler();
		return SPINN_FAILURE;
	}

	//set all struct values to 0
	memset((char *) &debug_addr, 0, sizeof(debug_addr));
	debug_addr.sin_family = AF_INET;
	debug_addr.sin_addr.s_addr = INADDR_ANY;
	debug_addr.sin_port = htons(SPINNAKER_DEBUG_OUTPUT_PORT);		// network byte order

	if (bind(debug_sock, (struct sockaddr*) &debug_addr, sizeof(debug_addr))  == -1){
		last_error = SPINN_ERROR_CONNECTION_DEBUG_SOCKET_BIND;
		error_handler();
		return SPINN_FAILURE;
	}

	return SPINN_SUCCESS;
}

void* listen_debug(void* null)
{
	char debug_buffer[SDP_DATA_MAX];
	sdp_cmd_resp_hdr res;
	SpiNN_address address;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS,NULL);

	//printf("Debug waiting..\n");

	while(1)
	{
		memset(debug_buffer, 0, SDP_DATA_MAX);
		if (!recv(debug_sock, debug_buffer, SDP_DATA_MAX, 0)){
			last_error = SPINN_ERROR_DEBUG_LISTENER_RECEIVE;
			error_handler();
			return 0;
		}
		else
		{
			//get address info
			memcpy(&res, debug_buffer, CMD_RESP_HDR_SIZE);
			address.x = res.src_cpu >> 8;
			address.y = res.src_cpu & 255;
			address.core_id = res.src_core_id;

			//send to handler
			debug_handler(address, &debug_buffer[CMD_RESP_HDR_SIZE]);
		}
	}
	pthread_exit(NULL);
	return NULL;
}

int send_cmd(sdp_hdr* hdr, const char* data, int data_length, sdp_cmd_resp_hdr* response, char* rsp_data)
{
	//response
	struct sockaddr from;
	unsigned int from_len;
	fd_set socks;
	struct timeval t;
	char* packet;

	//create the packet to be transmit (header plus the data part)
	packet = (char*)malloc(SDP_HDR_SIZE+data_length);

	memcpy(packet, hdr, SDP_HDR_SIZE);
	memcpy(&packet[SDP_HDR_SIZE], data, data_length);

	int sent = sendto(spiNN_sock, packet, SDP_HDR_SIZE+data_length, 0, (struct sockaddr*)&spiNN_addr, sizeof(spiNN_addr));
	free(packet);

	if (sent<0){
		last_error = SPINN_ERROR_SDP_CMD_SEND;
		error_handler();
		return SPINN_FAILURE;
	}


	//create the packet to hold response
	packet = (char*)malloc(CMD_RESP_HDR_SIZE+SDP_DATA_MAX);
	from_len = sizeof(from);

	//check for timeout
	FD_ZERO(&socks);
	FD_SET(spiNN_sock, &socks);
	t.tv_sec = TIMEOUT_SEC;
	t.tv_usec = 0;
	if (!select(spiNN_sock+1, &socks, NULL, NULL, &t))
	{
		last_error = SPINN_ERROR_SDP_CMD_TIMEOUT;
		error_handler();
		free(packet);
		return SPINN_FAILURE;
	}

	//receive packet
	int received = recvfrom(spiNN_sock, packet, CMD_RESP_HDR_SIZE+SDP_DATA_MAX, 0, (struct sockaddr*)&from, &from_len);
	if (received < 0){
		last_error = SPINN_ERROR_SDP_CMD_RECEIVE;
		error_handler();
		//printf("an error: %s\n", strerror(errno));
		free(packet);
		return SPINN_FAILURE;
	}

	//copy into rsp_hdr and rsp_data
	memcpy(response, packet, CMD_RESP_HDR_SIZE);
	memcpy(rsp_data, &packet[CMD_RESP_HDR_SIZE], received - CMD_RESP_HDR_SIZE);
	free(packet);

	//sleep wait
	usleep(SPINNAKER_CMD_DELAY);

	return SPINN_SUCCESS;
}


//************************************************************************************************************

int send_boot_pkt(unsigned int boot_sock, struct sockaddr_in *boot_addr, boot_hdr* hdr, const char* data, int data_length)
{
	char* packet;
	int i;

	//create the packet to be transmit (header plus the data part)
	packet = (char*)malloc(BOOT_HDR_SIZE+data_length);
	memcpy(packet, hdr, BOOT_HDR_SIZE);
	for (i=0;i<data_length;i+=sizeof(unsigned long int))	//big endian format
	{
		unsigned long int l = *((unsigned int long*)&data[i]);
		l = ntohl(l);
		memcpy(&packet[BOOT_HDR_SIZE+i], &l, sizeof(unsigned long int));
	}


	int sent = sendto(boot_sock, packet, BOOT_HDR_SIZE+data_length, 0, (struct sockaddr*)boot_addr, sizeof(*boot_addr));
	free(packet);

	if (sent<0){
		last_error = SPINN_ERROR_BOOT_PKT_SEND;
		error_handler();
		return SPINN_FAILURE;
	}

	//sleep wait
	usleep(SPINNAKER_CMD_DELAY);

	return SPINN_SUCCESS;
}

int boot(char* device_ip)
{
	unsigned int boot_sock;
	unsigned int n;
	struct sockaddr_in boot_addr;
	in_addr_t addr;
	boot_hdr hdr;
	char boot_buffer[SPINNAKER_BOOT_DATA_MAX];
	FILE *boot_file;
	unsigned int blocks;
	unsigned int i;


	//create new UDP socket
	boot_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (boot_sock == -1){
		last_error = SPINN_ERROR_BOOT_SOCKET_CREATION;
		error_handler();
		return SPINN_FAILURE;
	}

	addr = inet_addr(device_ip);
	if (addr<0)
	{
		last_error = SPINN_ERROR_CONNECTION_SERVER_ADDRESS;
		error_handler();
		return SPINN_FAILURE;
	}
	//set all struct values to 0
	boot_addr.sin_family = AF_INET;
	boot_addr.sin_addr.s_addr = addr;
	boot_addr.sin_port = htons(SPINNAKER_BOOT_PORT);		// network byte order

	//open file
	if(!(boot_file = fopen(SPINNAKER_BOOT_FILE, "r")))
	{
		last_error = SPINN_ERROR_BOOT_FILE_NOT_FOUND;
		error_handler();
		return SPINN_FAILURE;
	}

	//check size
	fseek(boot_file, 0, SEEK_END);
	n = ftell(boot_file);
	fseek(boot_file, 0, SEEK_SET);
	if (n>SPINNAKER_MAX_BOOT_SIZE)
	{
		last_error = SPINN_ERROR_BOOT_FILE_TOO_LARGE;
		error_handler();
		return SPINN_FAILURE;
	}
	blocks = n / SPINNAKER_BOOT_DATA_MAX;
	if (n % SPINNAKER_BOOT_DATA_MAX != 0)
		blocks++;

	//common boot header data (big endian)
	hdr.prot_ver = htons(1);


	//send start
	hdr.op = ntohl(SPINNAKER_BOOT_CMD_START);
	hdr.a1 = 0;
	hdr.a2 = 0;
	hdr.a3 = ntohl(blocks-1);
	send_boot_pkt(boot_sock, &boot_addr, &hdr, "", 0);

	hdr.op = ntohl(SPINNAKER_BOOT_CMD_DATA);
	hdr.a2 = 0;
	hdr.a3 = 0;
	//send data in blocks
	for(i=0;i<blocks;i++)
	{
		//read block
		int n = fread(boot_buffer, 1, SPINNAKER_BOOT_DATA_MAX, boot_file);
		if (n <= 0)
		{
			last_error = SPINN_ERROR_BOOT_FILE_READING;
			error_handler();
			return SPINN_FAILURE;
		}
		//send block
		hdr.a1 = ntohl((((SPINNAKER_BOOT_DATA_MAX/4)-1)<<8)|((i)&255));
		send_boot_pkt(boot_sock, &boot_addr, &hdr, boot_buffer, n);
	}

	//send end
	hdr.op = ntohl(SPINNAKER_BOOT_CMD_END);
	hdr.a1 = ntohl(1);
	hdr.a2 = 0;
	hdr.a3 = 0;
	send_boot_pkt(boot_sock, &boot_addr, &hdr, "", 0);

	fclose(boot_file);
	close(boot_sock);

	usleep(SPINNAKER_CMD_DELAY*10);

	return SPINN_SUCCESS;
}


