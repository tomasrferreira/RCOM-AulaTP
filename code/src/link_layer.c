// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

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
int llwrite(const unsigned char *buf, int bufSize) {
    const unsigned char flag = 0x7E;
    const unsigned char ack = 0x05;
    const int max_retries = 3;

    unsigned char frame[1024];
    int frame_length = 0;
    int retries = 0;
    int ack_received = 0;

    frame[frame_length++] = flag;
    frame[frame_length++] = 0x03;
    frame[frame_length++] = 0x00;
    frame[frame_length++] = 'T';

    unsigned char checksum = 0x03 ^ 0x00 ^ 'T';
    frame[frame_length++] = checksum;
    frame[frame_length++] = flag;

    while (retries < max_retries && !ack_received) {
        int bytes_written = writeBytesSerialPort(frame, frame_length);
        if (bytes_written < 0) {
            return -1;
        }

        unsigned char response;
        int res = readByteSerialPort(&response);
        if (res == 1 && response == ack) {
            ack_received = 1;
        } else {
            retries++;
        }
    }

    // Return success or failure after attempts
    return ack_received ? 0 : -1;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{

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
