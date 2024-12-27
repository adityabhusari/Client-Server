// Client.cpp : This file contains the 'main' function. Program execution begins and ends there.
 
#include <iostream>
#include "stdafx.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <tchar.h>
#include <fstream>
#include <vector>
#define MAX_BUFFER 1024*1024
#define CHUNK_SIZE 1024

using namespace std;

//Helpers
bool isPrime(int num) {
    if (num <= 1) return false;
    for (int i = 2; i <= sqrt(num); i++) {
        if (num % i == 0) return false;
    }
    return true;
}

int generateRandomPrime(int lower, int upper) {
    if (lower > upper) swap(lower, upper);
    vector<int> primes;

    for (int i = lower; i <= upper; i++) {
        if (isPrime(i)) {
            primes.push_back(i);
        }
    }

    if (primes.empty()) {
        cout << "No prime numbers in the given range!" << endl;
        return -1;
    }

    srand(time(0));
    int randomIndex = rand() % primes.size();
    return primes[randomIndex];
}

uint64_t mod_exp(uint64_t base, uint64_t exp, uint64_t mod) {
    uint64_t result = 1;
    base = base % mod;
    while (exp > 0) {
        if (exp % 2 == 1) { // If exp is odd
            result = (result * base) % mod;
        }
        exp = exp >> 1; // Divide exp by 2
        base = (base * base) % mod;
    }
    return result;
}

static void encrypt(char* sendBuffer, int size, uint64_t key) {
    for (size_t i = 0; i < size; i++) {
        sendBuffer[i] ^= (key >> (8 * (i % 8))) & 0xFF;
    }
}

//Handlers
void inputHandler(SOCKET clientSocket, char* recieveConf, char* requestBuffer) {
    memset(requestBuffer, 0, 8);

    cout << "Enter your request........." << endl;
    cin.getline(requestBuffer, sizeof(requestBuffer));

    requestBuffer[strcspn(requestBuffer, "\n")] = 0;

    int requestBytes = send(clientSocket, requestBuffer, strlen(requestBuffer), 0);

    if (requestBytes == SOCKET_ERROR || requestBytes == 0) {
        cout << "Server send error " << WSAGetLastError() << endl;;
        WSACleanup();
        return;
    }
    cout << "DEBUG: Client : Sent request: '" << requestBuffer << "', bytes: " << requestBytes << endl;

    memset(recieveConf, 0, sizeof(recieveConf));

    requestBytes = recv(clientSocket, recieveConf, 32, 0);

    if (requestBytes == SOCKET_ERROR || requestBytes == 0) {
        cout << "Server send error " << WSAGetLastError() << endl;;
        WSACleanup();
        return;
    }
    cout << "DEBUG: Client : Recieved Confirmation for " << requestBuffer << " request: '" << recieveConf << endl;
}

void chatRequestHandler(SOCKET clientSocket, char* sendBuffer, char* recieveConf, char* requestBuffer, uint64_t secret) {
    cout << "Enter your message for server" << endl;
    cin.getline(sendBuffer, MAX_BUFFER);

    //Encrypt
    encrypt(sendBuffer, MAX_BUFFER, secret);
    
    //Send returns the number of bytes sent to the server
    auto byteCount = send(clientSocket, sendBuffer, MAX_BUFFER, 0);

    if (byteCount != SOCKET_ERROR) {
        cout << "Client: sent " << sendBuffer << endl;
    }
    else {
        cout << "Server send error " << WSAGetLastError() << endl;;
        WSACleanup();
        return;
    }

    //Overwriting the recieve buffer for new information
    memset(recieveConf, 0, sizeof(recieveConf));

    byteCount = recv(clientSocket, recieveConf, 32, 0);

    if (byteCount == SOCKET_ERROR) {
        cout << "Server send error " << WSAGetLastError() << endl;;
        WSACleanup();
        return;
    }
    else if (strcmp(recieveConf, "STOP") == 0) {
        cout << "Server disconnected...." << endl;
        return;
    }
    cout << "Server: " << recieveConf << endl;
}

void fileRequestHandle(SOCKET clientSocket, char* recieveConf, uint64_t secret) {
    char filepath[200];
    string ex;
    char extension[16] = "";

    cout << "Enter the filepath" << endl;
    cin.getline(filepath, 200);

    ex = strrchr(filepath, '.') + 1;
    
    memcpy(extension, ex.c_str(), 16);
;
    //ifstream to only read the file
    fstream file(filepath, ios::binary | ios::in);

    if (!file.is_open()) {
        cout << "Error opening file." << endl;
        return;
    }

    //Get size of the file
    file.seekg(0, ios::end);
    streampos fileSize = file.tellg();
    file.seekg(0, ios::beg);

    // Send file size first
    send(clientSocket, reinterpret_cast<const char*>(&fileSize), sizeof(fileSize), 0);

    send(clientSocket, extension, 16, 0);

    //Extracts n characters from the stream and stores them in the array pointed to by s.
    char chunkBuffer[CHUNK_SIZE];
    
    while (file.read(chunkBuffer, CHUNK_SIZE) || file.gcount() > 0) {
        encrypt(chunkBuffer, CHUNK_SIZE, secret);
        send(clientSocket, chunkBuffer, file.gcount(), 0);
        memset(chunkBuffer, 0, sizeof(chunkBuffer));
    }
    cout << "File sent to server waiting for response......" << endl;
    file.close();

    auto byteCount = recv(clientSocket, recieveConf, 32, 0);

    if (byteCount == SOCKET_ERROR) {
        cout << "Server send error " << WSAGetLastError() << endl;;
        WSACleanup();
        return;
    }

    cout << "Server: " << recieveConf << endl;
}

void stopRequestHandler(SOCKET clientSocket) {
    cout << "Ending conversation with server." << endl;
    closesocket(clientSocket);
    WSACleanup();
    return;
}

int main()
{   
    //Step 1 => Initialize WSA
    cout << "----------STEP-1 => DLL SETUP------------" << endl;

    SOCKET clientSocket;
    int port = 55555;
    //data structure containing information about Windows sockets implementation that will be populated by the 
    // WSAStartup function
    WSADATA wsaData;
    int wsaerr;
    //Windows socket version here we have to typecast to primitive data type WORD
    WORD wVersionRequested = MAKEWORD(2, 2); // meaning 2.2 version
    wsaerr = WSAStartup(wVersionRequested, &wsaData);
    if (wsaerr != 0) {
        cout << "Winsock dll not found" << endl;
        return 0;
    }
    else {
        cout << "Winsock dll found" << endl;
        cout << "status: " << wsaData.szSystemStatus << endl;
    }

    cout << "---------- STEP-2 => CREATE CLIENT SOCKET ------------" << endl;
    clientSocket = INVALID_SOCKET;
    //af is address family here INET is IPv4, SOCK_STREAM is type here for TCP and IPPROTO_TCP is Protocol here TCP

    clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        cout << "Error while socket creation" << WSAGetLastError() << endl;
        WSACleanup();
        return 0;
    }
    else {
        cout << "socket() is OK !" << endl;
    }
    cout << "----------STEP-3 => CONNECT TO SERVER ------------" << endl;
    //No need to bind here as the OS will automatically do this for us
    sockaddr_in clientService;
    clientService.sin_family = AF_INET;
    InetPton(AF_INET, _T("127.0.0.1"), &clientService.sin_addr.s_addr);
    clientService.sin_port = htons(port);

    if (connect(clientSocket, (SOCKADDR*)& clientService, sizeof(clientService)) == SOCKET_ERROR){
        cout << "Client: connect() - Failed to connect" << endl;
        WSACleanup();
        return 0;
    }
    else {
        cout << "Client: connect() is OK !" << endl;
        cout << "Client: Can start sending and recieving data...." << endl;
    }

    //cout << "----------STEP-4 => SENDING AND RECIEVING DATA TO AND FROM SERVER ------------\n\n" << endl;

    //Calculate keys
    srand(10);
    uint16_t private_key, primitivRoot, prime, pub_key, pub_key_server;
    uint64_t secret;

    primitivRoot =  26363;
    private_key = rand() % 65536;

    if (recv(clientSocket, (char*)&prime, sizeof(uint16_t), 0) == SOCKET_ERROR) {
        std::cout << "Error while recieveing prime" << WSAGetLastError() << endl;;
        return -1;
    }
    
    if (recv(clientSocket, (char*)&pub_key_server, sizeof(uint16_t), 0) == SOCKET_ERROR) {
        std::cout << "Error while recieveing pub_key_server" << WSAGetLastError() << endl;;
        return -1;
    }
 
    pub_key = mod_exp(primitivRoot, private_key, prime);
    
    cout << "KEYS: " << "PRIVATE: " << private_key << " PRIME: " << prime << " SERVER PUBLIC: " << pub_key_server << endl;

    if (send(clientSocket, (char*)&pub_key, sizeof(uint16_t), 0) == SOCKET_ERROR) {
        cout << "Error sending keys " << WSAGetLastError() << endl;;
        WSACleanup();
        return -1;
    }
        
    secret = mod_exp(pub_key_server, private_key, prime);

    cout << "SECRET: " << secret << endl;

    char* sendBuffer = new char[MAX_BUFFER];
    char* recieveBuffer = new char[MAX_BUFFER];
    char recieveConf[32];
    char requestBuffer[9] = { 0 };  // Initialize to zeros

    cout << "\n***********************WELCOME TO MY SERVER !!!**************************" << endl;
    cout << "\t 1) TO SEND MESSAGE TO THE SERVER ENTER 'CHAT'...." << endl;
    cout << "\t 2) FOR SENDING A FILE TO SERVER ENTER 'SEND'...." << endl;
    cout << "\t 3) FOR REQUESTING A FILE FROM SERVER ENTER 'RECV'...." << endl;
    cout << "\t 4) TO EXIT PLEASE TYPE 'STOP'...." << endl;

    while (true) {

        //Input handler
        inputHandler(clientSocket, recieveConf, requestBuffer);

        //Send message to the server
        if (strncmp(requestBuffer, "CHAT", 4) == 0) {
            chatRequestHandler(clientSocket, sendBuffer, recieveBuffer, requestBuffer, secret);
        }
        //Send file to the server
        else if (strcmp(requestBuffer, "SEND") == 0) {
            fileRequestHandle(clientSocket, recieveConf, secret);
        }
        //Ending the conversation
        else if (strcmp(requestBuffer, "STOP") == 0) {
            stopRequestHandler(clientSocket);
            break;
        }
    }

    cout << "----------STEP-5 => CLOSE SOCKET ------------" << endl;
   
    closesocket(clientSocket);
    WSACleanup();

    return 0;
}


