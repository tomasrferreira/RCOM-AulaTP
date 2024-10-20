// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"
#include <unistd.h>
#include <stdio.h>
#include <signal.h>

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source
#define BUF_SIZE 5

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////

int alarmEnabled = FALSE;
int alarmCount = 0;
volatile int stop = FALSE;

void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    alarmCount++;

    printf("Alarm #%d\n", alarmCount);
}

int llopen(LinkLayer connectionParameters){
    int fd = openSerialPort(connectionParameters.serialPort,connectionParameters.baudRate);
    if (fd < 0){
        return -1;
    }

    int alarmCount = 0;
    int retransmissions = connectionParameters.nRetransmissions;
    volatile int stop = FALSE;
    int state = 0;
    unsigned char buf = 0;

    if (connectionParameters.role == LlTx){
        (void)signal(SIGALRM, alarmHandler);

        while (alarmCount < retransmissions){
            if (alarmEnabled == FALSE){
                unsigned char SET[5] = {0x7E,0x03,0x03,0x0,0x7E};
                printf("SET sent.\n");
                writeBytesSerialPort(SET, 5);
                alarm(connectionParameters.timeout);
                alarmEnabled = TRUE;
            }
            while (alarmEnabled == TRUE) {
                if (alarmEnabled == TRUE) {break;}
                readByteSerialPort(&buf);
                if (buf > 0){
                    switch (buf){
                        case 0x7E:
                            if (state == 4) {
                                printf("UA received.\n");
                                alarm(0);
                                return fd;
                            }
                            else{
                                state = 1;
                                continue;
                            }

                        case 0x03:
                            if (state == 1) {
                                state = 2;
                                continue;
                            }
                            else {
                                state = 0;
                                continue;
                            }
                        case 0x04:
                            if (state == 3){
                                state = 4;
                                continue;
                            }
                            else {
                                state = 0;
                                continue;
                            }
                        case 0x07:
                            if (state == 2){
                                state = 3;
                                continue;
                            }
                            else {
                                state = 0;
                                continue;
                            }
                        default:
                            state = 0;
                            continue;
                    }
                }
            }
        }
        alarm(0);
        return -1;
    }
    else if (connectionParameters.role == LlRx){
        state = 0;
        unsigned char UA[5] = {0x7E,0x03,0x07,0x04,0x7E};
        stop = FALSE;
        while (stop == FALSE){
            int bytes = readByteSerialPort(&buf);
            if (bytes > 0){
                switch (buf){
                    case 0x7E:
                        if (state == 4) {
                          printf("SET received.\n");
                          printf("UA Sent : 0x%x%x%x%x%x\n",UA[0],UA[1],UA[2],UA[3],UA[4]);
                          writeBytesSerialPort(UA, 5);
                          continue;
                        }
                        state = 1;
                        continue;
                    case 0x03:
                        if (state == 1 || state == 2) {
                            state++;
                            continue;
                        }
                        state = 0;
                        continue;
                    case 0x0:
                        if (state == 3){
                            state = 4;
                            continue;
                        }
                        state = 0;
                        continue;
                    default:
                      state = 0;
                      continue;
                }
            }
        }
        return 0;
    }
    return -1;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize)
{
    unsigned char FLAG = 0x7E;
    unsigned char controlField = 0x03;  
    unsigned char addressField = 0x01;  
    unsigned char frame[bufSize + 6]; 
    int frameIndex = 0;

    frame[frameIndex++] = FLAG;
    frame[frameIndex++] = addressField;
    frame[frameIndex++] = controlField;

    for (int i = 0; i < bufSize; i++) {
        if (buf[i] == FLAG) {
            frame[frameIndex++] = 0x7D;  
            frame[frameIndex++] = buf[i] ^ 0x20;  
        }
        else {
            frame[frameIndex++] = buf[i];
        }
    }

    unsigned char checksum = 0;
    for (int i = 0; i < bufSize; i++) {
        checksum ^= buf[i];
    }
    frame[frameIndex++] = checksum;
    frame[frameIndex++] = FLAG;

    int result = writeBytesSerialPort(frame, frameIndex);
    if (result != frameIndex) {
        printf("Error writing to serial port!\n");
        return -1;
    }
    return frameIndex;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    // TODO

    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    // TODO

    int clstat = closeSerialPort();
    return clstat;
}
