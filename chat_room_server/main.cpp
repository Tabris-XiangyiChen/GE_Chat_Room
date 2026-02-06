#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <string>
#include <algorithm>
#include "net_protocol.h"

#pragma comment(lib, "ws2_32.lib")

class ChatServer {
public:
    SOCKET server_socket;
    std::unordered_map<SOCKET, std::string> clients;
    std::mutex clients_mutex;
    bool running;

    //std::vector<std::thread> client_threads;

    ChatServer() : server_socket(INVALID_SOCKET), running(false) 
    {}

    bool init(int port) {

        // Step 1: Initialize WinSock
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup failed with error: " << WSAGetLastError() << std::endl;
            return false;
        }

        // create socket
        server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (server_socket == INVALID_SOCKET) 
        {
            std::cerr << "Socket creation failed" << std::endl;
            WSACleanup();
            return false;
        }

        // bind port and address
        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);
        serverAddr.sin_addr.s_addr = INADDR_ANY;

        if (bind(server_socket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) 
        {
            std::cerr << "Bind failed" << std::endl;
            closesocket(server_socket);
            WSACleanup();
            return false;
        }

        if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) 
        {
            std::cerr << "Listen failed" << std::endl;
            closesocket(server_socket);
            WSACleanup();
            return false;
        }

        running = true;
        std::cout << "Chat Server started on port " << port << std::endl;

        std::thread acceptThread(&ChatServer::accept_client, this);
        // detach thread, when close the server, the while loop
        // in thread will break, thread will be release
        acceptThread.detach();

        return true;
    }

    void close() {
        running = false;

        // close  linsten socket
        if (server_socket != INVALID_SOCKET) 
        {
            closesocket(server_socket);
            server_socket = INVALID_SOCKET;
        }

        // close all clients
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            for (auto& client : clients) 
            {
                closesocket(client.first);
            }
            clients.clear();
        }

        WSACleanup();
        std::cout << "Server stopp" << std::endl;
    }

    void accept_client() 
    {
        while (running) {
            sockaddr_in client_address;
            int len = sizeof(client_address);

            SOCKET client_socket = accept(server_socket, (sockaddr*)&client_address, &len);
            if (client_socket == INVALID_SOCKET) 
            {
                if (running) 
                    std::cerr << "Accept failed" << std::endl;
                continue;
            }

            char clientIP[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_address.sin_addr, clientIP, INET_ADDRSTRLEN);
            std::cout << "Accept connection from: " << clientIP << ":" << ntohs(client_address.sin_port) << std::endl;

            // create client thread
            std::thread clientThread(&ChatServer::handle_client, this, client_socket);
            clientThread.detach();
        }
    }

    void broadcast_userlist() {
        std::lock_guard<std::mutex> lock(clients_mutex);
        // if no user
        if (clients.empty()) 
            return;

        UserListMessage user_list;
        user_list.user_count = 0;

        // collect all user name
        for (const auto& client : clients) 
        {
            if (user_list.user_count < 32) 
            {
                strncpy_s(user_list.users[user_list.user_count], client.second.c_str(), sizeof(user_list.users[user_list.user_count]) - 1);
                user_list.users[user_list.user_count][sizeof(user_list.users[user_list.user_count]) - 1] = '\0';
                user_list.user_count++;
            }
        }

        // broadcast to all user
        MessageHeader header(MessageType::USER_LIST_UPDATE, sizeof(UserListMessage));

        for (const auto& client : clients) 
        {
            send(client.first, (char*)&header, sizeof(header), 0);
            send(client.first, (char*)&user_list, sizeof(user_list), 0);
        }
    }

    void handle_client(SOCKET client_socket) {
        std::string username;
        MessageHeader header;

        if (receive_message(client_socket, (char*)&header, sizeof(header)) != sizeof(header)) 
        {
            std::cout << "Failed to receive header" << std::endl;
            close_client(client_socket, username);
            return;
        }

        if (header.type != MessageType::CLIENT_CONNECT) 
        {
            close_client(client_socket, username);
            return;
        }

        ClientConnectMessage connect_message;
        if (receive_message(client_socket, (char*)&connect_message, sizeof(connect_message)) != sizeof(connect_message)) 
        {
            std::cout << "Failed to receive connect message" << std::endl;
            close_client(client_socket, username);
            return;
        }

        username = connect_message.username;
        // create client
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            clients[client_socket] = username;
        }

        std::cout << "User " << username << " joined the room" << std::endl;

        // send to new user
        //
        send_userlist(client_socket);
        // send a public message to all user
        PublicMessage message("System", username + " joined the chat");
        MessageHeader send_header(MessageType::PUBLIC_MESSAGE, sizeof(PublicMessage));

        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            for (const auto& client : clients) {
                // not send to myself
                if (client.second != username) {
                    send(client.first, (char*)&send_header, sizeof(send_header), 0);
                    send(client.first, (char*)&message, sizeof(message), 0);
                }
            }
        }
        broadcast_userlist();

        while (running) {

            if (receive_message(client_socket, (char*)&header, sizeof(header)) != sizeof(header)) 
            {
                std::cout << "Client '" << username << "' disconnected" << std::endl;
                break;
            }

            if (header.type == MessageType::PUBLIC_MESSAGE) 
            {

                PublicMessage message;

                if (receive_message(client_socket, (char*)&message, sizeof(message)) != sizeof(message)) {
                    std::cout << "Failed to receive public message from " << username << std::endl;
                    break;
                }

                std::cout << "Public message from " << message.sender << ": " << message.content << std::endl;

                MessageHeader header(MessageType::PUBLIC_MESSAGE, sizeof(PublicMessage));

                for (const auto& client : clients) {
                    send(client.first, (char*)&header, sizeof(header), 0);
                    send(client.first, (char*)&message, sizeof(message), 0);
                }
            }
            else if (header.type == MessageType::PRIVATE_MESSAGE) {

                PrivateMessage message;

                if (receive_message(client_socket, (char*)&message, sizeof(message)) != sizeof(message)) {
                    std::cout << "Failed to receive private message from " << username << std::endl;
                    break;
                }

                std::cout << "Private message from " << message.sender << " to " << message.target << std::endl;

                std::lock_guard<std::mutex> lock(clients_mutex);

                // search target
                SOCKET targetSocket = INVALID_SOCKET;
                for (const auto& client : clients) {
                    if (client.second == message.target) {
                        targetSocket = client.first;
                        break;
                    }
                }

                if (targetSocket != INVALID_SOCKET) {
                    MessageHeader header(MessageType::PRIVATE_MESSAGE, sizeof(PrivateMessage));
                    send(targetSocket, (char*)&header, sizeof(header), 0);
                    send(targetSocket, (char*)&message, sizeof(message), 0);
                }
            }
            else if (header.type == MessageType::CLIENT_DISCONNECT) {
                std::cout << "Client " << username << " requested disconnect" << std::endl;
                break;
            }
            else {
                std::cout << " unknown " << username << std::endl;
            }
        }

        close_client(client_socket, username);
    }


    void close_client(SOCKET clientSocket, const std::string& username) {
        if (!username.empty()) {

            {
                std::lock_guard<std::mutex> lock(clients_mutex);
                clients.erase(clientSocket);
            }

            std::cout << "User '" << username << "' left the chat" << std::endl;

            MessageHeader header(MessageType::PUBLIC_MESSAGE, sizeof(PublicMessage));
            PublicMessage message("System", username + " left the chat");

            for (const auto& client : clients) 
            {
                send(client.first, (char*)&header, sizeof(header), 0);
                send(client.first, (char*)&message, sizeof(message), 0);
            }
            broadcast_userlist();
        }

        closesocket(clientSocket);
    }

    int receive_message(SOCKET socket, char* buffer, int size) {
        int totalReceived = 0;
        while (totalReceived < size) {
            int received = recv(socket, buffer + totalReceived, size - totalReceived, 0);
            if (received <= 0) {
                return received;
            }
            totalReceived += received;
        }
        return totalReceived;
    }

    void send_userlist(SOCKET target) {
        std::lock_guard<std::mutex> lock(clients_mutex);

        UserListMessage list;
        list.user_count = 0;

        // collect all username
        for (const auto& client : clients) 
        {
            strncpy_s(list.users[list.user_count], client.second.c_str(), sizeof(list.users[list.user_count]) - 1);
            list.users[list.user_count][sizeof(list.users[list.user_count]) - 1] = '\0';
            list.user_count++;
        }

        MessageHeader header(MessageType::USER_LIST_UPDATE, sizeof(UserListMessage));

        send(target, (char*)&header, sizeof(header), 0);
        send(target, (char*)&list, sizeof(list), 0);
    }

};

int main() {
    std::cout << "Chat Server" << std::endl;
    std::cout << "Starting server on port 65432" << std::endl;

    ChatServer server;
    if (!server.init(65432)) {
        std::cout << "Start server failed" << std::endl;
        return 1;
    }

    std::cin.get();

    server.close();
    return 0;
}