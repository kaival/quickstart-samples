/* sub.c */

/*
 * This file was forked from libemqtt,
 * developed by Vicente Ruiz Rodríguez.
 * https://github.com/menudoproblema/libemqtt
 *
 */

#include "../include/libemqtt.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>

/* #include <linux/tcp.h> */	/* <-- For Linux applications */
#include <netinet/tcp.h>		/* <-- For OSX applications */

#include <signal.h>

// values below can be found by logging into the m2m.io portal (http://app.m2m.io)
// Username:  your email address you used to create the account
// Password:  MD5 hash of your password (32 character string)
// Domain:  Randomly generated.  Can be found on Accounts tab in portal.
// Device Type:  Can remain "things".  If this device is a commonly supported
//               device on the m2m.io portal there may be a type to choose.
//               For example, Arduino.
// Device ID: A string uniquely identifying this device from your other
//            devices.  Only needs to be unique to you, not across all users.
//            Common examples include device's MAC address or serial number.
//            Device-1, Device-2 are fine too.

#define  M2MIO_USERNAME   		"g3z559a6c1"
#define  M2MIO_PASSWORD   		"fe61771c6a61d59a3e6ea432521c3bf8"	/* pre-hashed (MD5) token */
#define  M2MIO_DOMAIN     		"maaakihz"
#define  M2MIO_DEVICE_TYPE		"test-topic"
#define  M2MIO_DEVICE_ID  		"testing-device"
#define  M2MIO_BROKER_HOSTNAME 	"q.m2m.io"
#define	 M2MIO_BROKER_PORT		1883

#define RCVBUFSIZE 1024
uint8_t packet_buffer[RCVBUFSIZE];

int keepalive = 30;
mqtt_broker_handle_t broker;

int socket_id;


int send_packet(void* socket_info, const void* buf, unsigned int count)
{
	int fd = *((int*)socket_info);
	return send(fd, buf, count, 0);
}

int init_socket(mqtt_broker_handle_t* broker, const char* hostname, short port, int keepalive)
{
	int flag = 1;

	// Create the socket
	if((socket_id = socket(PF_INET, SOCK_STREAM, 0)) < 0)
		return -1;

	// Disable Nagle Algorithm
	if (setsockopt(socket_id, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag)) < 0)
		return -2;

	struct sockaddr_in socket_address;
	// Create the stuff we need to connect
	socket_address.sin_family = AF_INET;
	socket_address.sin_port = htons(port);
	socket_address.sin_addr.s_addr = inet_addr(hostname);

	// Connect the socket
	if((connect(socket_id, (struct sockaddr*)&socket_address, sizeof(socket_address))) < 0)
		return -1;

	// MQTT stuffs
	mqtt_set_alive(broker, keepalive);
	broker->socket_info = (void*)&socket_id;
	broker->send = send_packet;

	return 0;
}

int close_socket(mqtt_broker_handle_t* broker)
{
	int fd = *((int*)broker->socket_info);
	return close(fd);
}

int read_packet(int timeout)
{
	if(timeout > 0)
	{
		fd_set readfds;
		struct timeval tmv;

		// Initialize the file descriptor set
		FD_ZERO (&readfds);
		FD_SET (socket_id, &readfds);

		// Initialize the timeout data structure
		tmv.tv_sec = timeout;
		tmv.tv_usec = 0;

		// select returns 0 if timeout, 1 if input available, -1 if error
		if(select(1, &readfds, NULL, NULL, &tmv))
			return -2;
	}

	int total_bytes = 0, bytes_rcvd, packet_length;
	memset(packet_buffer, 0, sizeof(packet_buffer));
	
	if((bytes_rcvd = recv(socket_id, (packet_buffer+total_bytes), RCVBUFSIZE - total_bytes, 0)) <= 0) {
		return -1;
	}

	total_bytes += bytes_rcvd; // Keep tally of total bytes
	if (total_bytes < 2)
		return -1;
	
	// now we have the full fixed header in packet_buffer
	// parse it for remaining length and number of bytes
	uint16_t rem_len = mqtt_parse_rem_len(packet_buffer);
	uint8_t rem_len_bytes = mqtt_num_rem_len_bytes(packet_buffer);
	
	//packet_length = packet_buffer[1] + 2; // Remaining length + fixed header length
	// total packet length = remaining length + byte 1 of fixed header + remaning length part of fixed header
	packet_length = rem_len + rem_len_bytes + 1;

	while(total_bytes < packet_length) // Reading the packet
	{
		if((bytes_rcvd = recv(socket_id, (packet_buffer+total_bytes), RCVBUFSIZE - total_bytes, 0)) <= 0)
			return -1;
		total_bytes += bytes_rcvd; // Keep tally of total bytes
	}

	return packet_length;
}

void alive(int sig)
{
	printf("Timeout! Sending ping...\n");
	mqtt_ping(&broker);

	alarm(keepalive);
}

void term(int sig)
{
	printf("Goodbye!\n");
	// >>>>> DISCONNECT
	mqtt_disconnect(&broker);
	close_socket(&broker);

	exit(0);
}

/**
 * Main routine
 *
 */
int main()
{
	int packet_length;
	uint16_t msg_id, msg_id_rcv;

	mqtt_init(&broker, "client-id");
	//mqtt_init_auth(&broker, "quijote", "rocinante");
	init_socket(&broker, "<broker ip address>", 1883, keepalive);

	// >>>>> CONNECT
	mqtt_connect(&broker);
	// <<<<< CONNACK
	packet_length = read_packet(1);
	if(packet_length < 0)
	{
		fprintf(stderr, "Error(%d) on read packet!\n", packet_length);
		return -1;
	}

	if(MQTTParseMessageType(packet_buffer) != MQTT_MSG_CONNACK)
	{
		fprintf(stderr, "CONNACK expected!\n");
		return -2;
	}

	if(packet_buffer[3] != 0x00)
	{
		fprintf(stderr, "CONNACK failed!\n");
		return -2;
	}

	// Signals after connect MQTT
	signal(SIGALRM, alive);
	alarm(keepalive);
	signal(SIGINT, term);

	// >>>>> SUBSCRIBE
	mqtt_subscribe(&broker, "public/test/topic", &msg_id);
	// <<<<< SUBACK
	packet_length = read_packet(1);
	if(packet_length < 0)
	{
		fprintf(stderr, "Error(%d) on read packet!\n", packet_length);
		return -1;
	}

	if(MQTTParseMessageType(packet_buffer) != MQTT_MSG_SUBACK)
	{
		fprintf(stderr, "SUBACK expected!\n");
		return -2;
	}

	msg_id_rcv = mqtt_parse_msg_id(packet_buffer);
	if(msg_id != msg_id_rcv)
	{
		fprintf(stderr, "%d message id was expected, but %d message id was found!\n", msg_id, msg_id_rcv);
		return -3;
	}

	while(1)
	{
		// <<<<<
		packet_length = read_packet(0);
		if(packet_length == -1)
		{
			fprintf(stderr, "Error(%d) on read packet!\n", packet_length);
			return -1;
		}
		else if(packet_length > 0)
		{
			printf("Packet Header: 0x%x...\n", packet_buffer[0]);
			if(MQTTParseMessageType(packet_buffer) == MQTT_MSG_PUBLISH)
			{
				uint8_t topic[255], msg[1000];
				uint16_t len;
				len = mqtt_parse_pub_topic(packet_buffer, topic);
				topic[len] = '\0'; // for printf
				len = mqtt_parse_publish_msg(packet_buffer, msg);
				msg[len] = '\0'; // for printf
				printf("%s %s\n", topic, msg);
			}
		}

	}
	return 0;
}
