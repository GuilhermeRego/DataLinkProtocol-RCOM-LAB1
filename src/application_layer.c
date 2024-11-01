// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Control field indicating type of packet
#define startPacket 0x01
#define dataPacket 0x02
#define endPacket 0x03

extern int rejected;

// Update progress bar
void updateProgressBar(int bytesWritten, int fileSize) {
    int progressBarWidth = 50;
    float progress = (float) bytesWritten / fileSize;
    int pos = progress * progressBarWidth;

    printf("\r[");
    for (int i = 0; i < progressBarWidth; i++) {
        if (i < pos) printf("=");
        else if (i == pos) printf(">");
        else printf(" ");
    }
    printf("] %d%%", (int)(progress * 100));

    fflush(stdout);
}

// Send Control Packet
int sendControlPacket(int type, const char *filename, int fileSize) {
    // Initialize Packet
    int numBits = sizeof(int) * 8 - __builtin_clz(fileSize);        // Number of bits needed to represent fileSize
    size_t fileLength = (numBits+7)/8;                              // Number of bytes needed to represent fileSize
    size_t filenameSize = strlen(filename);                         // Size of filename
    int packetSize = 3 + fileLength + 2 + filenameSize;             // Size of packet
    unsigned char *packet = (unsigned char*) malloc(packetSize);    // Allocate memory for packet

    // Construct packet: See protocol page 27
    int pos = 0;
    packet[pos++] = type; // 0x01 if Start, 0x03 if End
    packet[pos++] = 0; // T1 -> File Size
    packet[pos++] = sizeof(size_t); // L1
    memcpy(packet + pos, &fileSize, sizeof(size_t)); // V1 File Size Value
    pos += sizeof(size_t); // Move position to the end of the File Size Value
    packet[pos++] = 1; // T2 -> File Name
    packet[pos++] = filenameSize; // L2
    memcpy(packet + pos, filename, filenameSize); // V2 File Name Value

    // Send packet
    if (llwrite(packet, packetSize) < 0) {
        perror("ERROR: Failed to send Control Packet.\n");
        return -1;
    }

    printf("Control Packet Successfully sent!\n");

    return 1;
}

// Read Control Packet
int readControlPacket(int type, unsigned char *buffer, size_t *fileSize, char *filename) {
    // Read Control Packet
    size_t packetSize;
    if ((packetSize = llread(buffer)) < 0) {
        perror("ERROR: Failed to read Control Packet.\n");
        return -1;
    }

    // Check if the type is correct
    if (buffer[0] != type) {
        perror("ERROR: Invalid Control Packet.\n");
        return -1;
    }

    // Parse Control Packet: See protocol page 27
    int i = 1;
    unsigned char info;
    while (i < packetSize) {
        info = buffer[i++];
        switch (info) {
            // File Size
            case 0:
                memcpy(fileSize, buffer + i, sizeof(size_t));
                i += sizeof(size_t);
                break;

            // File Name
            case 1:
                memcpy(filename, buffer + i + 1, buffer[i]);
                i += buffer[i] + 1;
                break;

            // Invalid Control
            default:
                perror("ERROR: Invalid Control Packet.\n");
                return -1;
        }
    }

    return 1;
}

// Send Data Packet
int sendDataPacket(unsigned char *buffer, int contentSize) {
    // Initialize Packet
    int packetSize = contentSize + 3;
    unsigned char *packet = (unsigned char*) malloc(packetSize);

    // Construct packet: See protocol page 27
    int pos = 0;
    packet[pos++] = 0x02; // Control field: data packet -> 2
    packet[pos++] = (unsigned char) (contentSize / 256); // L2
    packet[pos++] = (unsigned char) (contentSize % 256); // L1
    memcpy(packet + pos, buffer, contentSize); // Data field
    pos += contentSize;
    packet[pos++] = 0;

    // Send packet and wait for RR or REJ
    int result;
    while (1) {
        if ((result = llwrite(packet, packetSize)) > 0) {
            break;
        }
        else if (result <= 0) {
            if (rejected) {
                printf("\nReceived REJ, resending packet...\n");
            }
            else {
                printf("\nExceeded number of retransmissions, aborting...\n");
            }
            return -1;
        }
    }

    free(packet);

    return 0;
}

void applicationLayer(const char *serialPort, const char *role, int baudRate, int nTries, int timeout, const char *filename) {
    // Set up Link Layer Connection Parameters
    LinkLayer connectionParameters;
    strcpy(connectionParameters.serialPort, serialPort);
    connectionParameters.role = (strcmp(role, "tx") == 0) ? LlTx : LlRx;
    connectionParameters.baudRate = baudRate;
    connectionParameters.nRetransmissions = nTries;
    connectionParameters.timeout = timeout;

    // Open link Layer Connection
    int fd = llopen(connectionParameters);
    if (fd < 0) {
        perror("ERROR: Failed to open connection.\n");
        exit(-1);
    }

    switch (connectionParameters.role) {
        case LlTx: {
            // Open file for reading
            FILE *file = fopen(filename, "rb");
            if (file == NULL) {
                perror("ERROR: Couldn't open File.\n");
                exit(-1);
            }

            // Determine File Size
            int prev = ftell(file);
            fseek(file, 0l, SEEK_END);
            int fileSize = ftell(file) - prev;
            fseek(file, prev, SEEK_SET);

            printf("Sending file %s with size %d...\n", filename, fileSize);

            // Assemble and send Starting Packet
            while (sendControlPacket(startPacket, filename, fileSize) < 0) {
                printf("Resending Start Packet due to failed transmission.\n");
            }

            printf("Start packet Successfully sent!\n");

            // Send Data Packets
            unsigned char* buf = (unsigned char*) malloc(MAX_PAYLOAD_SIZE-3);
            int contentSize;
            int bytesWritten = 0;
            while (TRUE) {
                contentSize = fread(buf, 1, MAX_PAYLOAD_SIZE - 3, file);

                if (contentSize <= 0) {
                    break;
                }

                if (sendDataPacket(buf, contentSize) < 0) {
                    if (rejected) {
                        fseek(file, -contentSize, SEEK_CUR);
                        printf("Resending the same content block due to failed transmission.\n");
                    }
                    else {
                        printf("Exceeded number of retransmissions, aborting...\n");
                        exit(-1);
                    }
                }

                else {
                    updateProgressBar(bytesWritten, fileSize);
                    bytesWritten += contentSize;
                }
            }
            free(buf);
            printf("\n");
            fclose(file);

            printf("All Data Packets Successfully sent!\n");

            // Assemble and send Ending Packet
            while (sendControlPacket(endPacket, filename, fileSize) < 0) {
                printf("Resending End Packet due to failed transmission.\n");
            }

            printf("End packet Successfully sent!\n");

            // Terminate Connection
            if (llclose(fd) < 0) {
                perror("ERROR: Failed to close connection\n");
                exit(-1);
            }
            printf("SUCCESS!\n");
            break;
        }

        case LlRx: {
            // Read Start Packet
            size_t packetSize;
            char *newFilename;
            unsigned char *buffer = (unsigned char*) malloc(MAX_PAYLOAD_SIZE);

            printf("Waiting for Start Packet...\n");

            if (readControlPacket(startPacket, buffer, &packetSize, newFilename) < 0) {
                perror("ERROR: Failed to read Start Packet.\n");
                exit(-1);
            }

            printf("Start packet Successfully received!\n");

            // Create file for Writing
            FILE *newFile = fopen(filename, "wb+");
            if (newFile == NULL) {
                perror("ERROR: Couldn't create File.\n");
                exit(-1);
            }

            // Read Content sent from the Serial Port and write it in the file
            free(buffer);
            int contentSize;
            printf("Receiving file content...\n");
            while ((contentSize = llread(buffer) > 0)) {
                if (buffer[0] == endPacket) {
                    break;
                }
                fwrite(buffer + 3, 1, buffer[1] * 256 + buffer[2], newFile);
            }

            free(buffer);
            fclose(newFile);

            // Terminate Connection
            if (llclose(fd) < 0) {
                perror("ERROR: Failed to close connection\n");
                exit(-1);
            }
            printf("SUCCESS!\n");
            break;
        }

        default:
            exit(-1);
    }
}