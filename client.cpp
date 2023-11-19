#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <iostream>
#include <termios.h>

#define BUF 1024
#define PORT 6543

int read_input(char* buffer, std::string message)
{
    if (strcmp(buffer, "QUIT")) {
        return 1;
    }
    message.append(buffer);
    std::cout << "message: " << message << std::endl;
    return 0;
}

// Disable terminal echo
void disableEcho() {
    struct termios t;
    tcgetattr(STDIN_FILENO, &t);
    t.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

// Enable terminal echo
void enableEcho() {
    struct termios t;
    tcgetattr(STDIN_FILENO, &t);
    t.c_lflag |= ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

int main(int argc, char** argv)
{
    int create_socket;
    char buffer[BUF];
    struct sockaddr_in address;
    int size;
    int isQuit;
    //int sending = 0;
    std::string message = "";

    // IPv4, TCP (connection oriented), IP (same as server)
    if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket error");
        return EXIT_FAILURE;
    }
    // INIT ADDRESS
    // Attention: network byte order => big endian
    memset(&address, 0, sizeof(address)); // init storage with 0
    address.sin_family = AF_INET;         // IPv4
    address.sin_port = htons(PORT);
    if (argc < 2) {
        inet_aton("127.0.0.1", &address.sin_addr);
    } else {
        inet_aton(argv[1], &address.sin_addr);
    }

    // CREATE A CONNECTION
    if (connect(create_socket,
            (struct sockaddr*) &address,
            sizeof(address)) == -1) {
        perror("Connect error - no server available");
        return EXIT_FAILURE;
    }

    // ignore return value of printf
    printf("Connection with server (%s) established\n",
            inet_ntoa(address.sin_addr));

    // RECEIVE DATA
    size = recv(create_socket, buffer, BUF - 1, 0);
    if (size == -1) {
        perror("recv error");
    } else if (size == 0) {
        printf("Server closed remote socket\n"); // ignore error
    } else {
        buffer[size] = '\0';
        printf("%s", buffer); // ignore error
    }
    
    do {
        std::string line;
        printf("Command: ");
        std::getline(std::cin, line);
        std::string command = line;

        if (command == "LOGIN"){
            message = "LOGIN\n";
            printf("Username: ");
            std::getline(std::cin, line);
            message += line + "\n";

            disableEcho(); // Disable echo to hide password
            printf("Password: ");
            std::getline(std::cin, line);
            message += line + "\n";
            enableEcho(); // Enable echo again
        } else if (command == "SEND") {
            message = "SEND\n";
            // printf("Sender: ");
            // std::getline(std::cin, line);
            // message += line + "\n";

            printf("Receiver: ");
            std::getline(std::cin, line);
            message += line + "\n";

            printf("Subject: ");
            std::getline(std::cin, line);
            message += line + "\n";

            printf("Message: ");
            while (std::getline(std::cin, line) && line != ".") {
                message += line + "\n";
            }
            message += ".\n";
        } else if (command == "LIST") {
            message = "LIST\n";
            /*
            printf("Username: ");
            std::getline(std::cin, line);
            message += line + "\n";
            */
        } else if (command == "READ") {
            message = "READ\n";
            /*
            printf("Username: ");
            std::getline(std::cin, line);
            message += line + "\n";
            */
            printf("Message-Number: ");
            std::getline(std::cin, line);
            message += line + "\n";
        } else if (command == "DEL") {
            message = "DEL\n";
            /*
            printf("Username: ");
            std::getline(std::cin, line);
            message += line + "\n";
            */
            printf("Message-Number: ");
            std::getline(std::cin, line);
            message += line + "\n";
        } else if (command == "QUIT") {
            message = "QUIT\n";
        } else {
            printf("Unknown command. Please try again.\n");
            continue;
        }

        int size = message.length();
        isQuit = command == "QUIT";

        // SEND LENGTH OF DATA
        if ((send(create_socket, &size, sizeof(int), 0)) == -1) {
            perror("send error");
            break;
        }

        // SEND DATA
        if ((send(create_socket, message.c_str(), message.length(), 0)) == -1) {
            perror("send error");
            break;
        }

        if (!isQuit) {
            // RECEIVE FEEDBACK
            int answerLength;
            recv(create_socket, &answerLength, sizeof(int), 0);
            char answer[BUF];
            int totalBytesRecv = 0;
            int bytesLeftRecv = BUF;
            int bytesRecv;

            while (totalBytesRecv < answerLength) {
                bytesRecv = recv(create_socket, answer + totalBytesRecv, bytesLeftRecv, 0);
                if (bytesRecv == -1) {
                    perror("recv error");
                    break;
                }

                if (bytesRecv == 0) {
                    printf("Server closed remote socket\n");
                    break;
                }

                totalBytesRecv += bytesRecv;
                bytesLeftRecv -= bytesRecv;
            }

            answer[totalBytesRecv] = '\0';
            printf("<< %s\n", answer);
        }
    } while (!isQuit);

    // CLOSES THE DESCRIPTOR
    if (create_socket != -1) {
        if (shutdown(create_socket, SHUT_RDWR) == -1) {
            // invalid in case the server is gone already
            perror("shutdown create_socket");
        }
        if (close(create_socket) == -1) {
            perror("close create_socket");
        }
        create_socket = -1;
    }

    return EXIT_SUCCESS;
}
