// Application layer protocol implementation

#include <stdlib.h>
#include "application_layer.h"
#include "link_layer.h"
#include "serial_port.h"
#include "string.h"
#include <stdio.h>

#define max_packet_size 255

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer ll;
    strncpy(ll.serialPort, serialPort, sizeof(ll.serialPort) - 1);
    ll.serialPort[sizeof(ll.serialPort) - 1] = '\0';

    ll.baudRate = baudRate;
    ll.nRetransmissions = nTries;
    ll.timeout = timeout;

    int fd = openSerialPort(serialPort, baudRate);
    if (fd < 0) {
        printf("Failed to open serial port.\n");
        return;
    }

    if (strcmp(role, "tx") == 0) {
        ll.role = LlTx;

        FILE *file = fopen(filename, "rb");
        if (!file) {
            printf("Failed to open file %s for reading.\n", filename);
            closeSerialPort();
            return;
        }

        unsigned char buffer[max_packet_size];
        int bytesRead;
        while ((bytesRead = fread(buffer, 1, max_packet_size, file)) > 0) {
            int result = llwrite(buffer, bytesRead);
            if (result != 0) {
                printf("Failed to send data using llwrite. Exiting.\n");
                break;
            }
        }
        fclose(file);
    }
    else if (strcmp(role, "rx") == 0) {
        ll.role = LlRx;

        FILE *file = fopen(filename, "wb");
        if (!file) {
            printf("Failed to open file %s for writing.\n", filename);
            closeSerialPort();
            return;
        }

        unsigned char packet[max_packet_size];
        int bytesRead;
        while (1) {
            bytesRead = llread(packet);
            if (bytesRead > 0) {
                fwrite(packet, 1, bytesRead, file);
                unsigned char ack = 0x05;
                writeBytesSerialPort(&ack, 1);
            } else {
                unsigned char nack = 0x15;
                writeBytesSerialPort(&nack, 1);
            }
        }
        fclose(file);
    }
    closeSerialPort();
}
