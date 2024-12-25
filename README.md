# Data Link Protocol over RS-232

A project for the **Redes de Computadores** (Computer Networks) 2024-2025 course at **FEUP**, implementing a data link protocol for file transfer over RS-232 using the Stop-and-Wait protocol. Please note that this README provides a summary of the project. For a detailed report, please refer to the [official documentation](https://github.com/GuilhermeRego/RCOM-Lab1/blob/main/RCOM_REPORT_LAB1_T9_up202207041_up202207784.pdf).

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

#### Data Transmission:
Packets sent with Stop-and-Wait protocol and byte stuffing.

#### Error Handling:
REJ and retransmissions for corrupted frames.

#### Validation
Tested with:

- Variable file sizes and baud rates.
- Simulated noise and interruptions.

Example of the program's execution:
- Transmitter:

![image](https://github.com/user-attachments/assets/a90bd864-6a9b-4535-85e6-931e78672cce)

- Receiver:

![image](https://github.com/user-attachments/assets/1593929d-372b-4a4f-a7dc-7dd9070ea659)

Checking differences between the original file and the result:

![image](https://github.com/user-attachments/assets/7faa9525-65fb-4d3f-a7d4-1733c6eb6f24)

![image](https://github.com/user-attachments/assets/02ed0211-784e-4bea-bc42-bdd32a7a6131)


### Conclusions
This project demonstrated:

- The impact of baud rate and noise on transfer efficiency.
- Robust file transfer under adverse conditions using Stop-and-Wait.

### Authors
- Gabriel da Quinta Braga (up202207784)
- Guilherme Silveira Rego (up202207041)
