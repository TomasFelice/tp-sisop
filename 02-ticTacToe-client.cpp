// cliente.cpp
#include <iostream>
#include <thread>
#include <string>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_PORT 5000
#define SERVER_IP "127.0.0.1"
#define BUFFER_SIZE 1024

void receiveHandler(int sockfd) {
    char buffer[BUFFER_SIZE];
    while (true) {
        ssize_t bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) {
            std::cout << "\n[INFO] Desconectado del servidor.\n";
            close(sockfd);
            exit(0);
        }
        buffer[bytes] = '\0';
        std::cout << buffer << std::flush;
    }
}

int main() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in servAddr{};
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &servAddr.sin_addr);

    if (connect(sockfd, (sockaddr*)&servAddr, sizeof(servAddr)) < 0) {
        perror("connect");
        return 1;
    }

    std::cout << "Conectado al servidor en " << SERVER_IP << ":" << SERVER_PORT << "\n";
    std::thread receiver(receiveHandler, sockfd);

    std::string input;
    while (true) {
        std::getline(std::cin, input);
        if (input == "") continue;
        input += "\n";
        if (send(sockfd, input.c_str(), input.length(), 0) <= 0) {
            std::cerr << "[ERROR] No se pudo enviar al servidor\n";
            break;
        }
        if (input == "QUIT\n") {
            std::cout << "[INFO] Desconectando...\n";
            break;
        }
    }

    receiver.detach(); // termina automáticamente si el servidor ya cerró
    close(sockfd);
    return 0;
}
