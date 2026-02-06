#pragma once
#include <string>
#include <cstdint>
#include <cstring>

enum class MessageType {
    // connect to the server
    CLIENT_CONNECT = 1,
    CLIENT_DISCONNECT = 2,
    // public and private message
    PUBLIC_MESSAGE = 3,
    PRIVATE_MESSAGE = 4,
    // userlist message for update the online user
    USER_LIST_UPDATE = 5,
};

// message header, send this before send the message content
struct MessageHeader {
    MessageType type;

    MessageHeader() : type(MessageType::CLIENT_CONNECT) {}
    MessageHeader(MessageType t, unsigned int s) : type(t) {}
};

struct ClientConnectMessage {
    char username[32];

    ClientConnectMessage()
    {
        memset(username, 0, sizeof(username));
    }

    ClientConnectMessage(const std::string& name)
    {
        memset(username, 0, sizeof(username));
        // safe copy, _TRUNCATE can add \0 atomicly
        strncpy_s(username, sizeof(username), name.c_str(), _TRUNCATE);
    }
};

struct PublicMessage {
    char sender[32];
    char content[256];

    PublicMessage() {
        memset(sender, 0, sizeof(sender));
        memset(content, 0, sizeof(content));
    }

    PublicMessage(const std::string& s, const std::string& c) {
        memset(sender, 0, sizeof(sender));
        memset(content, 0, sizeof(content));
        strncpy_s(sender, sizeof(sender), s.c_str(), _TRUNCATE);
        strncpy_s(content, sizeof(content), c.c_str(), _TRUNCATE);
    }
};

struct PrivateMessage {
    char sender[32];
    char target[32];
    char content[256];

    PrivateMessage() {
        memset(sender, 0, sizeof(sender));
        memset(target, 0, sizeof(target));
        memset(content, 0, sizeof(content));
    }

    PrivateMessage(const std::string& s, const std::string& t, const std::string& c) {
        memset(sender, 0, sizeof(sender));
        memset(target, 0, sizeof(target));
        memset(content, 0, sizeof(content));
        strncpy_s(sender, sizeof(sender), s.c_str(), _TRUNCATE);
        strncpy_s(target, sizeof(target), t.c_str(), _TRUNCATE);
        strncpy_s(content, sizeof(content), c.c_str(), _TRUNCATE);
    }
};

// user list
struct UserListMessage {
    int user_count;
    char users[32][32];

    UserListMessage() {
        user_count = 0;
        for (int i = 0; i < 32; i++) {
            memset(users[i], 0, sizeof(users[i]));
        }
    }
};
