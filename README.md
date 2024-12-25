# Data Link Protocol over RS-232

A project for the **Redes de Computadores** (Computer Networks) 2024-2025 course at **FEUP**, implementing a data link protocol for file transfer over RS-232 using the Stop-and-Wait protocol. This README is the summary of the whole project, for a complete report about this work please check the [official report](https://github.com/GuilhermeRego/RCOM-Lab1/blob/main/RCOM_REPORT_LAB1_T9_up202207041_up202207784.pdf).

## Overview
### Features
- Reliable file transfer using **Stop-and-Wait error control**.
- RS-232 communication with framing, byte stuffing, and flow control.
- Handles variable file sizes, noise levels, and baud rates.

### Architecture
- **Application Layer**: Handles user input, file management, and progress updates.
- **Link Layer**: Manages connection setup, data transmission, and error handling.

### Usage
Run on two terminals for transmitter and receiver roles:

#### Transmitter
```bash
make run_tx
```

#### Receiver
```bash
make run_rx
```

### Results
- Efficient transfer with high reliability.
- Transfer time inversely proportional to baud rate.
- Increased noise leads to longer transmission due to retransmissions.

### Key Components
#### Functions
- Application Layer:
sendControlPacket(), readControlPacket(), sendDataPacket(), updateProgressBar().

- Link Layer:
llopen(), llwrite(), llread(), llclose().

#### Protocol
Connection: Established with SET and UA supervision frames.

#### Data Transmission: Packets sent with Stop-and-Wait protocol and byte stuffing.

#### Error Handling: REJ and retransmissions for corrupted frames.

#### Validation
Tested with:

- Variable file sizes and baud rates.
- Simulated noise and interruptions.

### Conclusions
This project demonstrated:

- The impact of baud rate and noise on transfer efficiency.
- Robust file transfer under adverse conditions using Stop-and-Wait.

### Authors
Gabriel da Quinta Braga (up202207784)
Guilherme Silveira Rego (up202207041)
