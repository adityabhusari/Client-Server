// Server.cpp : This file contains the 'main' function. Program execution begins and ends there.
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <tchar.h>
#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>
#include <future>
#include <fstream>
#define MAX_BUFFER 1024*1024
#define CHUNK_SIZE 1024

using namespace std;

class ThreadPool {
private:

    vector<thread> threads;
    queue<function<void()>> tasks;
    mutex task_mutex;
    condition_variable mutex_condition;
    bool should_terminate = false;

public:
    //Intializing with the max number of threads supported by the hardware
    void Start() {
        int num_threads = thread::hardware_concurrency();
        for (int i = 0; i < num_threads; i++) {
            //Why Not Just the Function Name? A member function is not a standalone function, it is bound to an 
            // instance of its class. ThreadLoop on its own doesn’t know which instance of the class it should operate on.
            threads.emplace_back(thread(&ThreadPool::ThreadLoop, this));
        }
        std::cout << "Thread pool started with " << threads.size() << " threads." << std::endl;
    }

    void ThreadLoop() {
        //while loop so that the thread is continously active
        while (true) {
            function<void()> task;
            {
                unique_lock<std::mutex> lock(task_mutex);
                //waiting on the condition: If the tasks queue is empty then thread goes to sleep and release the lock
                //so that any other thread can acquire it 
                mutex_condition.wait(lock, [this] {
                    return !tasks.empty() || should_terminate;
                    });
                //if the programm is to terminate we should go through and return exiting out of the loop
                if (should_terminate) {
                    return;
                }
                task = tasks.front();
                tasks.pop();
                std::cout << "Task picked by thread: " << this_thread::get_id() << endl;
            }
            task();
      
        }
    }

    /*Templates allow the QueueTask method to accept any callable object(e.g., functions, lambdas, or functors)
    with any number of parameters and any return type.
    1) F is the callable function type (e.g., a lambda or function pointer).
    2) Args... represents the parameter types for the callable.
    3) decltype(func(args...)) deduces the return type of the callable.
    The thread pool queues tasks to be executed asynchronously, Without std::future, you'd have to block the main
    thread until the task completes, defeating the purpose of asynchronous execution.
    The && in F&& func and Args&&... args represents perfect forwarding,
    Perfect forwarding ensures that:
    1)If you pass an lvalue(e.g., a variable), it is passed as value to the callable.
    2)If you pass an rvalue(e.g., a temporary object), it is passed as reference to the callable.*/
    template<typename  F, typename ... Args>
    auto QueueTask(F&& func, Args&&... args) -> future<decltype(func(args...))> {

        using return_type = decltype(func(args...));

        /*is used to wrap the callable object. This lets us execute the function later while associating its
        result with a std::future. It gives us a shared pointer "task". The function and its arguments are
        bound together using std::bind*/

        auto task = make_shared<packaged_task<return_type()>>(bind(forward<F>(func), forward<Args>(args)...));

        future<return_type> result = task->get_future();

        /*block scoping this will automatically release the lock (functionality of unique_lock as it releases the
        lock when the scope ends*/
        {
            unique_lock<std::mutex> lock(task_mutex);
            /*[task]() { (*task)(); } is a lambda function that :
            1) Captures the task shared pointer by value([task]).
            2) Dereferences the task pointer and invokes the callable((*task)()).
            Why is this necessary? The queue stores generic tasks as std::function<void()>. To convert the
            actual task (which might take arguments and return values) into this format, we wrap it inside a
            lambda.*/
            tasks.push([task]() { (*task)(); });
        }


        //Notifying any sleeping thread that there is some task available. Ig it won't matter if I use notify_all
        mutex_condition.notify_one();
        return result;
    }

    ~ThreadPool() {
        {
            unique_lock<std::mutex> lock(task_mutex);
            should_terminate = true;
        }
        //notifying all the sleeping threads that should_terminate it true quickly finish your job and come out of
        //your while loops
        mutex_condition.notify_all();
        //Blocking the main thread till each thread is done and dusted with it's work
        for (thread& active_thread : threads) {
            active_thread.join();
        }
        threads.clear();
    }
};

//Helper
static bool isPrime(int num) {
    if (num <= 1) return false;
    for (int i = 2; i <= sqrt(num); i++) {
        if (num % i == 0) return false;
    }
    return true;
}

static int generateRandomPrime(int lower, int upper) {
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

static uint64_t mod_exp(uint16_t base, uint16_t exp, uint16_t mod) {
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

static void decrypt(char* recieveBuffer, int size, uint64_t key) {
    encrypt(recieveBuffer, size, key); // Same operation reverses the encryption
}

static string getCurrentTimeFilename(string extension) {
    // Get current time
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);

    // Use thread_local to ensure thread safety
    thread_local std::tm local_tm = {};

    // Use thread-safe localtime if available
    if (localtime_s(&local_tm, &time_t_now) != 0) {
        // Fallback or error handling
        return "default.txt";
    }

    // Create stringstream and format time
    std::stringstream ss;
    ss << std::put_time(&local_tm, "%Y%m%d_%H%M%S");

    // Append .txt to the end
    ss << "." << extension;

    return ss.str();
}

//Handler
static void handleClient(SOCKET& acceptSocket) {
    //std::cout << "Connection accepted on thread id: " << std::this_thread::get_id() << endl;
    std::cout << "AcceptSocket value: " << acceptSocket << " passed to thread." << std::this_thread::get_id() <<std::endl;

    std::cout << "----------STEP-6 => SEND AND RECIEVE DATA TO AND FROM CLIENT ------------" << endl;
    char requestRecvBuffer[8] = { 0 };  // Initialize entire buffer to zeros
    char* recieveBuffer = new char[MAX_BUFFER];
    char requestConfirmation[32] = "Recieved request confirmation";

    //Calculation of all keys
    srand(time(0));
    uint16_t private_key, primitivRoot, prime, pub_key, pub_key_client;
    uint64_t secret;

    primitivRoot = 26363;
    private_key = rand() % 65536;
    prime = generateRandomPrime(0, 65536);
    pub_key = mod_exp(primitivRoot, private_key, prime);


    if (send(acceptSocket, (char*)&prime, sizeof(uint16_t), 0) == SOCKET_ERROR) {
        std::cout << "Client send error " << WSAGetLastError() << endl;;
        return;
    }

    std::cout << "DEBUG: Sent Client prime successfully " << endl;

    if (send(acceptSocket, (char*)&pub_key, sizeof(uint16_t), 0) == SOCKET_ERROR) {
        std::cout << "Client send error " << WSAGetLastError() << endl;;
        return;
    }

    std::cout << "DEBUG: Sent Client pub_key successfully " << endl;

    if (recv(acceptSocket, (char *)&pub_key_client, sizeof(uint16_t), 0) == SOCKET_ERROR){
        std::cout << "Client send error " << WSAGetLastError() << endl;;
        return;
    }

    cout << "KEYS: " << " PRIVATE: " << private_key << " PRIME: " << prime << " CLIENT PUBLIC: " << pub_key_client << endl;

    //Calculate secret
    secret = mod_exp(pub_key_client, private_key, prime);
   
    cout << "Secret: " << secret << endl;
 
    while (true) {

        memset(requestRecvBuffer, 0, sizeof(requestRecvBuffer));
        int requestBytes = recv(acceptSocket, requestRecvBuffer, sizeof(requestRecvBuffer) - 1, 0);
        

        if (requestBytes == SOCKET_ERROR || requestBytes == 0) {
            std::cout << "Client disconnected or error: " << WSAGetLastError() << endl;
            break;
        }
        requestRecvBuffer[requestBytes] = '\0';
        std::cout << "DEBUG: Received bytes: " << requestBytes << endl;
        std::cout << "DEBUG: Received request (hex): ";
        for (int i = 0; i < requestBytes; ++i) {
            printf("%02X ", (unsigned char)requestRecvBuffer[i]);
        }
        std::cout << endl;

        std::cout << "DEBUG: Server : Received request (string): '" << requestRecvBuffer << "'" << endl;

        if (send(acceptSocket, "Recieved message confirmation", 32, 0) == SOCKET_ERROR) {
            std::cout << "Client send error " << WSAGetLastError() << endl;;
            continue;
        }
        std::cout << "DEBUG: Sent confirmation for: '" << requestRecvBuffer << "'" << endl;

        if (strncmp(requestRecvBuffer, "CHAT", 4) == 0) {
                printf("DEBUG: Entering CHAT block...\n");
                memset(recieveBuffer, 0, sizeof(recieveBuffer));
                //Overwriting the recieve buffer for new information
                int byteCount = recv(acceptSocket, recieveBuffer, MAX_BUFFER, 0);

                //decrypt
                decrypt(recieveBuffer, MAX_BUFFER, secret);

                //Send returns the number of bytes sent to the server
                if (byteCount == SOCKET_ERROR) {
                    std::cout << "Client send error " << WSAGetLastError() << endl;
                    break;
                }

                std::cout << "Server: recieved: " << recieveBuffer << " : Client on thread id: " << std::this_thread::get_id() << endl;

                if (send(acceptSocket, "Recieved message confirmation", 32, 0) == SOCKET_ERROR) {
                    std::cout << "Client send error " << WSAGetLastError() << endl;;
                    break;
                }
                std::cout << "Server: sent: " << "Recieved message confirmation" << endl;

        }

        if (strcmp(requestRecvBuffer, "SEND") == 0) {
            printf("DEBUG: Entering SEND block...\n");

            // Receive file size and extension first
            streampos fileSize;
            char extension[16];

            int sizeRecv = recv(acceptSocket, reinterpret_cast<char*>(&fileSize), sizeof(fileSize), 0);

            recv(acceptSocket, extension, 16, 0);

            if (sizeRecv != sizeof(fileSize)) {
                std::cout << "Error receiving file size: " << WSAGetLastError() << endl;
                continue;
            }
            
            string filename = getCurrentTimeFilename(extension);

            fstream file(filename, ios::binary | ios::out);

            if (!file.is_open()) {
                std::cout << "Error opening file......." << endl;
                continue;
            }

            
            std::cout << "Receiving file: " << filename << ", Size: " << fileSize << " bytes" << endl;
            
            char chunkBuffer[CHUNK_SIZE];
            streampos totalBytesReceived = 0;

            while (totalBytesReceived < fileSize) {
                int bytesRemaining = min(static_cast<int>(fileSize - totalBytesReceived), static_cast<int>(sizeof(chunkBuffer)));
                int bytes = recv(acceptSocket, chunkBuffer, bytesRemaining, 0);
                decrypt(chunkBuffer, CHUNK_SIZE, secret);
                if (bytes <= 0) {
                    std::cout << "Encountered error or client disconnected: " << WSAGetLastError() << endl;
                    break;
                }

                file.write(chunkBuffer, bytes);
                totalBytesReceived += bytes;

                // Optional: Show progress
                std::cout << "Received " << totalBytesReceived << "/" << fileSize << " bytes" << endl;
            }

            file.close();
            if (totalBytesReceived == fileSize) {
                std::cout << "File received and saved as: " << filename << endl;

                // Send confirmation
                if (send(acceptSocket, "Received file confirmation", 32, 0) == SOCKET_ERROR) {
                    std::cout << "Unable to process request " << WSAGetLastError() << endl;
                }
            }
            else {
                std::cout << "File transfer incomplete" << endl;
                send(acceptSocket, "File transfer failed", 32, 0);
            }
        }
        if (strcmp(requestRecvBuffer, "STOP") == 0) {
            cout << "Client disconnected." << endl;
            break;
        }
        
    }

    std::cout << "Server: Closing connection on thread: " << std::this_thread::get_id() << endl;
    closesocket(acceptSocket);
}

int main()
{

    //Step 1 => Initialize WSA
    std::cout << "----------STEP-1 => DLL SETUP------------" << endl;
    //Defining our sockets, socketServer (listening socket) and acceptServer (this will accept the clients request)

    SOCKET serverSocket;
    int port = 55555;
    //data structure containing information about Windows sockets implementation that will be populated by the 
    // WSAStartup function
    WSADATA wsaData;
    int wsaerr;
    //Windows socket version here we have to typecast to primitive data type WORD
    WORD wVersionRequested = MAKEWORD(2, 2); // meaning 2.2 version
    wsaerr = WSAStartup(wVersionRequested, &wsaData);
    if (wsaerr != 0) {
        std::cout << "Winsock dll not found" << endl;
        return 0;
    }
    else {
        std::cout << "Winsock dll found" << endl;
        std::cout << "status: " << wsaData.szSystemStatus << endl;
    }
    
    std::cout << "----------STEP-2 => CREATE SERVER SOCKET------------" << endl;
    serverSocket = INVALID_SOCKET;
    //af is address family here INET is IPv4, SOCK_STREAM is type here for TCP and IPPROTO_TCP is Protocol here TCP
    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        std::cout<< "Error while socket creation" << WSAGetLastError() <<endl;
        WSACleanup();
        return 0;
    }
    else {
        std::cout << "socket() is OK !" << endl;
    }

    std::cout << "----------STEP-3 => BINDING SERVER SOCKET------------" << endl;
    sockaddr_in service;
    service.sin_family = AF_INET;
    InetPton(AF_INET, _T("127.0.0.1"), &service.sin_addr.s_addr);
    service.sin_port = htons(port);

    if (::bind(serverSocket, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR) {
        std::cout << "Error while socket binding" << WSAGetLastError() << endl;
        closesocket(serverSocket);
        WSACleanup();
        return 0;
    }
    else {
        std::cout << "bind() is OK !" << endl;
    }

    std::cout << "----------STEP-4 => LISTENING FOR REQUESTS ------------" << endl;
    
    // Here we will only establish conenction with a single client at once
    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cout << "Error while listening on socket" << WSAGetLastError() << endl;
        closesocket(serverSocket);
        WSACleanup();
        return -1;
    }

    std::cout << "----------STEP-5 => ACCEPT REQUEST ------------" << endl;

    vector<thread> clientThreads;

    ThreadPool threadPool;
    threadPool.Start();

    while (true) {

        std::cout << "Waiting for a client..." << endl;
        //The accept function will pause the execution of the server till the client accepts the connection
        //2nd and 3rd arguments are addr and addrlen for client information (used if we want to connect to particular clients)
        //accept function spits out another SOCKET for handling the request while the serverSocket will be used for listening
        SOCKET acceptSocket = accept(serverSocket, NULL, NULL);
        if (acceptSocket == INVALID_SOCKET) {
            std::cout << "Error while accepting request" << WSAGetLastError() << endl;
            //If unable to accept this socket check for new connections rather than exiting
            continue;
        }
        //Could be using emplace instead of push (but not working here) because we would create the object 
        //in-memory rather than first making the obj and then creating a copy and pushing it into the vector
        //Why not std::thread(handleClient, acceptSocket)? Emplace gives a shortcut to directly pass the parameters
        
        //clientThreads.push_back(thread(handleClient, acceptSocket));
        threadPool.QueueTask([acceptSocket]() mutable {
            handleClient(acceptSocket);
            // Ensure the socket is closed when the task is done
            closesocket(acceptSocket);
            });
    }

    std::cout << "----------STEP-7 => CLOSE SERVER SOCKET ------------" << endl;

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}