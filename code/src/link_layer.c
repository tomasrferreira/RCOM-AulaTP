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
LinkLayer connectionParams;

void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    alarmCount++;
}

int llopen(LinkLayer connectionParameters) {
    // Copy the connection parameters to the global variable
    connectionParams = connectionParameters;

    int fd = openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate);
    if (fd < 0) {
        return -1;
    }

    int retransmissions = connectionParameters.nRetransmissions;
    unsigned char SET[5] = {0x7E,0x03,0x03,0x0,0x7E};
    int state = 0;
    unsigned char buf = 0;

    if (connectionParameters.role == LlTx){
        (void)signal(SIGALRM, alarmHandler);

        while (alarmCount < retransmissions){
            if (alarmEnabled == FALSE){
                printf("SET sent.\n");
                writeBytesSerialPort(SET, 5);
                alarmEnabled = TRUE;
                alarm(connectionParameters.timeout);
            }
            while (alarmEnabled == TRUE) {
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
                        case 0x07:
                            if (state == 2){
                                state = 3;
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
                        default:
                            state = 0;
                            continue;
                    }
                }
            }
        }
        alarm(0);
    }
    else if (connectionParameters.role == LlRx){
        state = 0;
        unsigned char UA[5] = {0x7E, 0x03, 0x07, 0x04, 0x7E};
        while (1) {
            int bytes = readByteSerialPort(&buf);
            if (bytes > 0) {
                switch (buf) {
                    case 0x7E:
                        if (state == 4) {
                            printf("SET received.\n");
                            printf("UA Sent : 0x%x%x%x%x%x\n", UA[0], UA[1], UA[2], UA[3], UA[4]);
                            writeBytesSerialPort(UA, 5);
                            alarm(0);
                            return fd;
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
                        if (state == 3) {
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
    }
    alarm(0);
    return -1;
}


////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize) {
    const unsigned char flag = 0x7E;
    const unsigned char ack = 0x05;
    const unsigned char nack = 0x15;
    const int max_retries = connectionParams.nRetransmissions;

    unsigned char frame[2 * bufSize + 10];
    int frame_length = 0;
    int retries = 0;
    int ack_received = 0;

    // Prepare the frame
    frame[frame_length++] = flag;           // Start flag
    frame[frame_length++] = 0x03;           // Address
    frame[frame_length++] = 0x00;           // Control

    // BCC1
    unsigned char BCC1 = frame[1] ^ frame[2];
    frame[frame_length++] = BCC1;           // Add BCC1


    // Calculate BCC2 (checksum) over data with byte-stuffing
    unsigned char BCC2 = 0;
    for (int i = 0; i < bufSize; i++) {
        BCC2 ^= buf[i];
        if (buf[i] == flag || buf[i] == 0x7D) {
            frame[frame_length++] = 0x7D;
            frame[frame_length++] = buf[i] ^ 0x20;
        } else {
            frame[frame_length++] = buf[i];
        }
    }

    // Add BCC2 with byte-stuffing if necessary
    if (BCC2 == flag || BCC2 == 0x7D) {
        frame[frame_length++] = 0x7D;
        frame[frame_length++] = BCC2 ^ 0x20;
    } else {
        frame[frame_length++] = BCC2;
    }

    frame[frame_length++] = flag;        // End flag

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
        if (readByteSerialPort(&response) == 1) {
            if (response == ack) {
                printf("ACK received for frame.\n");
                ack_received = 1;
            } else if (response == nack) {
                printf("NACK received, retrying...\n");
                retries++;
            } else {
                printf("Unexpected response. Retrying...\n");
                retries++;
            }
        } else {
            printf("No response detected; retrying...\n");
            retries++;
        }
    }

    return ack_received ? 0 : -1;        // Return 0 if ACK received, -1 otherwise
}


////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet) {
    const unsigned char flag = 0x7E;
    unsigned char byte;
    unsigned char frame[2048];        // Frame buffer
    int frame_length = 0;
    int data_length = 0;
    int start_flag_detected = 0;
    int escape_next = 0;

    printf("Waiting for data...\n");

    // Read bytes until a complete frame is detected
    while (readByteSerialPort(&byte) == 1) {
        if (byte == flag) {
            if (!start_flag_detected) {
                start_flag_detected = 1;
                frame_length = 0;
                printf("Start flag detected.\n");
                continue;
            } else {
                printf("End flag detected. Frame read complete.\n");
                break;
            }
        }

        if (start_flag_detected) {
            if (byte == 0x7D) {        // Byte-stuffing detected
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
                    return -1;
                }
            }
        }
    }

    // Validate frame length before processing
    if (frame_length < 5) {
        printf("Invalid frame length: %d\n", frame_length);
        return -1;
    }

    unsigned char address = frame[0];
    unsigned char control = frame[1];
    unsigned char BCC1 = address ^ control;


    // Check BCC1 for Address and Control field
    if (BCC1 != frame[2]) {
        printf("BCC1 mismatch. Sending NACK...\n");
        unsigned char nack = 0x15;
        writeBytesSerialPort(&nack, 1);
        return -2;
    }

    // Extract and validate data bytes
    data_length = frame_length - 4;     // Exclude Address, Control, BCC1, and final flag
    for (int i = 0; i < data_length; i++) {
        packet[i] = frame[i + 3];
    }

    // Validate BCC2 (checksum)
    unsigned char BCC2 = frame[frame_length - 2];
    unsigned char calculated_BCC2 = 0;
    for (int i = 0; i < data_length; i++) {
        calculated_BCC2 ^= packet[i];
    }

    if (BCC2 != calculated_BCC2) {
        printf("BCC2 mismatch. Sending NACK...\n");
        unsigned char nack = 0x15;
        writeBytesSerialPort(&nack, 1);
        return -2;
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
int llclose(int showStatistics) {
    unsigned char buf = 0;
    int state = 0;

    unsigned char DISC_TX[5] = {0x7E, 0x03, 0x0B, 0x08, 0x7E}; // DISC frame from transmitter
    unsigned char DISC_RX[5] = {0x7E, 0x01, 0x0B, 0x0A, 0x7E}; // DISC frame from receiver
    unsigned char UA[5] = {0x7E, 0x03, 0x07, 0x04, 0x7E}; // UA frame to finalize disconnection

    if (connectionParams.role == LlTx) { // Transmitter side
        printf("Transmitter initiating disconnection...\n");
        (void)signal(SIGALRM, alarmHandler);
        alarmCount = 0;
        alarmEnabled = FALSE;

        while (alarmCount < connectionParams.nRetransmissions) {
            if (alarmEnabled == FALSE) {
                printf("Transmitter: Sending DISC frame.\n");
                writeBytesSerialPort(DISC_TX, 5); // Send DISC frame to Receiver
                alarmEnabled = TRUE;
                alarm(connectionParams.timeout); // Start alarm for timeout
            }

            // Wait for DISC response from Receiver
            while (alarmEnabled) {
                int bytesRead = readByteSerialPort(&buf);
                if (bytesRead > 0) {
                    switch (buf) {
                        case 0x7E:
                            if (state == 4) {
                                printf("Transmitter: DISC received from Receiver.\n");
                                // Send UA to finalize disconnection
                                printf("Transmitter: Sending UA frame.\n");
                                writeBytesSerialPort(UA, 5);
                                alarm(0); // Stop the alarm
                                closeSerialPort();
                                return 0;
                            }
                            state = 1; // Start of frame detected
                            break;

                        case 0x01:
                            if (state == 1) state = 2; // Expecting A = 0x01 (Receiver's DISC)
                            else state = 0; // Reset if unexpected
                            break;

                        case 0x0B:
                            if (state == 2) state = 3; // C = 0x0B (DISC control byte)
                            else state = 0; // Reset if unexpected
                            break;

                        case 0x0A:
                            if (state == 3) state = 4; // BCC check for DISC
                            else state = 0; // Reset if unexpected
                            break;

                        default:
                            state = 0;
                            break;
                    }
                }
            }
        }
        alarm(0);
        printf("Transmitter: Failed to close connection after %d retransmissions.\n", alarmCount);
        return -1;

    } else if (connectionParams.role == LlRx) {
        printf("Receiver awaiting DISC from Transmitter...\n");

        while (1) {

            int bytesRead = readByteSerialPort(&buf);
            if (bytesRead > 0) {
                switch (buf) {
                    case 0x7E:
                        if (state == 4) {
                            // End flag of the DISC frame received from Transmitter
                            printf("Receiver: DISC received from Transmitter.\n");
                            printf("Receiver: Sending DISC frame.\n");
                            writeBytesSerialPort(DISC_RX, 5);

                            state = 0;
                            int uaState = 0;
                            while (1) {
                                int uaBytes = readByteSerialPort(&buf);
                                if (uaBytes > 0) {
                                    switch (buf) {
                                        case 0x7E:
                                            if (uaState == 4) { // End flag of UA frame
                                                printf("Receiver: UA received from Transmitter. Closing connection.\n");
                                                closeSerialPort();
                                                return 0;
                                            }
                                            uaState = 1;
                                            break;

                                        case 0x03:
                                            if (uaState == 1) uaState = 2; // A = 0x03
                                            else uaState = 0;
                                            break;

                                        case 0x07:
                                            if (uaState == 2) uaState = 3; // C = 0x07 (UA control byte)
                                            else uaState = 0;
                                            break;

                                        case 0x04:
                                            if (uaState == 3) uaState = 4; // BCC check for UA
                                            else uaState = 0;
                                            break;

                                        default:
                                            uaState = 0;
                                            break;
                                    }
                                }
                            }
                        }
                        state = 1;
                        break;

                    case 0x03:
                        if (state == 1) state = 2; // A = 0x03 from Transmitter DISC
                        else state = 0;
                        break;

                    case 0x0B:
                        if (state == 2) state = 3; // C = 0x0B (DISC control byte)
                        else state = 0;
                        break;

                    case (0x03 ^ 0x0B):
                        if (state == 3) state = 4; // BCC check
                        else state = 0;
                        break;

                    default:
                        state = 0;
                        break;
                }
            }
        }
    }

    return -1; // Return -1 if disconnection fails for either role
}



