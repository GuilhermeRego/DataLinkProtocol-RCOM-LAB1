// Link layer protocol implementation

#include "link_layer.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

// State machine states
typedef enum {
    START,
    FLAG_RCV,
    A_RCV,
    C_RCV,
    BCC_OK,
    DATA_RCV,
    FOUND_DATA,
    STOP
} LinkLayerState;

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

// Frame delimiters: Page 10 of the protocol
#define FLAG 0x7E

// Address field: Pages 10 and 11 of the protocol
#define A_TX 0x03
#define A_RX 0x01

// Control field for Supervision and Unnumbered frames: Page 10 of the protocol
#define C_SET 0x03      // Set up connection: Tx
#define C_UA 0x07       // Unnumbered acknowledgment: Rx
#define C_RR0 0xAA      // Receiver ready 0: Rx
#define C_RR1 0xAB      // Receiver ready 1: Rx
#define C_REJ0 0x54     // Reject 0: Rx
#define C_REJ1 0x55     // Reject 1: Rx
#define C_DISC 0x0B     // Disconnect: Tx | Rx

// Control field for Information frames: Page 11 of the protocol
#define C_N0 0x00       // Information frame control field (frame 0)
#define C_N1 0x80       // Information frame control field (frame 1)
#define TX 0            // Transmitter
#define RX 1            // Receiver

// Byte stuffing: Page 17 of the protocol
#define ESCAPE 0x7D     // Escape character
#define STUFF 0x20      // XOR value for byte stuffing

// Global variables
int alarmTriggered = FALSE;
int alarmCount = 0;
int fd = 0;
int rejected = FALSE;

unsigned char tramaTx = 0;
unsigned char tramaRx = 0;

int framesSent, framesReceived = 0;
time_t startTime, startTimeConnection, endTime;


// Alarm function handler
void alarmHandler(int signal){
    alarmTriggered = TRUE;
    alarmCount++;
    printf("Alarm #%d\n", alarmCount);
}

int retransmissions;
int timeout;
LinkLayerRole role;

// Auxiliar function to make Tx receive the Rx's response (RR or REJ)
unsigned char readControlFrame() {
    unsigned char byte;
    unsigned char response;
    LinkLayerState state = START;
    int result;

    while(state != STOP){
        result = readByteSerialPort(&byte, 1);
        if (result > 0) {
            switch (state){
                case START: {
                    if (byte == FLAG) {
                        state = FLAG_RCV;
                    }
                    break;
                }

                case FLAG_RCV: {
                    if (byte == A_RX) {
                        state = A_RCV;
                    } else if (byte != FLAG) {
                        state = START;
                    }
                    break;
                }

                case A_RCV: {
                    if (byte == C_RR0 || byte == C_RR1 || byte == C_REJ0 || byte == C_REJ1 || byte == C_DISC) {
                        state = C_RCV;
                        response = byte;
                    } else if (byte == FLAG) {
                        state = FLAG_RCV;
                    } else {
                        state = START;
                    }
                    break;
                }

                case C_RCV: {
                    if (byte == (A_RX ^ response)) {
                        state = BCC_OK;
                    } else if (byte == FLAG) {
                        state = FLAG_RCV;
                    } else {
                        state = START;
                    }
                    break;
                }

                case BCC_OK: {
                    if (byte == FLAG) {
                        framesReceived++;
                        state = STOP;
                    } else {
                        state = START;
                    }
                    break;
                }

                case STOP: {
                    break;
                }

                default:
                    return -1;
            }
        }
    }

    return response;
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters){
    time(&startTime);

    // Open serial port
    fd = openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate);
    if (fd < 0) {
        return -1;
    }

    // Set global variables
    retransmissions = connectionParameters.nRetransmissions;
    timeout = connectionParameters.timeout;
    role = connectionParameters.role;

    // Establish connection
    int currentTransmission = retransmissions;
    LinkLayerState state = START;
    switch (role) {
        case LlTx: {
            // Set alarm handler
            (void) signal(SIGALRM, alarmHandler);
            alarmTriggered = FALSE;

            // Send SET frame
            unsigned char frame_set[5] = {FLAG, A_TX, C_SET, (A_TX ^ C_SET), FLAG};
            while (currentTransmission && state != STOP) {
                if (writeBytesSerialPort(frame_set, 5) < 0) {
                    perror("ERROR: Error on writing to serial port. (1)\n");
                }
                framesSent++;
                alarmTriggered = FALSE;
                alarm(timeout);
                while (!alarmTriggered && state != STOP) {
                    // Read UA frame
                    unsigned char byte;
                    int result;
                    result = readByteSerialPort(&byte, 1);
                    if (result > 0) {
                        switch (state) {
                            case START:
                                if (byte == FLAG) {
                                    state = FLAG_RCV;
                                }
                                break;

                            case FLAG_RCV:
                                if (byte == A_RX) {
                                    state = A_RCV;
                                }
                                else if (byte != FLAG) {
                                    state = START;
                                }
                                break;

                            case A_RCV:
                                if (byte == C_UA) {
                                    state = C_RCV;
                                }
                                else if (byte == FLAG) {
                                    state = FLAG_RCV;
                                }
                                else {
                                    state = START;
                                }
                                break;

                            case C_RCV:
                                if (byte == (C_UA ^ A_RX)) {
                                    state = BCC_OK;
                                }
                                else if (byte == FLAG) {
                                    state = FLAG_RCV;
                                }
                                else {
                                    state = START;
                                }
                                break;

                            case BCC_OK:
                                if (byte == FLAG) {
                                    state = STOP;
                                }
                                else {
                                    state = START;
                                }
                                break;

                            case STOP: {
                                framesReceived++;
                                break;
                            }

                            default:
                                break;
                        }
                    }
                }
                if (state == STOP) {
                    break;
                }
                currentTransmission--;
            }

            // Reached maximum number of retransmissions
            if (state != STOP) {
                return -1;
            }


            break;
        }

        case LlRx: {
            // Read SET frame
            unsigned char byte;
            int result;
            while (state != STOP) {
                result = readByteSerialPort(&byte, 1);
                if (result > 0) {
                    switch (state) {
                        case START: {
                            if (byte == FLAG) {
                                state = FLAG_RCV;
                            }
                            break;
                        }

                        case FLAG_RCV: {
                            if (byte == A_TX) {
                                state = A_RCV;
                            }
                            else if (byte != FLAG) {
                                state = START;
                            }
                            break;
                        }

                        case A_RCV: {
                            if (byte == C_SET) {
                                state = C_RCV;
                            }
                            else if (byte == FLAG) {
                                state = FLAG_RCV;
                            }
                            else {
                                state = START;
                            }
                            break;
                        }

                        case C_RCV: {
                            if (byte == (C_SET ^ A_TX)) {
                                state = BCC_OK;
                            }
                            else if (byte == FLAG) {
                                state = FLAG_RCV;
                            }
                            else {
                                state = START;
                            }
                            break;
                        }

                        case BCC_OK: {
                            if (byte == FLAG) {
                                framesReceived++;
                                state = STOP;
                            } else {
                                state = START;
                            }
                            break;
                        }

                        case STOP: {
                            break;
                        }

                        default:
                            break;
                    }
                }
            }


            unsigned char frame[5] = {FLAG, A_RX, C_UA, (A_RX ^ C_UA), FLAG};
            if (writeBytesSerialPort(frame, 5) < 0) {
                perror("ERROR: Error on writing to serial port. (2)\n");
            }
            break;
        }

        default:
            return -1;
    }

    time(&startTimeConnection); // Track time when connection was established and packet transfer started
    alarmCount = 0;
    return 1;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize) {
    // Frame structure: | FLAG | A | C | BCC1 | D1 | D2 | ... | DN | BCC2 | FLAG 
    // Initialize frame to write
    int frameSize = bufSize+6;
    unsigned char* frame = malloc(frameSize);

    // Construct frame:
    // Frame header
    frame[0] = FLAG;
    frame[1] = A_TX;
    frame[2] = (tramaTx % 2 == 0) ? C_N0 : C_N1;    // Sequence number
    frame[3] = (A_TX ^ frame[2]);                   // BCC1

    // Calculate BCC2
    unsigned char BCC2 = 0;
    for (int i = 0; i < bufSize; i++) {
        BCC2 ^= buf[i];
    }

    // Byte stuffing
    int frameCount = 4;
    for (int i = 0; i < bufSize; i++) {
        if(buf[i] == FLAG || buf[i] == ESCAPE){
            frame = realloc(frame, ++frameSize);
            frame[frameCount++] = ESCAPE;
            frame[frameCount++] = buf[i] ^ STUFF;
        }
        else{
            frame[frameCount++] = buf[i];
        }
    }

    // Finish information frame
    frame[frameCount++] = BCC2;
    frame[frameCount] = FLAG;

    // Send frame
    int currentTransmission = retransmissions;
    int accepted = FALSE;
    alarmTriggered = FALSE;
    alarmCount = 0;
    (void) signal(SIGALRM, alarmHandler);

    while (currentTransmission > 0 && !accepted && !rejected) {
        if (writeBytesSerialPort(frame, frameSize) < 0) {
            perror("ERROR: Error on writing to serial port. (3)\n");
        }
        framesSent++;
        alarmTriggered = FALSE;
        rejected = FALSE;
        alarm(timeout);

        while (!alarmTriggered && !accepted && !rejected) {
            unsigned char response;
            unsigned char byte;
            LinkLayerState state = START;
            int result;
            while (state != STOP && !alarmTriggered) {
                result = readByteSerialPort(&byte, 1);
                if (result > 0) {
                    switch (state){
                        case START: {
                            if (byte == FLAG) {
                                state = FLAG_RCV;
                            }
                            break;
                        }

                        case FLAG_RCV: {
                            if (byte == A_RX) {
                                state = A_RCV;
                            }
                            else if (byte != FLAG) {
                                state = START;
                            }
                            break;
                        }

                        case A_RCV: {
                            if (byte == C_RR0 || byte == C_RR1 || byte == C_REJ0 || byte == C_REJ1 || byte == C_DISC) {
                                state = C_RCV;
                                response = byte;
                            }
                            else if (byte == FLAG) {
                                state = FLAG_RCV;
                            }
                            else {
                                state = START;
                            }
                            break;
                        }

                        case C_RCV: {
                            if (byte == (A_RX ^ response)) {
                                state = BCC_OK;
                            }
                            else if (byte == FLAG) {
                                state = FLAG_RCV;
                            }
                            else {
                                state = START;
                            }
                            break;
                        }

                        case BCC_OK: {
                            if (byte == FLAG) {
                                framesReceived++;
                                state = STOP;
                            }
                            else {
                                state = START;
                            }
                            break;
                        }

                        case STOP: {
                            break;
                        }

                        default:
                            return -1;
                    }
                }
            }
            if (state != STOP) {
                continue;
            }

            if (response == C_RR0 || response == C_RR1) {
                accepted = TRUE;
                tramaTx = (tramaTx + 1) % 2;
                break;
            }
            else if (response == C_REJ0 || response == C_REJ1) {
                alarmTriggered = FALSE;
                rejected = TRUE;
                break;
            }
            else if (response < 0) {
                return -1;
            }
        }
        if (accepted || rejected) {
            break;
        }
        currentTransmission--;
    }

    free(frame);
    alarmCount = 0;

    if (accepted) {
        return frameSize;
    }
    else {
        return -1;
    }
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet){
    unsigned char byte, field;
    int i = 0;
    int result;
    LinkLayerState state = START;
    while (state != STOP) {
        result = readByteSerialPort(&byte, 1);
        if (result > 0) {
            switch (state){
                case START: {
                    if (byte == FLAG) {
                        state = FLAG_RCV;
                    }
                    break;
                }

                case FLAG_RCV: {
                    if (byte == A_TX) {
                        state = A_RCV;
                    }
                    else if (byte != FLAG) {
                        state = START;
                    }
                    break;
                }

                case A_RCV: {
                    if (byte == C_N0 || byte == C_N1) {
                        state = C_RCV;
                        field = byte;
                    }
                    else if (byte == FLAG) {
                        state = FLAG_RCV;
                    }
                    else {
                        state = START;
                    }
                    break;
                }

                case C_RCV: {
                    if (byte == (field ^ A_TX)) {
                        state = DATA_RCV;
                    }
                    else if (byte == FLAG) {
                        state = FLAG_RCV;
                    }
                    else {
                        state = START;
                    }
                    break;
                }

                case DATA_RCV: {
                    if (byte == ESCAPE) {
                        state = FOUND_DATA;
                    }
                    else if (byte == FLAG) {
                        framesReceived++;
                        // Check BCC2
                        unsigned char BCC2 = packet[i - 1];
                        i--;
                        packet[i] = '\0';
                        unsigned char check = packet[0];

                        // Calculate BCC2
                        for (unsigned int j = 1; j < i; j++) {
                            check ^= packet[j];
                        }

                        // If BCC2 is correct, send RR
                        if (BCC2 == check) {
                            int c;
                            c = tramaRx % 2 == 0 ? C_RR0 : C_RR1;
                            unsigned char frame[5] = {FLAG, A_RX, c, (A_RX ^ c), FLAG};
                            if (writeBytesSerialPort(frame, 5) < 0) {
                                perror("ERROR: Error on writing to serial port. (4)\n");
                            }
                            framesSent++;
                            if ((tramaRx % 2 == 0 && field == C_N0) || (tramaRx % 2 == 1 && field == C_N1)) {
                                tramaRx = (tramaRx + 1) % 2;
                            }
                            else {
                                i = 0;
                            }
                            return i;
                        }

                        // If BCC2 is incorrect, send REJ
                        else {
                            int c;
                            if (tramaRx % 2 == 0) c = C_REJ0;
                            else c = C_REJ1;
                            unsigned char frame[5] = {FLAG, A_RX, c, (A_RX ^ c), FLAG};
                            if (writeBytesSerialPort(frame, 5) < 0) {
                                perror("ERROR: Error on writing to serial port. (5)\n");
                            }
                            framesSent++;
                            state = START;
                            i = 0;
                            continue;
                        }

                    }

                    // Data
                    else {
                        packet[i++] = byte;
                    }
                    break;
                }

                case FOUND_DATA: {
                    state = DATA_RCV;
                    packet[i++] = byte ^ STUFF;
                    break;
                }
            }
        }
    }
}

void printStats(role) {
    switch (role) {
        case LlTx:
            printf("\n");
            printf("╔════════════════════════════════════════════════════════╗\n");
            printf("║      Displaying Statistics for Transmitter (LlTx)      ║\n");
            printf("╠═════════════════════════╦══════════════════════════════╣\n");
            printf("║      Total Runtime      ║     %10ld seconds       ║\n", endTime - startTime);
            printf("╠═════════════════════════╬══════════════════════════════╣\n");
            printf("║       Frames Sent       ║     %10d               ║\n", framesSent);
            printf("╠═════════════════════════╬══════════════════════════════╣\n");
            printf("║     Frames Received     ║     %10d               ║\n", framesReceived);
            printf("╠═════════════════════════╬══════════════════════════════╣\n");
            printf("║    Data Transfer Time   ║     %10ld seconds       ║\n", endTime - startTimeConnection);
            printf("╚═════════════════════════╩══════════════════════════════╝\n\n");
            break;

        case LlRx:
            printf("\n");
            printf("╔════════════════════════════════════════════════════════╗\n");
            printf("║     Displaying Statistics for Receiver (LlRx)          ║\n");
            printf("╠═════════════════════════╦══════════════════════════════╣\n");
            printf("║      Total Runtime      ║     %10ld seconds       ║\n", endTime - startTime);
            printf("╠═════════════════════════╬══════════════════════════════╣\n");
            printf("║       Frames Sent       ║     %10d               ║\n", framesSent);
            printf("╠═════════════════════════╬══════════════════════════════╣\n");
            printf("║     Frames Received     ║     %10d               ║\n", framesReceived);
            printf("╠═════════════════════════╬══════════════════════════════╣\n");
            printf("║    Data Transfer Time   ║     %10ld seconds       ║\n", endTime - startTimeConnection);
            printf("╚═════════════════════════╩══════════════════════════════╝\n\n");
            break;

        default:
            break;
    }
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics){
    LinkLayerState state = START;
    unsigned char byte;
    alarmTriggered = FALSE;
    int currentTransmission = retransmissions;

    switch (role) {
        case (LlTx): {
            // Set alarm handler
            (void) signal(SIGALRM, alarmHandler);
            alarmTriggered = FALSE;

            // Send DISC frame
            unsigned char frame_disc[5] = {FLAG, A_TX, C_DISC, A_TX ^ C_DISC, FLAG};
            while (currentTransmission && state != STOP) {
                if (writeBytesSerialPort(frame_disc, 5) < 0) {
                    perror("ERROR: Error on writing to serial port. (6)\n");
                }
                framesSent++;
                alarmTriggered = FALSE;
                alarm(timeout);

                // Read DISC frame
                while (!alarmTriggered && state != STOP) {
                    unsigned char byte;
                    int result;
                    result = readByteSerialPort(&byte, 1);
                    if (result > 0) {
                        switch (state) {
                            case START: {
                                if (byte == FLAG) {
                                    state = FLAG_RCV;
                                }
                                break;
                            }

                            case FLAG_RCV: {
                                if (byte == A_RX) {
                                    state = A_RCV;
                                }
                                else if (byte != FLAG) {
                                    state = START;
                                }
                                break;
                            }

                            case A_RCV: {
                                if (byte == C_DISC) {
                                    state = C_RCV;
                                }
                                else if (byte == FLAG) {
                                    state = FLAG_RCV;
                                }
                                else {
                                    state = START;
                                }
                                break;
                            }

                            case C_RCV: {
                                if (byte == (C_DISC ^ A_RX)) {
                                    state = BCC_OK;
                                }
                                else if (byte == FLAG) {
                                    state = FLAG_RCV;
                                }
                                else {
                                    state = START;
                                }
                                break;
                            }

                            case BCC_OK: {
                                if (byte == FLAG) {
                                    framesReceived++;
                                    state = STOP;
                                }
                                else {
                                    state = START;
                                }
                                break;
                            }
                        }
                    }
                }
                currentTransmission--;
            }
            break;
        }

        case (LlRx): {
            // Read DISC frame
            while (state != STOP) {
                int result = readByteSerialPort(&byte, 1);
                if (result > 0) {
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
                            if (byte == C_DISC) {
                                state = C_RCV;
                            }
                            else if (byte == FLAG) {
                                state = FLAG_RCV;
                            }
                            else {
                                state = START;
                            }
                            break;

                        case C_RCV:
                            if (byte == (C_DISC ^ A_TX)) {
                                state = BCC_OK;
                            }
                            else if (byte == FLAG) {
                                state = FLAG_RCV;
                            }
                            else {
                                state = START;
                            }
                            break;

                        case BCC_OK:
                            if (byte == FLAG) {
                                framesReceived++;
                                state = STOP;
                            }
                            else {
                                state = START;
                            }
                            break;
                    }
                }
            }

            // Send DISC frame
            unsigned char frame_disc[5] = {FLAG, A_RX, C_DISC, A_RX ^ C_DISC, FLAG};
            if (writeBytesSerialPort(frame_disc, 5) < 0) {
                perror("ERROR: Error on writing to serial port. (7)\n");
            }
            framesSent++;
        }

        default:
            break;
    }

    time(&endTime);

    if (state != STOP) {
        return -1;
    }

    alarm(0);

    printStats(role);

    int clstat = closeSerialPort();
    return clstat;
}