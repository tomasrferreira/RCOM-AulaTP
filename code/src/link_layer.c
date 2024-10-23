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
    const unsigned char nack = 0x15;
    const int max_retries = 3;

    unsigned char frame[2048]; // Larger buffer for potential stuffed bytes
    int frame_length = 0;
    int retries = 0;
    int ack_received = 0;

    // Prepare the frame
    frame[frame_length++] = flag; // Start flag
    frame[frame_length++] = 0x03; // Address
    frame[frame_length++] = 0x00; // Control

    // Calculate checksum over data
    unsigned char checksum = 0x03 ^ 0x00;
    for (int i = 0; i < bufSize; i++) {
        checksum ^= buf[i];
        // Byte-stuffing: escape `0x7E` and `0x7D` in data
        if (buf[i] == flag || buf[i] == 0x7D) {
            frame[frame_length++] = 0x7D;
            frame[frame_length++] = buf[i] ^ 0x20;
        } else {
            frame[frame_length++] = buf[i];
        }
    }

    // Add checksum with potential byte-stuffing
    if (checksum == flag || checksum == 0x7D) {
        frame[frame_length++] = 0x7D;
        frame[frame_length++] = checksum ^ 0x20;
    } else {
        frame[frame_length++] = checksum;
    }

    frame[frame_length++] = flag; // End flag

    // Retry sending until ACK or max retries
    while (retries < max_retries && !ack_received) {
        printf("Attempt %d to send frame\n", retries + 1);
        int bytes_written = writeBytesSerialPort(frame, frame_length);
        if (bytes_written < 0) {
            printf("Failed to transmit frame via serial port.\n");
            return -1;
        }

        printf("Frame sent, waiting for ACK or NACK...\n");

        unsigned char response;
        int res = readByteSerialPort(&response);
        if (res == 1) {
            printf("Received response: 0x%02X\n", response);
            if (response == ack) {
                ack_received = 1;
                printf("ACK received for frame.\n");
            } else if (response == nack) {
                printf("NACK received, retrying...\n");
                retries++;
            } else {
                printf("Unexpected response: 0x%02X. Retrying...\n", response);
                retries++;
            }
        } else {
            printf("No response detected; attempting retransmission...\n");
            retries++;
        }
    }

    if (!ack_received) {
        printf("Failed to receive ACK after %d retries.\n", max_retries);
    }

    return ack_received ? 0 : -1;
}


////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet) {
    const unsigned char flag = 0x7E;
    unsigned char byte;
    unsigned char frame[2048]; // Buffer to accommodate unstuffed data
    int frame_length = 0;
    int data_length = 0;
    unsigned char calculated_checksum = 0;
    unsigned char received_checksum = 0;
    int start_flag_detected = 0;
    int escape_next = 0;

    printf("Waiting for data...\n");

    // Read bytes until a complete frame is detected
    while (readByteSerialPort(&byte) == 1) {
        if (byte == flag) {
            if (!start_flag_detected) {
                start_flag_detected = 1;
                frame_length = 0;
                printf("Start flag detected. Reading frame...\n");
                continue;
            } else {
                printf("End flag detected. Frame read complete.\n");
                break;
            }
        }

        if (start_flag_detected) {
            if (byte == 0x7D) {
                escape_next = 1;
            } else {
                if (escape_next) {
                    frame[frame_length++] = byte ^ 0x20;
                    escape_next = 0;
                } else {
                    frame[frame_length++] = byte;
                }
                if (frame_length >= sizeof(frame)) {
                    printf("Frame too long, discarding.\n");
                    return -1; // Frame too long, discard it
                }
            }
        }
    }

    // Validate frame length before processing
    if (frame_length < 3) {
        printf("Invalid frame length: %d\n", frame_length);
        return -1;
    }

    // Extract address, control, and data bytes
    unsigned char address = frame[0];
    unsigned char control = frame[1];
    data_length = frame_length - 3; // Exclude address, control, and checksum

    printf("Frame received with length: %d\n", frame_length);
    printf("Data bytes: ");
    for (int i = 0; i < data_length; i++) {
        packet[i] = frame[i + 2];
        printf("0x%02X ", packet[i]);
    }
    printf("\n");

    // Calculate checksum including address and control
    calculated_checksum = address ^ control;
    for (int i = 0; i < data_length; i++) {
        calculated_checksum ^= packet[i];
    }
    received_checksum = frame[frame_length - 1];

    printf("Received checksum: 0x%02X, Calculated checksum: 0x%02X\n", received_checksum, calculated_checksum);

    // Verify checksum
    if (calculated_checksum != received_checksum) {
        printf("Checksum mismatch. Sending NACK...\n");
        unsigned char nack = 0x15;
        writeBytesSerialPort(&nack, 1);
        return -2; // Indicate checksum error
    }

    // Send ACK after successful frame reception
    unsigned char ack = 0x05;
    writeBytesSerialPort(&ack, 1);
    printf("Frame received and verified successfully.\n");

    return data_length;
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
