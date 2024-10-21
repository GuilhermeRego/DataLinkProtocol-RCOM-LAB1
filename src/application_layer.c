#include "application_layer.h"
#include "link_layer.h"
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>

// Função que interpreta ("parseia") um frame de controle
unsigned char* parseControlFrame(unsigned char* frame, int size, unsigned long int *fileSize) {

    // Extrai o tamanho do arquivo (File Size)
    unsigned char fileSize1 = frame[2];  // Pega o tamanho do campo de tamanho do arquivo
    unsigned char fileSize2[fileSize1];  // Cria um array temporário para armazenar o tamanho do arquivo
    memcpy(fileSize2, frame+3, fileSize1); // Copia o tamanho do arquivo do quadro para o array temporário

    // Converte os bytes para formar o valor completo do tamanho do arquivo
    for(int i = 0; i < fileSize1; i++) {
        *fileSize |= (fileSize2[fileSize1 - i - 1] << (i * 8)); // Constrói o tamanho do arquivo somando os bytes
    }

    // Extrai o nome do arquivo (File Name)
    unsigned char fileNBytes = frame[3 + fileSize1 + 1]; // Tamanho do nome do arquivo
    unsigned char *ret = (unsigned char*)malloc(fileNBytes); // Aloca memória para o nome do arquivo
    memcpy(ret, frame + 3 + fileSize1 + 2, fileNBytes); // Copia o nome do arquivo para o buffer retornado

    return ret; // Retorna o nome do arquivo
}

// Função para construir um quadro de controle (control frame)
unsigned char* getControlFrame(const unsigned int c, const char* filename, long int len, unsigned int* size) {
    // Calcula o tamanho necessário para armazenar o tamanho do arquivo (em bytes)
    int L0 = (int) ((float) len / 8.0); // Tamanho do arquivo em bytes 
    int L1 = strlen(filename); // Tamanho do nome do arquivo
    *size = L0 + L1 + 5; // O tamanho total do quadro

    // Aloca memória para o quadro de controle
    unsigned char *frame = (unsigned char*) malloc(*size);
    
    unsigned int pos = 0;
    frame[pos++] = c;   // Campo de controle (c: START = 1, END = 3)
    frame[pos++] = 0;   // Campo indicador de tamanho do arquivo (ID)
    frame[pos++] = L0;  // Quantos bytes o campo de tamanho do arquivo tem (L0)

    // Converte o tamanho do arquivo para uma sequência de bytes
    for (unsigned char i = 0 ; i < L0 ; i++) {
        frame[L0 - i + 2] = len & 0xFF; // Pega os últimos 8 bits de "len"
        len >>= 8; // Desloca o tamanho do arquivo para processar o próximo byte
    }
    
    pos += L0; // Atualiza o ponteiro
    frame[pos++] = 1;   // ID para o nome do arquivo
    frame[pos++] = L1;  // Quantos bytes o nome do arquivo tem
    memcpy(frame + pos, filename, L1);  // Copia o nome do arquivo no quadro de controle

    return frame; // Retorna o quadro de controle
}

// Função que cria e retorna um frame de dados (data frame)
unsigned char * getDataFrame(unsigned char sequence, unsigned char *data, int dataSize, int *frameSize) {
    // O frame de dados é composto por 5 bytes de cabeçalho e o conteúdo dos dados
    *frameSize = 5 + dataSize;
    unsigned char* frame = (unsigned char*) malloc(*frameSize);

    // Preenche o cabeçalho do quadro de dados
    frame[0] = 1;  // Tipo de frame: 1 para frame de dados
    frame[1] = sequence;  // Número de sequência (para controle de duplicatas e ordem)
    frame[2] = (dataSize >> 8) & 0xFF;  // Parte alta do tamanho dos dados
    frame[3] = dataSize & 0xFF;  // Parte baixa do tamanho dos dados
    memcpy(frame + 4, data, dataSize);  // Copia os dados reais para o frame

    return frame;  // Retorna o quadro de dados
}

// Função que lê o conteúdo do arquivo e o coloca em um buffer
unsigned char * getData(FILE* fd, long int fileLength) {
    // Aloca memória para o conteúdo do arquivo
    unsigned char* content = (unsigned char*)malloc(sizeof(unsigned char) * fileLength);

    // Lê o arquivo completo para a memória
    fread(content, sizeof(unsigned char), fileLength, fd);

    return content;  // Retorna o buffer de dados do arquivo
}

// Função que interpreta um quadro de dados e copia os dados para um buffer
void parseDataframe(const unsigned char* frame, const unsigned int frameSize, unsigned char* buffer) {
    // Copia os dados do quadro (ignorando o cabeçalho de 4 bytes)
    memcpy(buffer, frame + 4, frameSize - 4);
    buffer += frameSize - 4; // Atualiza o ponteiro do buffer
}

// Função principal da camada de aplicação
void applicationLayer(const char *serialPort, const char *role, int baudRate, int nTries, int timeout, const char *filename) {
    // Completa a estrutura de dados do link layer
    LinkLayer linkLayer;
    strcpy(linkLayer.serialPort, serialPort);
    linkLayer.role = strcmp(role, "tx") ? LlRx : LlTx;
    linkLayer.baudRate = baudRate;
    linkLayer.nRetransmissions = nTries;
    linkLayer.timeout = timeout;

    // Abre a conexão com o link layer
    int fd = llopen(linkLayer);
    if (fd < 0) {
        perror("Connection error\n");
        exit(-1);
    }

    // Verifica se o papel é transmissor (LlTx) ou receptor (LlRx)
    switch (linkLayer.role) {
        case LlTx: {  // Transmissor
            // Abre o arquivo para leitura
            FILE* file = fopen(filename, "r");
            if (file == NULL) {
                perror("File not found\n");
                exit(-1);
            }
            
            // Calcula o tamanho do arquivo
            int prev = ftell(file);
            fseek(file, 0, SEEK_END);
            long int fileSize = ftell(file) - prev;
            fseek(file, prev, SEEK_SET);

            // Cria e envia o frame de controle inicial (START)
            unsigned int cfSize;
            unsigned char *controlFrameStart = getControlFrame(2, filename, fileSize, &cfSize);
            if(llwrite(controlFrameStart, cfSize) == -1){ 
                printf("Exit: error in start frame\n");
                exit(-1);
            }

            // Lê o conteúdo do arquivo e envia os frames dos dados em si
            unsigned char sequence = 0;
            unsigned char* content = getData(file, fileSize);
            long int bytesLeft = fileSize;

            // Envia os quadros de dados
            while (bytesLeft > 0) {
                int dataSize = bytesLeft > (long int) MAX_PAYLOAD_SIZE ? MAX_PAYLOAD_SIZE : bytesLeft;
                int isLast = (dataSize != MAX_PAYLOAD_SIZE) ? 1 : 0; // Verifica se é o último frame

                // Cria e envia o frames de dados
                int frameSize;
                unsigned char* frame = getDataFrame(sequence, content, dataSize, &frameSize);
                if(llwrite(frame, frameSize) == -1) {
                    printf("Exit: error in data frames\n");
                    exit(-1);
                }

                // Atualiza as variáveis de controle
                bytesLeft -= dataSize;
                content += dataSize;
                sequence++;
                sequence %= 255;
            }
            
            // Cria e envia o quadro de controle final (END)
            unsigned char *controlFrameEnd = getControlFrame(3, filename, fileSize, &cfSize);
            if(llwrite(controlFrameEnd, cfSize) == -1) { 
                printf("Exit: error in end frame\n");
                exit(-1);
            }

            // Fecha a conexão
            llclose(fd);
            break;
        }

        case LlRx: {  // Receptor
            // Recebe o frame de controle inicial (START)
            unsigned char *frame = (unsigned char *) malloc(MAX_PAYLOAD_SIZE);
            int frameSize = 0;
            while ((frameSize = llread(frame)) < 0);  // Espera pelo frame de controle

            unsigned long int rxFileSize = 0;
            unsigned char* name = parseControlFrame(frame, frameSize, &rxFileSize);  // Parse ao frame de controle

            // Cria um novo arquivo para salvar os dados recebidos (com o nome do arquivo e permissões de escrita)
            FILE* newFile = fopen((char *) name, "wb+");
            while (1) {    
                // Recebe o próximo frame de dados
                while ((frameSize = llread(frame)) < 0);

                // Se o frame for de controle END, finaliza
                if(frameSize == 0) break;

                // Caso contrário, processa o frame de dados
                else if(frame[0] != 3){
                    unsigned char *buffer = (unsigned char*)malloc(frameSize);
                    parseDataframe(frame, frameSize, buffer);
                    fwrite(buffer, sizeof(unsigned char), frameSize - 4, newFile);  // Escreve os dados no arquivo
                    free(buffer);
                }
            }
            
            // Fecha o arquivo
            fclose(newFile);
            break;
        }
    }
}
