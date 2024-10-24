// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"
#include "signal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source
#define ESCAPE 0x7D
#define STUFF 0x20

// Receiver's response control fields to inform transmitter of the received frame
#define C_RR0 0xAA
#define C_RR1 0xAB
#define C_REJ0 0x54
#define C_REJ1 0x55

#define FLAG 0x7E 
#define A_TX 0x03 // Transmitter
#define A_RX 0x01 // Receiver

// Supervision control fields
#define C_SET 0x03 // Transmitter
#define C_UA 0x07 // Receiver

#define DISC 0x0B

#define C_N(x) (x << 6)

int timeout = 0;
int retransmissions = 0;
LinkLayerRole role;

int alarmTriggered = FALSE;
int alarmCount = 0;

unsigned char tramaTx = 0;
unsigned char tramaRx = 1;

typedef enum {
    START,
    FLAG_RCV,
    A_RCV,
    C_RCV,
    BCC_OK,
    STOP
} LinkLayerState;

void alarmHandler(int signal) {
    alarmTriggered = TRUE;
    alarmCount++;
}

// Send supervision frame (SET | DISC) - Transmitter
int writeTX(int controlField) {
    unsigned char supervision[5] = {FLAG, A_TX, controlField, A_TX^controlField, FLAG};
    // Write frame
    if (writeBytesSerialPort(supervision, 5) < 0) {
        printf("ERROR: Failed to write frame\n");
        return -1;
    }
    return 0;
}


// Send supervision frame (DISC | UA | RR | REJ) - Receiver
int writeRX(int controlField) {
    unsigned char supervision[5] = {FLAG, A_RX, controlField, A_RX^controlField, FLAG};
    if (writeBytesSerialPort(supervision, 5) < 0) {
        printf("ERROR: Failed to write frame\n");
        return -1;
    }
    return 0;
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters) {
    if (connectionParameters.serialPort == NULL || connectionParameters.baudRate <= 0) {
        printf("ERROR: Invalid connection parameters\n");
        return -1;
    }
    // Open serial port
    int fd = openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate);
    if (fd < 0) {
        printf("ERROR: Failed to open serial port\n");
        return -1;
    }

    // Set connection parameters
    timeout = connectionParameters.timeout;
    retransmissions = connectionParameters.nRetransmissions;
    role = connectionParameters.role;

    LinkLayerState state = START;
    unsigned char byte;

    // Establish connection
    switch (connectionParameters.role) {
        case LlTx: // Transmitter     
            // Install alarm handler
            (void) signal(SIGALRM, alarmHandler);
            while (retransmissions && state != STOP) {
                // Set alarm timeout
                alarm(timeout);
                alarmTriggered = FALSE;
                // Send SET
                if (writeTX(C_SET) < 0) {
                    printf("ERROR: Failed to send SET\n");
                    return -1;
                }
                // Wait for UA
                while (state != STOP && alarmTriggered == FALSE) {
                    if (readByteSerialPort(&byte) < 0) {
                        printf("ERROR: Failed to read byte from serial port\n");
                        return -1;
                    }
                    switch (state) {
                        case START:
                            if (byte == FLAG) state = FLAG_RCV;
                            break;
                        case FLAG_RCV:
                            if (byte == A_RX) state = A_RCV;
                            else if (byte != FLAG) state = START;
                            break;
                        case A_RCV:
                            if (byte == C_UA) state = C_RCV;
                            else if (byte == FLAG) state = FLAG_RCV;
                            else state = START;
                            break;
                        case C_RCV:
                            if (byte == A_RX^C_UA) state = BCC_OK;
                            else if (byte == FLAG) state = FLAG_RCV;
                            else state = START;
                            break;
                        case BCC_OK:
                            if (byte == FLAG) state = STOP;
                            else state = START;
                            break;
                        default:
                            break;
                    }
                }
                // If we didn't received UA, decrement retransmissions
                if (state != STOP) {
                    retransmissions--;
                }
            }
            // If we didn't reached the STOP state (didn't received UA), we failed to establish connection
            if (state != STOP) {
                printf("ERROR: Failed to establish connection\n");
                return -1;
            }
            alarm(0);
            break;
        case LlRx: // Receiver
            // Wait for SET
            while (state != STOP) {
                if (readByteSerialPort(&byte) < 0) {
                    printf("ERROR: Failed to read byte from serial port\n");
                    return -1;
                }
                switch (state) {
                    case START:
                        if (byte == FLAG) state = FLAG_RCV;
                        break;
                    case FLAG_RCV:
                        if (byte == A_TX) state = A_RCV;
                        else if (byte != FLAG) state = START;
                        break;
                    case A_RCV:
                        if (byte == C_SET) state = C_RCV;
                        else if (byte == FLAG) state = FLAG_RCV;
                        else state = START;
                        break;
                    case C_RCV:
                        if (byte == A_TX^C_SET) state = BCC_OK;
                        else if (byte == FLAG) state = FLAG_RCV;
                        else state = START;
                        break;
                    case BCC_OK:
                        if (byte == FLAG) state = STOP;
                        else state = START;
                        break;
                    default:
                        break;
                }
            }
            // If everything went well, send UA
            if (writeRX(C_UA) < 0) {
                printf("ERROR: Failed to send UA\n");
                return -1;
            }

            break;
        default:
            return -1;
    }
    
    return fd;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize) {
    // Check if buffer is empty
    if (buf == NULL || bufSize <= 0 || bufSize > MAX_PAYLOAD_SIZE) {
        printf("ERROR: Invalid buffer\n");
        return -1;
    }

    // Create frame
    int frameSize = 6+bufSize;
    unsigned char *frame = (unsigned char *) malloc(frameSize);
    frame[0] = FLAG;
    frame[1] = A_TX; 
    frame[2] = C_N(tramaTx); // PLACE HOLDER FOR CONTROL FIELD
    frame[3] = A_TX ^ frame[2]; // BCC1 WITH PLACE HOLDER CONTROL FIELD
    memcpy(frame+4, buf, bufSize); // Copy buffer to frame
    unsigned char BCC2 = buf[0];
    for (unsigned int i = 1 ; i < bufSize ; i++) BCC2 ^= buf[i]; // Calculate BCC2
    int written = 4;

    // Copy buffer to frame
    for (int i = 0; i < bufSize; i++) { // Stuffing
        if (buf[i] == FLAG || buf[i] == ESCAPE) {
            if (written + 2 > MAX_PAYLOAD_SIZE) { // Check if frame buffer is full
                printf("ERROR: Frame buffer overflow\n");
                return -1;
            }
            frame[written++] = ESCAPE; // Current byte is replaced by ESCAPE
            frame[written++] = buf[i] ^ STUFF; // Next byte XORed with STUFF
        }
        else {
            if (written + 1 > MAX_PAYLOAD_SIZE) {
                printf("ERROR: Frame buffer overflow\n");
                return -1;
            }
            frame[written++] = buf[i];
        }
    }
    frame[written++] = BCC2; // BCC2
    frame[written++] = FLAG; // End of frame

    // Write frame
    int currentTransmition = retransmissions;
    int rejected = 0, accepted = 0;
    unsigned char byte;

    while (currentTransmition) { 
        alarmTriggered = FALSE;
        alarm(timeout);
        rejected = FALSE;
        accepted = FALSE;

        while (alarmTriggered == FALSE && !rejected && !accepted) {

            // Send frame
            if (writeBytesSerialPort(frame, frameSize) < 0) {
                printf("ERROR: Failed to write frame\n");
                return -1;
            }

            // Wait for response
            unsigned char result = readByteSerialPort(&byte);
            
            // Check if the response is valid and if it's a RR or REJ
            if(!result) {
                continue;
            }

            // Accepted
            else if (result == C_RR0 || result == C_RR1) {
                accepted = TRUE;
                tramaTx = (tramaTx+1) % 2;
            }

            // Rejected
            else if (result == C_REJ0 || result == C_REJ1) {
                rejected = TRUE;
            }
            else continue;

        }
        if (accepted) break;
        currentTransmition--;
    }
    
    free(frame);
    if (accepted) {
        printf("Frame sent successfully\n");
        return bufSize;
    }
    else {
        printf("ERROR: Failed to send frame\n");
        return -1;
    }
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    // Check if buffer is empty
    if (packet == NULL) {
        printf("ERROR: Invalid buffer\n");
        return -1;
    }
    unsigned char byte;
    LinkLayerState state = START;
    unsigned char *frame = (unsigned char *) malloc(MAX_PAYLOAD_SIZE);
    int frameSize = 0;
    int accepted = FALSE;
    int rejected = FALSE;
    int currentTransmition = retransmissions;
    while (currentTransmition && !accepted) {
        alarmTriggered = FALSE;
        alarm(timeout);
        rejected = FALSE;
        accepted = FALSE;
        while (alarmTriggered == FALSE && !rejected && !accepted) {
            if (readByteSerialPort(&byte) < 0) {
                printf("ERROR: Failed to read byte from serial port\n");
                return -1;
            }
            switch (state) {
                case START:
                    if (byte == FLAG) state = FLAG_RCV;
                    break;
                case FLAG_RCV:
                    if (byte == A_TX) state = A_RCV;
                    else if (byte != FLAG) state = START;
                    break;
                case A_RCV:
                    if (byte == 0x00 || byte == 0x01) {
                        frame[0] = byte;
                        state = C_RCV;
                    }
                    else if (byte == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;
                case C_RCV:
                    if (byte == A_TX^frame[0]) state = BCC_OK;
                    else if (byte == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;
                case BCC_OK:
                    if (byte == FLAG) {
                        frameSize--;
                        accepted = TRUE;
                    }
                    else if (byte == ESCAPE) state = ESCAPE;
                    else {
                        frame[frameSize++] = byte;
                    }
                    break;
                case ESCAPE:
                    frame[frameSize++] = byte ^ STUFF;
                    state = BCC_OK;
                    break;
                default:
                    break;
            }
        }
        if (accepted) break;
        currentTransmition--;
    }
    if (accepted) {
        for (int i = 0; i < frameSize; i++) {
            packet[i] = frame[i];
        }
        free(frame);
        return frameSize;
    }
    else {
        free(frame);
        return -1;
    }
    return frameSize;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics) {
    unsigned char byte;
    LinkLayerState state = START;
    int currentTransmition = retransmissions;
    switch (role) {
        case LlRx:
            // Wait for DISC
            while (state != STOP) {
                if (readByteSerialPort(&byte) < 0) {
                    printf("ERROR: Failed to read byte from serial port\n");
                    return -1;
                }
                switch (state) {
                    case START:
                        if (byte == FLAG) state = FLAG_RCV;
                        break;
                    case FLAG_RCV:
                        if (byte == A_TX) state = A_RCV;
                        else if (byte != FLAG) state = START;
                        break;
                    case A_RCV:
                        if (byte == DISC) state = C_RCV;
                        else if (byte == FLAG) state = FLAG_RCV;
                        else state = START;
                        break;
                    case C_RCV:
                        if (byte == A_TX^DISC) state = BCC_OK;
                        else if (byte == FLAG) state = FLAG_RCV;
                        else state = START;
                        break;
                    case BCC_OK:
                        if (byte == FLAG) state = STOP;
                        else state = START;
                        break;
                    default:
                        break;
                }
            }
            // Send DISC
            if (writeRX(DISC) < 0) {
                printf("ERROR: Failed to send DISC\n");
                return -1;
            }
            break;
        case LlTx:
            // Send DISC
            while (currentTransmition && state != STOP) {
                alarmTriggered = FALSE;
                alarm(timeout);
                while (alarmTriggered == FALSE) {
                    if (writeTX(DISC) < 0) {
                        printf("ERROR: Failed to send DISC\n");
                        return -1;
                    }
                    if (readByteSerialPort(&byte) < 0) {
                        printf("ERROR: Failed to read byte from serial port\n");
                        return -1;
                    }
                    switch (state) {
                        case START:
                            if (byte == FLAG) state = FLAG_RCV;
                            break;
                        case FLAG_RCV:
                            if (byte == A_RX) state = A_RCV;
                            else if (byte != FLAG) state = START;
                            break;
                        case A_RCV:
                            if (byte == DISC) state = C_RCV;
                            else if (byte == FLAG) state = FLAG_RCV;
                            else state = START;
                            break;
                        case C_RCV:
                            if (byte == A_RX^DISC) state = BCC_OK;
                            else if (byte == FLAG) state = FLAG_RCV;
                            else state = START;
                            break;
                        case BCC_OK:
                            if (byte == FLAG) state = STOP;
                            else state = START;
                            break;
                        default:
                            break;
                    }
                }
                if (state != STOP) {
                    currentTransmition--;
                }
            }
            alarm(0);
            break;
    }
    int clstat = closeSerialPort();
    if (clstat < 0) {
        printf("ERROR: Failed to close serial port\n");
        return -1;
    }
    return clstat;
}
