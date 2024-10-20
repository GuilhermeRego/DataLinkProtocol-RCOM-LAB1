// Application layer protocol implementation

#include "application_layer.h"

#include <stdio.h>

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer linklayer;
    strcpy(linklayer.serialPort, serialPort);
    linklayer.baudRate = baudRate;
    linklayer.nRetransmissions = nTries;
    linklayer.timeout = timeout;
    linklayer.role = strcmp(role, "tx") == 0 ? LlTx : LlRx;

    int fd = llopen(linklayer);
    if (fd < 0) {
        printf("ERROR: Failed to open connection\n");
        exit(-1);
    }
}
