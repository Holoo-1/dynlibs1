#include <winsock2.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <cstring>
#include <nlohmann/json.hpp>
#include <vector>
#include <future>


#pragma comment(lib, "ws2_32.lib")

#define THREADSNUM 1

using namespace std;
using json = nlohmann::json;

static mutex m;

void calculate_mult_thread(int** matrix, int start, int end, int MATRIX_SIZE, std::promise<bool>& promise) {
    this_thread::sleep_for(std::chrono::seconds(3));

    // Load // dll_max_coll_num_dyn.dll  // Mult_coll.dll
    HMODULE hDLL = LoadLibrary(L"dll_max_coll_num_dyn.dll");
    if (hDLL == NULL) {
        printf("load error");
    }

    typedef int(*calccoll)(int[], int);
    calccoll calculate = (calccoll)GetProcAddress(hDLL, "calculate");
    if (calculate == NULL) {
        printf("func error");
    }

    // Calculate
    int* diagonal = new int[MATRIX_SIZE];
    for (int i = 0; i < MATRIX_SIZE; i++) {
        int* column = new int[MATRIX_SIZE];
        for (int j = 0; j < MATRIX_SIZE; j++) {
            column[j] = matrix[j][i];
        }

        diagonal[i] = calculate(column, MATRIX_SIZE);

        delete[] column;
    }

    for (int i = start; i < end; i++) {
        lock_guard<mutex> lock(m);
        matrix[MATRIX_SIZE - i - 1][i] = diagonal[i];
    }

    FreeLibrary(hDLL);

    promise.set_value(true);
}

void print_matrix( int** matrix, int MATRIX_SIZE) {
    for (int i = 0; i < MATRIX_SIZE; i++) {
        for (int j = 0; j < MATRIX_SIZE; j++) {
            std::cout << matrix[i][j] << "\t";
        }
        std::cout << "\n";
    }
    std::cout << "\n";
}

void stripHeader(string& receivedData) {
    int nullCounter = 0;
    //printf("HEADER REMOVED");
    for (int i = 0; i < 4; i++) {
        if (receivedData[i] == '\0') {
            nullCounter++;
            //cout << nullCounter << " - nullCounter" << endl;
            if (nullCounter == 3) {
                receivedData = receivedData.substr(4);
                return;
            }
        }
    }
}

json matrixToJson( int** matrix, int MATRIX_SIZE) {
    json j;
    for (int i = 0; i < MATRIX_SIZE; i++) {
        json row;
        for (int j = 0; j < MATRIX_SIZE; j++) {
            row.push_back(matrix[i][j]);
        }
        j.push_back(row);
    }
    return j.dump();
}

void handleClient(SOCKET clientSocket) {
    std::cout << "__GOT NEW CLIENT__" << endl;

    // Receive the data size
    uint32_t expected_data_size;
    int size_recv = recv(clientSocket, reinterpret_cast<char*>(&expected_data_size), sizeof(expected_data_size), 0);
    if (size_recv <= 0) {
        std::cerr << "Failed to receive data size" << endl;
        closesocket(clientSocket);
        return;
    }

    // Loop until all data is received
    std::string receivedData;
    while (receivedData.size() < expected_data_size) {
        char buffer[4096];
        int receivedBytes = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (receivedBytes == SOCKET_ERROR || receivedBytes == 0) {
            std::cerr << "Failed to receive data. matrix" << endl;
            closesocket(clientSocket);
            return;
        }

        receivedData.append(buffer, receivedBytes);
    }


    stripHeader(receivedData);
    //printf(&receivedData[0]);

    int matrixSize;
    json matrixData;
    int** matrix = {};
    bool got_data = 0;

    json jsonData;
    //check for json data
    if (receivedData[0] == '{') {
        try {
            jsonData = json::parse(receivedData);
        }
        catch (const exception& e) {
            std::cerr << "Failed to parse JSON data: " << e.what() << endl;
            closesocket(clientSocket);
            return;
        }

        matrixSize = jsonData["size"];
        matrixData = jsonData["matrix"];

        matrix = new int* [matrixSize];
        for (int i = 0; i < matrixSize; i++) {
            matrix[i] = new int[matrixSize];
            for (int j = 0; j < matrixSize; j++) {
                matrix[i][j] = matrixData[i][j];
            }
        }

        printf("Initial matrix:\n");
        print_matrix(matrix, matrixSize);
        got_data = 1;
    }

    std::promise<bool> promise;
    std::future<bool> future = promise.get_future();

    while (true) {

        const int COMM_BUFFER_SIZE = 16;
        char comm_buffer[COMM_BUFFER_SIZE];
        auto receivedBytes_com = recv(clientSocket, comm_buffer, COMM_BUFFER_SIZE, 0);
        if (receivedBytes_com == SOCKET_ERROR) {
            std::cerr << "Failed to receive command" << endl;
            closesocket(clientSocket);
            return;
        }
        string receivedCommand = {};

        receivedCommand.append(comm_buffer, receivedBytes_com);
        printf(&receivedCommand[0], comm_buffer);
        printf("_");

        if (got_data != 1) {
            printf("GOT NO DATA");
            return;
        }

        string word2 = "BEGIN";
        if (receivedCommand.find(word2) != std::string::npos) {
            printf("CALCULATIONS BEGUN\n");

            thread threads[THREADSNUM];

            int chunkSize = matrixSize / THREADSNUM;
            int start = 0;
            int end = chunkSize;

            for (int i = 0; i < THREADSNUM; i++) {
                int start = i * chunkSize;
                if (i == THREADSNUM - 1) {
                    end = matrixSize;
                }
                else {
                    end = start + chunkSize;
                }

                threads[i] = thread(calculate_mult_thread, matrix, start, end, matrixSize, std::ref(promise));
                threads[i].detach();
            }
        }

        string word = "STATUS";
        string msg = "in_progress";
        string msg_done = "completed";
        std::future_status status = future.wait_for(std::chrono::seconds(1));

        if (receivedCommand.find(word) != std::string::npos) {
            receivedCommand.clear();
            // Send status update to the client in a separate thread
            if (status != future_status::ready) {

                // Send status to client
                int sendResult = send(clientSocket, msg.c_str(), msg.size() + 1, 0);
                if (sendResult == SOCKET_ERROR) {
                    std::cerr << "Failed to send status update" << endl;
                    closesocket(clientSocket);
                    return;
                }
            }
            else {
                // Send completion status to client
                int sendResult = send(clientSocket, msg_done.c_str(), msg_done.size() + 1, 0);
                if (sendResult == SOCKET_ERROR) {
                    std::cerr << "Failed to send completion status" << endl;
                    closesocket(clientSocket);
                    return;
                }
            }
        }

        string word3 = "GET";
        if (receivedCommand.find(word3) != std::string::npos) {
            if (status != future_status::ready) {
                printf("Received GET too early\n");
                continue;
            }
            json response;
            string responseStr;

            response["size"] = matrixSize;
            response["matrix"] = matrixToJson(matrix, matrixSize);

            responseStr = response.dump();

            uint32_t dataSize = htonl(responseStr.size());
            int sendSizeResult = send(clientSocket, reinterpret_cast<char*>(&dataSize), sizeof(dataSize), 0);
            if (sendSizeResult == SOCKET_ERROR) {
                std::cerr << "Failed to send data size" << endl;
                closesocket(clientSocket);
                return;
            }

            int sendResult = send(clientSocket, responseStr.c_str(), responseStr.size(), 0);
            if (sendResult == SOCKET_ERROR) {
                std::cerr << "Failed to send modified matrix" << endl;
                closesocket(clientSocket);
                return;
            }

            printf("Modified matrix sent:\n");
            print_matrix(matrix, matrixSize);
            return;
        }
    }

    closesocket(clientSocket);
}

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Failed to initialize Winsock." << endl;
        return 1;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "Failed to create server socket." << endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(12345);

    if (::bind(serverSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR) {
        std::cerr << "Failed to bind server socket." << endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }
    //somaxconn = max number of clients
    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Failed to listen on server socket." << endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "Server started. Waiting for client connections..." << endl;

    const int MAX_CLIENTS = 2;
    std::thread clientThread[MAX_CLIENTS];
    int numThreads = 0;

    while (true) {
        SOCKET clientSocket = accept(serverSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "Failed to accept client connection." << endl;
            closesocket(serverSocket);
            WSACleanup();
            return 1;
        }

        printf("Client connected.");
        clientThread[numThreads] = std::thread(handleClient, clientSocket);
        numThreads++;
        std::cout << "thread_id:" << clientThread[numThreads].get_id() << endl;

    }

    for (int i = 0; i < numThreads; i++) {
        clientThread[i].join();
    }
    printf("closesocket(serverSocket) \n");
    closesocket(serverSocket);
    WSACleanup();

    return 0;
}
