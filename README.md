# Secure Multi-threaded Server-Client Application

A robust C++ implementation of a secure client-server application using Windows Socket Programming (Winsock2). Features encrypted communication, custom thread pooling, and file transfer capabilities.

## Features

### Core Functionality
- TCP-based communication using Winsock2 API
- Support for multiple concurrent client connections
- File transfer with progress tracking
- Real-time chat messaging
- Custom thread pool implementation for efficient connection handling

### Security
- Diffie-Hellman key exchange for secure communication
- Custom encryption implementation for messages and files
- Session-based security with unique keys per connection

### Performance
- Custom thread pool with dynamic task distribution
- Efficient file transfer using chunked data transmission
- Automatic hardware-optimized thread count
- Non-blocking I/O operations

## Technical Implementation

### Networking
- Uses TCP/IP protocol for reliable data transmission
- Implements server-side connection pool
- Supports IPv4 addressing
- Default port: 55555

### Security Implementation
```cpp
// Key Exchange
primitivRoot = 26363;
private_key = rand() % 65536;
prime = generateRandomPrime(0, 65536);
pub_key = mod_exp(primitivRoot, private_key, prime);
secret = mod_exp(pub_key_client, private_key, prime);
```

### Thread Pool Architecture
- Dynamic thread allocation based on hardware concurrency
- Task queue with mutex protection
- Condition variables for thread synchronization
- Automated task distribution

### File Transfer
- Chunked file transfer (1024 KB chunks)
- Progress tracking
- Automatic file naming with timestamps
- Support for multiple file types

## Usage

### Server Commands
1. Start the server
2. Server automatically listens for incoming connections
3. Handles multiple clients concurrently via thread pool

### Client Commands
```
1. CHAT - Send messages to server
2. SEND - Transfer files to server
3. RECV - Request files from server
4. STOP - Terminate connection
```

## Building the Project

### Prerequisites
- Windows OS
- Visual Studio (with C++ support)
- Winsock2 library

### Compilation
1. Include required headers:
```cpp
#include <winsock2.h>
#include <ws2tcpip.h>
```

2. Link necessary libraries:
```
ws2_32.lib
```

## Technical Details

### Buffer Sizes
- Maximum Buffer: 1024 KB
- Chunk Size: 1 KB
- Default message buffer: 32 bytes

### Security Constants
- Private Key Range: 0-65535
- Prime Number Range: 0-65535
- Fixed Primitive Root: 26363

## Architecture

```
Server
├── Thread Pool Manager
│   ├── Task Queue
│   └── Worker Threads
├── Connection Handler
│   ├── Client Sessions
│   └── Key Exchange
└── File Manager
    ├── Upload Handler
    └── Download Handler

Client
├── Connection Manager
├── File Transfer
└── Message Handler
```
