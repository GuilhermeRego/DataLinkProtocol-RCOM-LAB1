#include "application_layer.h"
#include "link_layer.h"

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

    // Calls the appropriate function based on the role
    switch (linkLayer.role) {
        case LlTx:   // Transmiter
            // I want to send a string to test the link layer
            char *string = "Hello, World!";
            if (llwrite((unsigned char *)string, strlen(string)) < 0) {
                perror("ERROR: llwrite\n");
                exit(-1);
            }
            break;

        case LlRx: {  // Receiver
            // I want to receive a string to test the link layer
            unsigned char buffer[MAX_PAYLOAD_SIZE];
            if (llread(buffer) < 0) {
                perror("ERROR: llread\n");
                exit(-1);
            }
            printf("Received: %s\n", buffer);
            break;
        }
    }
    
    if (llclose(0) < 0) {
        perror("ERROR: llclose\n");
        exit(-1);
    }
}
