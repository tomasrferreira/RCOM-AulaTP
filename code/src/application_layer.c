// Application layer protocol implementation

#include <stdlib.h>
#include "application_layer.h"
#include "link_layer.h"
#include "serial_port.h"
#include "string.h"
#include <stdio.h>

#define max_packet_size 255
#define END_OF_TRANSMISSION 0x04  // Special control character indicating end of transmission

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
            printf("Read %d bytes from file. Sending using llwrite...\n", bytesRead);
            int result = llwrite(buffer, bytesRead);
            if (result != 0) {
                printf("Failed to send data using llwrite. Exiting.\n");
                break;
            }
        }

        // Signal end of transmission to the receiver
        unsigned char end_signal[] = {END_OF_TRANSMISSION};
        llwrite(end_signal, 1);

        fclose(file);
    } else if (strcmp(role, "rx") == 0) {
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
            printf("Waiting for data...\n");
            bytesRead = llread(packet);
            if (bytesRead > 0) {
                // Check if it's the end of transmission signal
                if (bytesRead == 1 && packet[0] == END_OF_TRANSMISSION) {
                    printf("End of transmission received. Exiting.\n");
                    break;
                }

                printf("Received %d bytes, writing to file.\n", bytesRead);
                fwrite(packet, 1, bytesRead, file);

                // Send ACK after successfully reading a frame
                unsigned char ack = 0x05;
                writeBytesSerialPort(&ack, 1);
            } else if (bytesRead == -2) {
                printf("Checksum mismatch. Sending NACK...\n");
                unsigned char nack = 0x15;
                writeBytesSerialPort(&nack, 1);
            } else {
                printf("Failed to read data. Exiting.\n");
                break;
            }
        }
        fclose(file);
    }

    // Close the serial port after operations are complete
    closeSerialPort();
}
