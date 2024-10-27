#include "application_layer.h"
#include "link_layer.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned char *readFile(const char *filename, int *fileSize) {
    // Open file
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        perror("ERROR: Failed to open file\n");
        return NULL;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    *fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Read file
    unsigned char *fileData = (unsigned char *)malloc(*fileSize);
    if (fileData == NULL) {
        perror("ERROR: Failed to allocate memory for file\n");
        fclose(file);
        return NULL;
    }

    if (fread(fileData, 1, *fileSize, file) != *fileSize) {
        perror("ERROR: Failed to read file\n");
        fclose(file);
        free(fileData);
        return NULL;
    }

    fclose(file);
    return fileData;
}

unsigned char *getData(unsigned char* fileData, int fileSize) {
    if (fileData == NULL) {
        printf("ERROR: Invalid file data\n");
        return NULL;
    }
    unsigned char *data = (unsigned char *) malloc(fileSize);
    if (data == NULL) {
        printf("ERROR: Failed to allocate memory for data\n");
        return NULL;
    }
    memcpy(data, fileData, fileSize);
    return data;
}

unsigned char * getControlPacket (int controlField, int fileSize, char *fileName, int *packetSize) {
    // Calculate L1 and L2
    int L1 = 0;
    int tempFileSize = fileSize;
    while (tempFileSize > 0) {
        tempFileSize >>= 8; 
        L1++;
    }
    int L2 = strlen(fileName);
    *packetSize = 5 + L2 + L1;
    unsigned char *packet = (unsigned char *) malloc(*packetSize);

    // Create packet
    packet[0] = controlField;
    packet[1] = 0x00;
    packet[2] = L1;

    for (int i = 0; i < L1; i++) {
        packet[3 + L1 - i - 1] = fileSize & 0xFF;
        fileSize >>= 8;
    }

    packet[3 + L1] = 0x01;
    packet[4 + L1] = L2;

    // Copy filename to packet
    memcpy(packet + L1 + 5, fileName, L2);

    return packet;
}

unsigned char *getDataPacket(int sequenceNumber, int dataSize, unsigned char *data, int *packetSize) {
    *packetSize = dataSize + 4;
    unsigned char *packet = (unsigned char *) malloc(*packetSize);

    packet[0] = 0x01;
    packet[1] = sequenceNumber;
    packet[2] = dataSize >> 8 & 0xFF;
    packet[3] = dataSize & 0xFF;

    memcpy(packet + 4, data, dataSize);

    return packet;
}

unsigned char *parseControlPacket(unsigned char *packet, int packetSize, unsigned long int *fileSize) {
    // Check if packet is valid
    if (packet == NULL || fileSize == NULL) {
        printf("ERROR: Invalid arguments\n");
        return NULL;
    }

    // File size
    unsigned char nbytes = packet[2];
    unsigned char sizeaux[nbytes];
    memcpy(sizeaux, packet + 3, nbytes);
    for (int i = 0; i < nbytes; i++) {
        *fileSize |= (unsigned long) sizeaux[i] << (8 * i);
    }

    // File name
    unsigned char nameSize = packet[3 + nbytes];
    unsigned char *name = (unsigned char *) malloc(nameSize);
    memcpy(name, packet + 5 + nbytes, nameSize);
    return name;
}

void parseDataPacket(unsigned char *packet, int packetSize, int *buffer) {
    // Check if packet is valid
    if (packet == NULL || buffer == NULL) {
        printf("ERROR: Invalid arguments\n");
        return;
    }

    memcpy(buffer, packet + 4, packetSize - 4);
    buffer += packetSize + 4;
}


void applicationLayer(const char *serialPort, const char *role, int baudRate, int nTries, int timeout, const char *filename) {
    // Completes the link layer struct
    LinkLayer linkLayer;
    strcpy(linkLayer.serialPort, serialPort);
    linkLayer.role = strcmp(role, "tx") ? LlRx : LlTx;
    linkLayer.baudRate = baudRate;
    linkLayer.nRetransmissions = nTries;
    linkLayer.timeout = timeout;

    // Opens the connection
    int fd = llopen(linkLayer);
    if (fd < 0) {
        perror("ERROR: llopen\n");
        exit(-1);
    }

    printf("Connection opened successfully\n");
    if (llclose(fd) < 0) exit(-1);
    printf("Connection closed successfully\n");
    exit(0);

    //printf("Connection opened successfully\n");

    // Calls the appropriate function based on the role
    switch (linkLayer.role) {
        case LlTx:   // Transmiter
            
            // Read file
            int fileSize;
            unsigned char *fileData;
            if ((fileData = readFile(filename, &fileSize)) == NULL) {
                perror("ERROR: readFile\n");
                exit(-1);
            }

            // Get start control packet
            int packetSize;
            unsigned char *startPacket;
            if ((startPacket = getControlPacket(0x01, fileSize, filename, &packetSize)) == NULL) {
                perror("ERROR: getControlPacket\n");
                exit(-1);
            }

            // Send control packet
            if (llwrite(startPacket, packetSize) < 0) {
                perror("ERROR: llwrite\n");
                exit(-1);
            }

            // Send data packet
            int sequenceNumber = 0;
            unsigned char *data;
            if ((data = getData(fileData, fileSize)) == NULL) {
                perror("ERROR: getData\n");
                exit(-1);
            }

            int writtenBytes = 0;
            while (writtenBytes <= fileSize) {
                
                // Calculate bytes to write
                int bytesToWrite = fileSize - writtenBytes > MAX_PAYLOAD_SIZE ? MAX_PAYLOAD_SIZE : fileSize - writtenBytes;
                
                // Copy data to send
                *data = (unsigned char *) malloc(bytesToWrite);
                memcpy(data, fileData + writtenBytes, bytesToWrite);
                
                // Get data packet
                int ps;
                unsigned char *packet;
                if ((packet = getDataPacket(sequenceNumber, bytesToWrite, data, &ps)) == NULL) {
                    perror("ERROR: getDataPacket\n");
                    exit(-1);
                }

                // Write data packet
                if (llwrite(packet, ps) < 0) {
                    perror("ERROR: llwrite\n");
                    exit(-1);
                }
                writtenBytes += bytesToWrite;
            }

            // Get end control packet
            unsigned char *endPacket;
            if ((endPacket = getControlPacket(0x03, fileSize, filename, &packetSize)) == NULL) {
                perror("ERROR: getControlPacket\n");
                exit(-1);
            }

            // Send end control packet
            if (llwrite(endPacket, packetSize) < 0) {
                perror("ERROR: llwrite\n");
                exit(-1);
            }

            llclose(fd);
            break;

        case LlRx: {  // Receiver

            // Create space for control packet
            unsigned char *packet = (unsigned char *) malloc(MAX_PAYLOAD_SIZE);
            int packetSize = 0;

            // Read control packet
            while ((packetSize = llread(packet)) < 0);
            printf("Received control packet\n");
            unsigned long int fileSize = 0;
            unsigned char *fileName;
            if ((fileName = parseControlPacket(packet, packetSize, &fileName)) == NULL) {
                perror("ERROR: getControlPacket\n");
                exit(-1);
            }

            // Create file
            FILE *file = fopen(fileName, "wb");
            if (file == NULL) {
                perror("ERROR: Failed to create file\n");
                exit(-1);
            }
            // While there are packets to read
            while (TRUE) {
                // Wait for packet and read it
                while ((packetSize = llread(packet)) < 0);
                // Check if it is the end control packet
                if(packetSize == 0) break;
                // Check if it is a data packet
                else if(packet[0] != 3){
                    unsigned char *buffer = (unsigned char*)malloc(packetSize);
                    parseDataPacket(packet, packetSize, buffer);
                    fwrite(buffer, sizeof(unsigned char), packetSize-4, file);
                    free(buffer);
                }
                else continue;
            }

            fclose(file);
            break;
        }
    }
    
    if (llclose(0) < 0) {
        perror("ERROR: llclose\n");
        exit(-1);
    }
}
