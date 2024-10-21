// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source
#define ESCAPE 0x7D
#define STUFF 0x20

#define FLAG 0x7E 
#define A_TX 0x03 // Transmitter
#define A_RX 0x01 // Receiver
#define C_SET 0x03 // Transmitter
#define C_UA 0x07 // Receiver

#define C_REJ(N) (0x01 | (N << 7)) // Reject
#define C_RR(N) (0x05 | (N << 7)) // Receive Ready

int timeout = 0;
retransmissions = 0;

int alarmTriggered = FALSE;
int alarmCount = 0;

unsigned char trama = 0;

unsigned char controlFrameTX[5] = {FLAG, A_TX, C_SET, A_TX^C_SET, FLAG};
unsigned char controlFrameRX[5] = {FLAG, A_RX, C_UA, A_RX^C_UA, FLAG};

void alarmHandler(int signal) {
    alarmTriggered = TRUE;
    alarmCount++;
}

typedef enum {
    START,
    FLAG_RCV,
    A_RCV,
    C_RCV,
    BCC_OK,
    STOP
} LinkLayerState;

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters) {
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
            (void) signal(SIGALRM, alarmHandler); // Install alarm handler
            // While we haven't received the frames and we still have retransmissions
            while (state != STOP && retransmissions) {
                if (writeBytesSerialPort(controlFrameTX, 5) < 0) { // Write control frame
                    printf("ERROR: Failed to write control frame\n");
                    return -1;
                }
                alarm(timeout);
                alarmTriggered = FALSE;

                // While we haven't received the frames and the alarm hasn't been triggered
                while (state != STOP && !alarmTriggered) {
                    // Read byte from serial port
                    unsigned char byte;
                    if (readByteSerialPort(&byte) < 0) {
                        printf("ERROR: Failed to read byte from serial port\n");
                        return -1;
                    }
                    else {
                        // Check byte
                        switch (state) {
                            case START:
                                if (byte == FLAG) {
                                    state = FLAG_RCV;
                                }
                                break;
                            case FLAG_RCV:
                                if (byte == A_RX) {
                                    state = A_RCV;
                                } else if (byte != FLAG) {
                                    state = START;
                                }
                                break;
                            case A_RCV:
                                if (byte == C_UA) {
                                    state = C_RCV;
                                } else if (byte == FLAG) {
                                    state = FLAG_RCV;
                                } else {
                                    state = START;
                                }
                                break;
                            case C_RCV:
                                if (byte == (A_RX^C_UA)) {
                                    state = BCC_OK;
                                } else if (byte == FLAG) {
                                    state = FLAG_RCV;
                                } else {
                                    state = START;
                                }
                                break;
                            case BCC_OK:
                                if (byte == FLAG) {
                                    state = STOP;
                                } else {
                                    state = START;
                                }
                                break;
                            default:
                                break;
                        }
                    }
                }
                // If we haven't received the wished frames, decrement retransmissions
                retransmissions--;
            }
            if (state != STOP) {
                printf("ERROR: Failed to establish connection\n");
                return -1;
            }
            break;
        case LlRx:
            while (state != STOP) {
                // Read byte from serial port
                unsigned char byte;
                if (readByteSerialPort(&byte) < 0) {
                    printf("ERROR: Failed to read byte from serial port\n");
                    return -1;
                }
                else {
                    switch (state) {
                        case START:
                            if (byte == FLAG) {
                                state = FLAG_RCV;
                            }
                            break;
                        case FLAG_RCV:
                            if (byte == A_TX) {
                                state = A_RCV;
                            } else if (byte != FLAG) {
                                state = START;
                            }
                            break;
                        case A_RCV:
                            if (byte == C_SET) {
                                state = C_RCV;
                            } else if (byte == FLAG) {
                                state = FLAG_RCV;
                            } else {
                                state = START;
                            }
                            break;
                        case C_RCV:
                            if (byte == (A_TX^C_SET)) {
                                state = BCC_OK;
                            } else if (byte == FLAG) {
                                state = FLAG_RCV;
                            } else {
                                state = START;
                            }
                            break;
                        case BCC_OK:
                            if (byte == FLAG) {
                                state = STOP;
                            } else {
                                state = START;
                            }
                            break;
                        default:
                            break;
                    }
                }
            }
            // Write control frame
            if (writeBytesSerialPort(controlFrameRX, 5) < 0) {
                printf("ERROR: Failed to write control frame\n");
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
    // Frame format: FLAG A C BCC1 D1 D2 ... DN BCC2 FLAG
    int frameSize = bufSize + 6;
    unsigned char *frame = (unsigned char *) malloc(frameSize);
    frame[0] = FLAG;
    frame[1] = A_TX;
    frame[2] = trama << 6;
    frame[3] = A_TX^0x00;
    memcpy(frame+4, buf, bufSize);

    // Calculate BCC2
    unsigned char BCC2 = buf[0];
    for (int i = 1; i < bufSize; i++) {
        BCC2 ^= buf[i];
    }

    // Stuffing
    int j = 4;
    for (int i = 0; i < bufSize; i++) {
        if (buf[i] == FLAG || buf[i] == ESCAPE) {
            frame[j++] = ESCAPE;
            frame[j++] = buf[i]^STUFF;
        } else {
            frame[j++] = buf[i];
        }
    }
    frame[j++] = BCC2;
    frame[j++] = FLAG;

    // Send frame
    int current = 0;
    int accepted = FALSE;
    while (current < retransmissions && !accepted) {
        alarm(timeout);
        alarmTriggered = FALSE;
        accepted = FALSE;

        // While we haven't received the wished frames and the alarm hasn't been triggered
        while (!alarmTriggered && !accepted) {
            // Write frame
            if (writeBytesSerialPort(frame, frameSize) < 0) {
            printf("ERROR: Failed to write frame\n");
            return -1;
            }
            // Read byte from serial port
            unsigned char byte;
            if (readByteSerialPort(&byte) < 0) {
                printf("ERROR: Failed to read byte from serial port\n");
                return -1;
            }
            // Check result
            else {
                switch (byte) {
                    case C_RR(0): case C_RR(1):
                        accepted = TRUE;
                        break;
                    case C_REJ(0): case C_REJ(1):
                        break;
                    default:
                        break;
                }
            }
        }
        if (!accepted) current++;
        else break;
    }

    if (accepted) return bufSize;
    return -1;
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
