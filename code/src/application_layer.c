// Application layer protocol implementation

#include <stdlib.h>
#include "application_layer.h"
#include "link_layer.h"
#include "string.h"
#include <stdio.h>

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer ll;
    strncpy(ll.serialPort, serialPort, sizeof(ll.serialPort) - 1);
    ll.serialPort[sizeof(ll.serialPort) - 1] = '\0';
    if (strcmp(role, "tx") == 0){
      ll.role = LlTx;
    }
    else {
        ll.role = LlRx;
    }
    ll.baudRate = baudRate;
    ll.nRetransmissions = nTries;
    ll.timeout = timeout;

    if (llopen(ll) == -1) {
            printf("Error opening link layer!\n");
            return;
        }

    if (ll.role == LlTx) {
        FILE *file = fopen(filename, "rb");
        if (file == NULL) {
            printf("Error opening file %s!\n", filename);
            return;
        }

        fseek(file, 0, SEEK_END);
        int fileSize = ftell(file);
        fseek(file, 0, SEEK_SET);

        unsigned char *buf = (unsigned char *)malloc(fileSize);
        if (buf == NULL) {
            printf("Error allocating memory!\n");
            fclose(file);
            return;
        }

        size_t bytesRead = fread(buf, 1, fileSize, file);
        if (bytesRead != fileSize) {
            printf("Error reading file %s!\n", filename);
            free(buf);
            fclose(file);
            return;
        }

        if (llwrite(buf, fileSize) == -1) {
            printf("Error writing data to link layer!\n");
        }

        free(buf);
        fclose(file);
    }
    else if (ll.role == LlRx) {
        unsigned char buf[1024];
        int receivedSize = llread(buf);

        FILE *file = fopen(filename, "wb");
        if (file == NULL) {
            printf("Error opening file for writing: %s!\n", filename);
            return;
        }

        fwrite(buf, 1, receivedSize, file);
        fclose(file);
    }

    llclose(1);
}
