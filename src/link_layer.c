// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"
#include "signal.h"
#include <stdio.h>

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source
#define ESCAPE 0x7D
#define STUFF 0x20

// Receiver's response control fields to inform transmitter of the received frame
#define C_RR(N) (0x05 | (N << 7))
#define C_REJ(N) (0x01 | (N << 7))

#define FLAG 0x7E 
#define A_TX 0x03 // Transmitter
#define A_RX 0x01 // Receiver

// Supervision control fields
#define C_SET 0x03 // Transmitter
#define C_UA 0x07 // Receiver

int timeout = 0;
int retransmissions = 0;

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
    LinkLayerState state = START;

    // Establish connection
    switch (connectionParameters.role) {
        case LlTx: // Transmitter     
            // Install alarm handler
            (void) signal(SIGALRM, alarmHandler);
            unsigned char byte;
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
            break;
        case LlRx: // Receiver
            // Wait for SET
            unsigned char byte;
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
    frame[2] = 0x00; // PLACE HOLDER FOR CONTROL FIELD
    frame[3] = A_TX ^ frame[2]; // BCC1 WITH PLACE HOLDER CONTROL FIELD
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
            unsigned char result = readByteSerialPort(byte);
            
            // Check if the response is valid and if it's a RR or REJ
            if(!result) {
                continue;
            }

            // Accepted
            else if (result == C_RR(0) || result == C_RR(1)) {
                accepted = TRUE;
                tramaTx = (tramaTx+1) % 2;
            }

            // Rejected
            else if (result == C_REJ(0) || result == C_REJ(1)) {
                rejected = TRUE;
            }
            else continue;

        }
        if (accepted) break;
        currentTransmition--;
    }
    
    free(frame);
    accepted ? printf("Frame accepted\n") : printf("Frame rejected\n");
    if (accepted) return written;
    else{
        llclose(0); // CLOSE CONNECTION WITH PLACEHOLDER
        return -1;
    }
    return written;
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
int llclose(int showStatistics) {
    int clstat = closeSerialPort();
    return clstat;
}
