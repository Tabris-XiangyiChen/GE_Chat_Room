#pragma once
#define _USE_MATH_DEFINES
#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_   
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>
#include <map>
#include "imgui.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include "net_protocol.h"
#include <fmod.hpp>
#include <fmod_errors.h>
#include <cmath>
#include <chrono>
#include <iostream>
#include <conio.h>
#include <algorithm>
#include <array>
#pragma comment(lib, "fmod_vc.lib")

struct ChatMessage {
    std::string sender;
    std::string target;
    std::string text;
    bool isPrivate;

    ChatMessage(const std::string& s = "", const std::string& t = "", bool priv = false, const std::string& tar = "")
        : sender(s), text(t), isPrivate(priv), target(tar) {
    }
};

enum class NetworkEventType {
    CONNECTED = 0,
    DISCONNECTED,
    PUBLIC_MESSAGE,
    PRIVATE_MESSAGE,
    USER_LIST_UPDATE
};

struct NetworkEvent {
    NetworkEventType type;
    std::string sender;
    std::string text;
    std::string target;
    std::vector<std::string> users;

    NetworkEvent() : type(NetworkEventType::CONNECTED) {}
    NetworkEvent(NetworkEventType t) : type(t) {}
};

class ChatWindow {
public:
    std::string username;
    std::vector<std::string> users_online;
    std::vector<ChatMessage> public_message;
    std::map<std::string, std::vector<ChatMessage>> private_chat;

    // input buffer
    char public_input[200];
    std::map<std::string, std::array<char, 200>> private_input;

    bool connected;

    // own socket
    SOCKET client_socket;
    std::thread recieve_thread;
    std::atomic<bool> running;
    std::queue<NetworkEvent> event_queue;

    FMOD::System* system;

    ChatWindow() {
        username = "";
        public_input[0] = '\0';
        connected = false;
        client_socket = INVALID_SOCKET;
        running = false;
        FMOD::System_Create(&system);
        system->init(512, FMOD_INIT_NORMAL, NULL);
    }
    ~ChatWindow() {
        close_connect();
    }

    bool connect_server(const std::string& ip, int port, const std::string& username);
    void close_connect();

    bool send_message_toserver(MessageType type, const void* data, int size) {

        MessageHeader header(type, size);

        if (send(client_socket, (char*)&header, sizeof(header), 0) == SOCKET_ERROR) {
            return false;
        }

        if (send(client_socket, (char*)data, size, 0) == SOCKET_ERROR) {
            return false;
        }

        return true;
    }

    void update_userlist(const std::vector<std::string>& users);

    void recive_message();
    void process_event();


    void render_all();
    void login_win();
    void chat_win();
    void private_win();
    void user_win();


    void play_music(NetworkEventType type);

    std::string get_username() const { return username; }
};