#pragma once
#include "ChatWindow.h"
#define WIN32_LEAN_AND_MEAN
//#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

void ChatWindow::recive_message() {
    while (running && client_socket != INVALID_SOCKET) {
        MessageHeader header;

        int received = recv(client_socket, (char*)&header, sizeof(header), 0);
        if (received <= 0) {
            NetworkEvent event(NetworkEventType::DISCONNECTED);

            event_queue.push(event);
            break;
        }

        NetworkEvent event;

        switch (header.type) {
        case MessageType::PUBLIC_MESSAGE: 
        {
            PublicMessage message;
            recv(client_socket, (char*)&message, sizeof(message), 0);

            event.type = NetworkEventType::PUBLIC_MESSAGE;
            event.sender = message.sender;
            event.text = message.content;
            break;
        }

        case MessageType::PRIVATE_MESSAGE: 
        {
            PrivateMessage private_message;
            recv(client_socket, (char*)&private_message, sizeof(private_message), 0);

            event.type = NetworkEventType::PRIVATE_MESSAGE;
            event.sender = private_message.sender;
            event.target = private_message.target;
            event.text = private_message.content;
            break;
        }
       

        case MessageType::USER_LIST_UPDATE: 
        {
            UserListMessage userlist;
            recv(client_socket, (char*)&userlist, sizeof(userlist), 0);

            event.type = NetworkEventType::USER_LIST_UPDATE;
            for (int i = 0; i < userlist.user_count; i++) {
                event.users.push_back(userlist.users[i]);
            }
            break;
        }
        

        default:
            continue;
        }


        {
            std::lock_guard<std::mutex> lock(event_mutex);
            event_queue.push(event);
        }
        // if not my message,paly sound
        if (event.sender != username)
            ChatWindow::play_music(event.type);
    }
}

// paly message music
void ChatWindow::play_music(NetworkEventType type)
{
    std::thread([this, type]() {
        FMOD::Sound* sound = nullptr;
        FMOD::Channel* channel = nullptr;

        switch (type) {
        case NetworkEventType::PUBLIC_MESSAGE:
            system->createSound("music/public.mp3", FMOD_DEFAULT, nullptr, &sound);
            break;

        case NetworkEventType::PRIVATE_MESSAGE:
            system->createSound("music/private.mp3", FMOD_DEFAULT, nullptr, &sound);
            break;
        default:
            return;
        }

        system->playSound(sound, nullptr, false, &channel);

        std::this_thread::sleep_for(std::chrono::seconds(1));

        channel->stop();
        sound->release();
        }).detach();
}

bool ChatWindow::connect_server(const std::string& ip, int port, const std::string& user_name) {
    // Step 1: Initialize WinSock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed with error: " << WSAGetLastError() << std::endl;
        return false;
    }

    // Step 2: Create a socket
    client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client_socket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed with error: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return false;
    }

    // set server address
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr) <= 0) 
    {
        std::cout << "Invalid address" << std::endl;
        if (client_socket != INVALID_SOCKET) {
            closesocket(client_socket);
            client_socket = INVALID_SOCKET;
        }
        WSACleanup();
        return false;
    }

    // connect server
    if (connect(client_socket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) 
    {
        std::cout << "connect failed" << std::endl;
        if (client_socket != INVALID_SOCKET) {
            closesocket(client_socket);
            client_socket = INVALID_SOCKET;
        }
        WSACleanup();
        return false;
    }

    ClientConnectMessage connect_message(user_name);

    if (!send_message_toserver(MessageType::CLIENT_CONNECT, &connect_message, sizeof(connect_message))) {
        std::cerr << "Failed to send connect message" << std::endl;
        if (client_socket != INVALID_SOCKET) {
            closesocket(client_socket);
            client_socket = INVALID_SOCKET;
        }
        WSACleanup();
        return false;
    }

    username = user_name;
    connected = true;
    running = true;

    // receive thread
    recieve_thread = std::thread(&ChatWindow::recive_message, this);

    public_message.push_back(ChatMessage("System", "Connect to chat server"));
    return true;
}

void ChatWindow::close_connect() {
    if (!connected) return;

    running = false;

    // send disconnect message
    if (client_socket != INVALID_SOCKET) {
        MessageHeader header(MessageType::CLIENT_DISCONNECT, 0);
        send(client_socket, (char*)&header, sizeof(header), 0);
    }

    // wait thread
    if (recieve_thread.joinable()) {
        recieve_thread.join();
    }

    // clear socket
    if (client_socket != INVALID_SOCKET) {
        closesocket(client_socket);
        client_socket = INVALID_SOCKET;
    }
    WSACleanup();

    connected = false;

    public_message.push_back(ChatMessage("System", "Disconnected from server"));
}

void ChatWindow::update_userlist(const std::vector<std::string>& users) {
    users_online.clear();

    // add all users
    for (const auto& user : users) {
        if (!user.empty()) {
            users_online.push_back(user);
        }
    }

    // add my username
    bool found = false;
    for (const auto& user : users_online) {
        if (user == username) {
            found = true;
            break;
        }
    }
    if (!found) {
        users_online.push_back(username);
    }
    

    std::cout << "update userlist" << std::endl;
}


// process different type events
void ChatWindow::process_event() {

    while (!event_queue.empty()) {
        // get event
        NetworkEvent event;
        {
            std::lock_guard<std::mutex> lock(event_mutex);
            event = event_queue.front();
            event_queue.pop();
        }

        switch (event.type) {
        case NetworkEventType::DISCONNECTED:
            connected = false;
            close_connect();
            public_message.push_back(ChatMessage("System", "Connect lost"));
            // clear all users
            users_online.clear();
            if (!username.empty()) {
                users_online.push_back(username);
            }
            break;

        case NetworkEventType::PUBLIC_MESSAGE:
            if (event.sender != username) 
            public_message.push_back(ChatMessage(event.sender, event.text));
            break;

        case NetworkEventType::PRIVATE_MESSAGE:
        {
            std::string name = (event.sender == username) ? event.target : event.sender;

            // check private chat map
            if (private_chat.find(name) == private_chat.end())
            {
                private_chat[name] = std::vector<ChatMessage>();
            }

            // add chat message
            ChatMessage mess(event.sender, event.text, true, event.target);
            private_chat[name].push_back(mess);

            if (private_input.find(name) == private_input.end())
            {
                private_input[name].fill('\0');
            }
        }
            break;

        case NetworkEventType::USER_LIST_UPDATE:
            // when someone link the server
            update_userlist(event.users);
            break;

        default:
            break;
        }
    }
}

void ChatWindow::user_win() {
    ImGui::BeginChild("Users", ImVec2(150, 0), true);

    ImGui::TextColored(ImVec4(0, 1, 0, 1), "Online (%d):", (int)users_online.size());
    ImGui::Separator();

    for (const auto& user : users_online) {
        if (user == username) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "> %s (You)", user.c_str());
        }
        else {
            bool hasPrivateChat = (private_chat.find(user) != private_chat.end());

            if (ImGui::Selectable(user.c_str())) {
                // open chat
                if (private_chat.find(user) == private_chat.end()) {
                    private_chat[user] = std::vector<ChatMessage>();
                    private_input[user].fill('\0');
                }
            }

            if (hasPrivateChat) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1, 0.5f, 1, 1), " *");
            }
        }
    }

    ImGui::EndChild();
}

void ChatWindow::chat_win() {

    ImGui::BeginChild("Chat messages", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.5f), true);

    for (const auto& p_message : public_message) {
        // set user color
        if (p_message.sender == username) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
        }
        else if (p_message.sender == "System") {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
        }
        else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.2f, 1.0f));
        }

        ImGui::Text("%s:", p_message.sender.c_str());
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::TextWrapped("%s", p_message.text.c_str());

        ImGui::Spacing();
    }

    ImGui::EndChild();

    // input
    ImGui::Separator();

    ImGui::Text("Message:");
    ImGui::SameLine();

    ImGui::PushItemWidth(-60);
    bool sendPublic = false;
    // press endter
    if (ImGui::InputText("##PublicInput", public_input, sizeof(public_input), ImGuiInputTextFlags_EnterReturnsTrue)) 
    {
        sendPublic = true;
    }
    ImGui::PopItemWidth();

    ImGui::SameLine();
    if (ImGui::Button("Send", ImVec2(50, 0)) || sendPublic) {
        if (strlen(public_input) > 0) {
            // send
            PublicMessage message(username, public_input);

            if (send_message_toserver(MessageType::PUBLIC_MESSAGE, &message, sizeof(message))) {
                // show at local
                ChatMessage mess(username, public_input);
                public_message.push_back(mess);
            }
            // clear input
            public_input[0] = '\0';
        }
    }
}

void ChatWindow::private_win() {
    for (auto it = private_chat.begin(); it != private_chat.end();) {
        const std::string targetUser = it->first;
        std::vector<ChatMessage>& messages = it->second;

        std::string title = "Private: " + targetUser;
        bool isopen = true;

        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
        // imgui can renmber last window's detail by title
        if (ImGui::Begin(title.c_str(), &isopen)) {

            ImGui::BeginChild("PrivateMessages", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.5f), true);

            for (const auto& mes : messages) {
                // color
                if (mes.sender == username) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.1f, 0.5f, 1.0f));
                }
                else {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.1f, 0.5f, 0.1f, 1.0f));
                }

                ImGui::Text("%s:", mes.sender.c_str());
                ImGui::PopStyleColor();

                ImGui::SameLine();
                ImGui::TextWrapped("%s", mes.text.c_str());

                ImGui::Spacing();
            }

            ImGui::EndChild();

            // input
            ImGui::Separator();
            ImGui::Text("To %s:", targetUser.c_str());
            ImGui::SameLine();

            auto& input_buff = private_input[targetUser];

            ImGui::PushItemWidth(-60);
            bool send = false;
            // press endter
            if (ImGui::InputText("##PrivateInput", input_buff.data(), input_buff.size(),ImGuiInputTextFlags_EnterReturnsTrue)) 
            {
                send = true;
            }
            ImGui::PopItemWidth();

            ImGui::SameLine();
            if (ImGui::Button("Send", ImVec2(50, 0)) || send) {
                if (strlen(input_buff.data()) > 0) {
                    PrivateMessage message(username, targetUser, input_buff.data());

                    if (send_message_toserver(MessageType::PRIVATE_MESSAGE, &message, sizeof(message))) {
                        std::string name = (username == username) ? targetUser : username;

                        // check private chat map
                        if (private_chat.find(name) == private_chat.end())
                        {
                            private_chat[name] = std::vector<ChatMessage>();
                        }

                        // add chat message
                        ChatMessage mess(username, input_buff.data(), true, targetUser);
                        private_chat[name].push_back(mess);

                        if (private_input.find(name) == private_input.end())
                        {
                            private_input[name].fill('\0');
                        }
                    }
                    input_buff.fill('\0');
                }
            }
        }

        ImGui::End();

        // if close the chat, cler the chat
        if (!isopen) {
            it = private_chat.erase(it);
            private_input.erase(targetUser);
        }
        else {
            ++it;
        }
    }
}

void ChatWindow::login_win() {
    ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(500, 500), ImGuiCond_FirstUseEver);
    bool connect = false;

    if (ImGui::Begin("Connect to Chat Server", nullptr, ImGuiWindowFlags_NoCollapse)) {

        static char username[64] = "Fox";
        static char serverIP[64] = "127.0.0.1";
        static char port[32] = "65432";

        ImGui::Text("Username:");
        ImGui::SameLine(200);
        if (ImGui::InputText("##username", username, sizeof(username), ImGuiInputTextFlags_EnterReturnsTrue))
            connect = true;
        ImGui::Text("Server IP:");
        ImGui::SameLine(200);
        if (ImGui::InputText("##server", serverIP, sizeof(serverIP), ImGuiInputTextFlags_EnterReturnsTrue))
            connect = true;

        ImGui::Text("Port:");
        ImGui::SameLine(200);
        if (ImGui::InputText("##port", port, sizeof(port), ImGuiInputTextFlags_EnterReturnsTrue))
            connect = true;


        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Connect", ImVec2(120, 30)) || connect) {
            int portNum = atoi(port);
            if (strlen(username) > 0 && portNum > 0 && serverIP != "") {
                if (connect_server(serverIP, portNum, username)) {
                }
                else {
                    public_message.push_back(ChatMessage("System", "Connection failed"));
                }
            }
        }

        ImGui::SameLine();


        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1, 0, 1, 1), "Tips:");
        ImGui::Text("1. Run the server on port 65432");
        ImGui::Text("2. Use different names for multiple clients");
        ImGui::Text("3. Click on user names to start private chat");
    }
    ImGui::End();
}

void ChatWindow::render_all() {

    process_event();

    if (!connected) {
        // login
        login_win();

        // Display some public messages
        ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(600, 350), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("notification", nullptr, ImGuiWindowFlags_NoCollapse)) {

            ImGui::BeginChild("Messages", ImVec2(0, 0), true);

            for (const auto& message : public_message) {
                if (message.sender == "System") {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
                }
                else 
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                }

                ImGui::Text("%s: %s", message.sender.c_str(), message.text.c_str());
                ImGui::PopStyleColor();
            }

            ImGui::EndChild();
        }

        ImGui::End();
    }
    else {
        // main
        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
        ImGui::Begin("Chat Room", nullptr, ImGuiWindowFlags_NoCollapse);

        // user
        user_win();
        ImGui::SameLine();

        // chat
        ImGui::BeginGroup();
        chat_win();
        ImGui::EndGroup();

        ImGui::End();

        // private
        private_win();
    }
}