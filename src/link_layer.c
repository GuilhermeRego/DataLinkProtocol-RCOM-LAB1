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
    STOP_RCV,
    FOUND_ESCAPE,
    DATA_RCV
} LinkLayerState;

void alarmHandler(int signal) {
    alarmTriggered = TRUE;
    alarmCount++;
    printf("Alarm #%d\n", alarmCount);
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

unsigned char readControlFrame() {
    LinkLayerState state = START;
    unsigned char byte;
    unsigned char control;
    while (state != STOP_RCV) {
        if (readByteSerialPort(&byte) > 0) {
            switch (state) {
                case START:
                    if (byte == FLAG) state = FLAG_RCV;
                    break;
                case FLAG_RCV:
                    if (byte == A_RX) state = A_RCV;
                    else if (byte != FLAG) state = START;
                    break;
                case A_RCV:
                    if (byte == C_RR0 || byte == C_RR1 || byte == C_REJ0 || byte == C_REJ1) {
                        state = C_RCV;
                        control = byte;
                    }
                    else if (byte == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;
                case C_RCV:
                    if (byte == A_RX^(byte)) state = BCC_OK;
                    else if (byte == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;
                case BCC_OK:
                    if (byte == FLAG) state = STOP_RCV;
                    else state = START;
                    break;
                case STOP_RCV:
                    break;
                default:
                    return -1;
            }
        }
    }
    return control;
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters) {
    if (connectionParameters.serialPort == NULL || connectionParameters.baudRate <= 0) {
        printf("ERROR: Invalid connection parameters\n");
        return -1;
    }

    //printf("Opening serial port...\n");

    // Open serial port
    int fd = openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate);
    if (fd < 0) {
        printf("ERROR: Failed to open serial port\n");
        return -1;
    }
    //printf("Serial port opened successfully\n");

    // Set connection parameters
    timeout = connectionParameters.timeout;
    retransmissions = connectionParameters.nRetransmissions;
    role = connectionParameters.role;

    LinkLayerState state = START;
    unsigned char byte;
    int currentTransmition = retransmissions;

    // Establish connection
    switch (connectionParameters.role) {
        case LlTx: { // Transmitter     
            // Install alarm handler
            (void) signal(SIGALRM, alarmHandler);
            while (currentTransmition && state != STOP_RCV) {
                //printf("Trying to establish connection...\n");
                // Send SET
                if (writeTX(C_SET) < 0) {
                    printf("ERROR: Failed to send SET\n");
                    return -1;
                }
                // Set alarm 
                //printf("Alarm #%d\n", alarmCount);
                alarm(connectionParameters.timeout);
                alarmTriggered = FALSE;
                //printf("SET sent: %x\n", C_SET);
                // Wait for UA
                while (state != STOP_RCV && alarmTriggered == FALSE) {
                    //printf("Waiting for byte...\n");
                    if (readByteSerialPort(&byte) > 0) { 
                        //printf("Received byte: %x\n", byte);
                        switch (state) {
                            case START:
                                if (byte == FLAG) {
                                    state = FLAG_RCV;
                                    //printf("FLAG OK!\n");
                                }
                                break;

                            case FLAG_RCV:
                                if (byte == A_RX) {
                                    state = A_RCV;
                                    //printf("A OK!\n");
                                }
                                else if (byte != FLAG) {
                                    state = START;
                                    //printf("NOT SUPPOSED TO BE FLAG!\n");
                                }
                                break;

                            case A_RCV:
                                if (byte == C_UA) {
                                    state = C_RCV;
                                    //printf("C OK!\n");
                                }
                                else if (byte == FLAG) {
                                    state = FLAG_RCV;
                                    //printf("FLAG OK!\n");
                                }
                                else {
                                    state = START;
                                    //printf("NOT SUPPOSED TO BE FLAG!\n");
                                }
                                break;

                            case C_RCV:
                                if (byte == A_RX^C_UA) {
                                    state = BCC_OK;
                                    //printf("BCC OK!\n");
                                }
                                else if (byte == FLAG) {
                                    state = FLAG_RCV;
                                    //printf("FLAG OK!\n");
                                }
                                else {
                                    state = START;
                                    //printf("NOT SUPPOSED TO BE FLAG!\n");
                                }
                                break;

                            case BCC_OK:
                                if (byte == FLAG) {
                                    state = STOP_RCV;
                                    //printf("STOP OK!\n");
                                }
                                else {
                                    state = START;
                                    //printf("NOT SUPPOSED TO BE FLAG!\n");
                                }
                                break;
                            
                            default:
                                break;
                        }
                    }
                }
                currentTransmition--;
            }
            // If we didn't reached the STOP state (didn't received UA), we failed to establish connection
            if (state != STOP_RCV) {
                printf("ERROR: Failed to establish connection\n");
                return -1;
            }
            alarm(0);
            break;
        }

        case LlRx: { // Receiver
            // Wait for SET
            while (state != STOP_RCV) {
                //printf("Waiting for byte...\n");
                if (readByteSerialPort(&byte) > 0) {
                    //printf("Received byte: %x\n", byte);
                    switch (state) {
                        case START:
                            if (byte == FLAG) {
                                state = FLAG_RCV;
                                //printf("FLAG OK!\n");
                            }
                            break;

                        case FLAG_RCV:
                            if (byte == A_TX) {
                                state = A_RCV;
                                //printf("A OK!\n");
                            }
                            else if (byte != FLAG) {
                                state = START;
                                //printf("NOT SUPPOSED TO BE FLAG!\n");
                            }
                            break;

                        case A_RCV:
                            if (byte == C_SET) {
                                state = C_RCV;
                                //printf("C OK!\n");
                            }
                            else if (byte == FLAG) {
                                state = FLAG_RCV;
                                //printf("FLAG OK!\n");
                            }
                            else {
                                state = START;
                                //printf("NOT SUPPOSED TO BE FLAG!\n");
                            }
                            break;

                        case C_RCV:
                            if (byte == A_TX^C_SET) {
                                state = BCC_OK;
                                //printf("BCC OK!\n");
                            }
                            else if (byte == FLAG) {
                                state = FLAG_RCV;
                                //printf("FLAG OK!\n");
                            }
                            else {
                                state = START;
                                //printf("NOT SUPPOSED TO BE FLAG!\n");
                            }
                            break;

                        case BCC_OK:
                            if (byte == FLAG) {
                                state = STOP_RCV;
                                //printf("STOP OK!\n");
                            }
                            else {
                                state = START;
                                //printf("NOT SUPPOSED TO BE FLAG!\n");
                            }
                            break;
                        
                        default:
                            break;
                    }
                }
            }
            // If everything went well, send UA
            if (writeRX(C_UA) < 0) {
                printf("ERROR: Failed to send UA\n");
                return -1;
            }

            break;
        }
        default:
            return -1;
    }
    alarmCount = 0;
    alarmTriggered = FALSE;
    return fd;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize) {
    // Check if buffer is empty
    if (buf == NULL || bufSize <= 0) {
        printf("ERROR: Invalid buffer\n");
        return -1;
    }

    // Create frame
    int frameSize = 6+bufSize*2;
    unsigned char *frame = (unsigned char *) malloc(frameSize);
    frame[0] = FLAG;
    frame[1] = A_TX; 
    frame[2] = C_N(tramaTx); // PLACE HOLDER FOR CONTROL FIELD
    frame[3] = A_TX ^ C_N(tramaTx); // BCC1 WITH PLACE HOLDER CONTROL FIELD
    memcpy(frame+4, buf, bufSize); // Copy buffer to frame
    unsigned char BCC2 = buf[0];
    for (unsigned int i = 1 ; i < bufSize ; i++)
        BCC2 ^= buf[i]; // Calculate BCC2
    int written = 4;

    for (int i = 0; i < bufSize; i++) { // Stuffing
        if (buf[i] == FLAG || buf[i] == ESCAPE) {
            frame = (unsigned char *) realloc(frame, frameSize);
            frame[written++] = ESCAPE; // Current byte is replaced by ESCAPE
        }
        frame[written++] = buf[i];
    }
    frame[written++] = BCC2; // BCC2
    frame[written++] = FLAG; // End of frame


    // Write frame
    int currentTransmition = retransmissions;
    int rejected;
    int accepted;
    unsigned char byte;

    while (currentTransmition) { 
        alarm(timeout);
        alarmTriggered = FALSE;
        rejected = FALSE;
        accepted = FALSE;

        while (alarmTriggered == FALSE && !rejected && !accepted) {

            // Send frame
            if (writeBytesSerialPort(frame, frameSize) < 0) {
                printf("ERROR: Failed to write frame\n");
                return -1;
            }

            // Wait for response
            byte = readControlFrame();
            //printf("Received byte: %x\n", byte);
                
            // Accepted
            if (byte == C_RR0 || byte == C_RR1) {
                //printf("Frame accepted\n");
                accepted = TRUE;
                tramaTx = (tramaTx+1) % 2;
            }

            // Rejected
            else if (byte == C_REJ0 || byte == C_REJ1) {
                //printf("Frame rejected\n");
                rejected = TRUE;
            }
            else continue;
        }
        if (accepted) break;
        currentTransmition--;
    }
    
    free(frame);
    if (accepted) {
        //printf("Frame sent successfully\n");
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
    unsigned char control;
    LinkLayerState state = START;
    int i = 0;
    while (state != STOP_RCV) {
        if (readByteSerialPort(&byte) > 0) {
            switch (state) {
                case START:
                    if (byte == FLAG) {
                        state = FLAG_RCV;
                    }
                    break;

                case FLAG_RCV:
                    if (byte == A_TX) {
                        state = A_RCV;
                    }
                    else if (byte != FLAG) {
                        state = START;
                    }
                    break;

                case A_RCV:
                    if (byte == C_N(tramaRx) || byte == C_N(tramaTx)) {
                        state = C_RCV;
                        control = byte;
                    }
                    else if (byte == FLAG) {
                        state = FLAG_RCV;
                    }
                    else if (byte == DISC) {
                        writeRX(DISC);
                        return 0;
                    }
                    else{
                        state = START;
                    }
                    break;

                case C_RCV:
                    if (byte == control^A_TX) {
                        state = DATA_RCV;
                    }
                    else if (byte == FLAG) {
                        state = FLAG_RCV;
                    }
                    else {
                        state = START;
                    }
                    break;
                
                case FOUND_ESCAPE: 
                    if (byte == ESCAPE || byte == FLAG) {
                         state = DATA_RCV;
                         packet[i++] = byte;
                    }
                    else {
                        state = DATA_RCV;
                        packet[i++] = ESCAPE;
                        packet[i++] = byte;
                    }
                    break;

                case DATA_RCV:
                    if (byte == FLAG) {
                        unsigned char BCC2 = packet[--i];
                        packet[i] = '\0';
                        unsigned char BCC2_calc = packet[0];
                        for (int j = 1; j < i; j++) BCC2_calc ^= packet[j];
                        if (BCC2 == BCC2_calc) {
                            if (control == C_N(tramaRx))
                                writeRX(C_RR0);
                            else
                                writeRX(C_RR1);
                            return i;
                        }
                        else {
                            if (control == C_N(tramaRx))
                                writeRX(C_REJ0);
                            else
                                writeRX(C_REJ1);
                            return -1;
                        }
                    }
                    else if (byte == ESCAPE) {
                        state = FOUND_ESCAPE;
                    }
                    else {
                        packet[i++] = byte;
                    }
                    break;
                default:
                    break;
            }
        }
    }
    return -1;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics) {
    unsigned char byte;
    LinkLayerState state = START;
    int currentTransmition = retransmissions;
    switch (role) {
        case LlTx: { // Transmitter     
            // Install alarm handler
            (void) signal(SIGALRM, alarmHandler);
            while (currentTransmition && state != STOP_RCV) {
                //printf("Trying to establish connection...\n");
                // Send DISC
                if (writeTX(DISC) < 0) {
                    printf("ERROR: Failed to send DISC\n");
                    return -1;
                }
                // Set alarm 
                //printf("Alarm #%d\n", alarmCount);
                alarm(timeout);
                alarmTriggered = FALSE;
                //printf("DISC sent: %x\n", DISC);
                // Wait for DISC
                while (state != STOP_RCV && alarmTriggered == FALSE) {
                    //printf("Waiting for byte...\n");
                    if (readByteSerialPort(&byte) > 0) { 
                        //printf("Received byte: %x\n", byte);
                        switch (state) {
                            case START:
                                if (byte == FLAG) {
                                    state = FLAG_RCV;
                                    //printf("FLAG OK!\n");
                                }
                                break;

                            case FLAG_RCV:
                                if (byte == A_RX) {
                                    state = A_RCV;
                                    //printf("A OK!\n");
                                }
                                else if (byte != FLAG) {
                                    state = START;
                                    //printf("NOT SUPPOSED TO BE FLAG!\n");
                                }
                                break;

                            case A_RCV:
                                if (byte == DISC) {
                                    state = C_RCV;
                                    //printf("C OK!\n");
                                }
                                else if (byte == FLAG) {
                                    state = FLAG_RCV;
                                    //printf("FLAG OK!\n");
                                }
                                else {
                                    state = START;
                                    //printf("NOT SUPPOSED TO BE FLAG!\n");
                                }
                                break;

                            case C_RCV:
                                if (byte == A_RX^DISC) {
                                    state = BCC_OK;
                                    //printf("BCC OK!\n");
                                }
                                else if (byte == FLAG) {
                                    state = FLAG_RCV;
                                    //printf("FLAG OK!\n");
                                }
                                else {
                                    state = START;
                                    //printf("NOT SUPPOSED TO BE FLAG!\n");
                                }
                                break;

                            case BCC_OK:
                                if (byte == FLAG) {
                                    state = STOP_RCV;
                                    //printf("STOP OK!\n");
                                }
                                else {
                                    state = START;
                                    //printf("NOT SUPPOSED TO BE FLAG!\n");
                                }
                                break;
                            
                            default:
                                break;
                        }
                    }
                }
                currentTransmition--;
            }
            // If we didn't reached the STOP state (didn't received UA), we failed to establish connection
            if (state != STOP_RCV) {
                printf("ERROR: Failed to establish connection\n");
                return -1;
            }
            alarm(0);
            break;
        }

        case LlRx: { // Receiver
            // Wait for DISC
            while (state != STOP_RCV) {
                //printf("Waiting for byte...\n");
                if (readByteSerialPort(&byte) > 0) {
                    //printf("Received byte: %x\n", byte);
                    switch (state) {
                        case START:
                            if (byte == FLAG) {
                                state = FLAG_RCV;
                                //printf("FLAG OK!\n");
                            }
                            break;

                        case FLAG_RCV:
                            if (byte == A_TX) {
                                state = A_RCV;
                                //printf("A OK!\n");
                            }
                            else if (byte != FLAG) {
                                state = START;
                                //printf("NOT SUPPOSED TO BE FLAG!\n");
                            }
                            break;

                        case A_RCV:
                            if (byte == DISC) {
                                state = C_RCV;
                                //printf("C OK!\n");
                            }
                            else if (byte == FLAG) {
                                state = FLAG_RCV;
                                //printf("FLAG OK!\n");
                            }
                            else {
                                state = START;
                                //printf("NOT SUPPOSED TO BE FLAG!\n");
                            }
                            break;

                        case C_RCV:
                            if (byte == A_TX^DISC) {
                                state = BCC_OK;
                                //printf("BCC OK!\n");
                            }
                            else if (byte == FLAG) {
                                state = FLAG_RCV;
                                //printf("FLAG OK!\n");
                            }
                            else {
                                state = START;
                                //printf("NOT SUPPOSED TO BE FLAG!\n");
                            }
                            break;

                        case BCC_OK:
                            if (byte == FLAG) {
                                state = STOP_RCV;
                                //printf("STOP OK!\n");
                            }
                            else {
                                state = START;
                                //printf("NOT SUPPOSED TO BE FLAG!\n");
                            }
                            break;
                        
                        default:
                            break;
                    }
                }
            }
            // If everything went well, send DISC
            if (writeRX(DISC) < 0) {
                printf("ERROR: Failed to send DISC\n");
                return -1;
            }

            break;
        }

        default:
            return -1;
    }
    int clstat = closeSerialPort();
    if (clstat < 0) {
        printf("ERROR: Failed to close serial port\n");
        return -1;
    }
    return clstat;
}
