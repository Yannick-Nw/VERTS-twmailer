#include <string>
#include <sstream>
#include <iostream>
#include <string>
#include <cstring>
#include <csignal>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fstream>

#define BUF 1024
#define PORT 6543

bool abortRequested = false;
int create_socket = -1;
int new_socket = -1;

void clientCommunication(int* data);

void signalHandler(int sig);

const char* messageHandler(char* buffer);

int createDirectory(std::string& path);

int clientSend(char* message);

const char* clientList(char* message);

void clientRead();

void clientDel();

void clientQuit();

///////////////////////////////////////////////////////////////////////////////

int main(void)
{
    socklen_t addrlen;
    sockaddr_in address, cliaddress;
    int reuseValue = 1;

    if (signal(SIGINT, signalHandler) == SIG_ERR) {
        std::cerr << "signal can not be registered";
        return EXIT_FAILURE;
    }

    if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        std::perror("Socket error");
        return EXIT_FAILURE;
    }

    if (setsockopt(create_socket,
            SOL_SOCKET,
            SO_REUSEADDR,
            &reuseValue,
            sizeof(reuseValue)) == -1) {
        std::perror("set socket options - reuseAddr");
        return EXIT_FAILURE;
    }

    if (setsockopt(create_socket,
            SOL_SOCKET,
            SO_REUSEPORT,
            &reuseValue,
            sizeof(reuseValue)) == -1) {
        std::perror("set socket options - reusePort");
        return EXIT_FAILURE;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(create_socket, (struct sockaddr*) &address, sizeof(address)) == -1) {
        std::perror("bind error");
        return EXIT_FAILURE;
    }

    if (listen(create_socket, 5) == -1) {
        std::perror("listen error");
        return EXIT_FAILURE;
    }

    while (!abortRequested) {
        std::cout << "Waiting for connections...\n";

        addrlen = sizeof(struct sockaddr_in);
        if ((new_socket = accept(create_socket,
                (struct sockaddr*) &cliaddress,
                &addrlen)) == -1) {
            if (abortRequested) {
                std::perror("accept error after aborted");
            } else {
                std::perror("accept error");
            }
            break;
        }

        std::cout << "Client connected from " << inet_ntoa(cliaddress.sin_addr) << ":" << ntohs(cliaddress.sin_port) << "...\n";
        clientCommunication(&new_socket);
        new_socket = -1;
    }

    if (create_socket != -1) {
        if (shutdown(create_socket, SHUT_RDWR) == -1) {
            std::perror("shutdown create_socket");
        }
        if (close(create_socket) == -1) {
            std::perror("close create_socket");
        }
        create_socket = -1;
    }

    return EXIT_SUCCESS;
}

void clientCommunication(int* data)
{
    char buffer[BUF];
    int size;
    //int* current_socket = (int*) data;

    strcpy(buffer, "Welcome to myserver!\r\nPlease enter your commands...\r\n");
    if (send(*data, buffer, strlen(buffer), 0) == -1) {
        std::perror("send failed");
        return;
    }

    do {
        size = recv(*data, buffer, BUF - 1, 0);
        if (size == -1) {
            if (abortRequested) {
                std::perror("recv error after aborted");
            } else {
                std::perror("recv error");
            }
            break;
        }

        if (size == 0) {
            std::cout << "Client closed remote socket\n";
            break;
        }

        buffer[size] = '\0';
        std::cout << "Message received: " << buffer << "\n";
        std::string path = "./users";
        createDirectory(path);
        const char* answer = messageHandler(buffer);
        if (send(*data, answer, 3, 0) == -1) {
            std::perror("send answer failed");
            return;
        }
    } while (strcmp(buffer, "quit") != 0 && !abortRequested);

    if (*data != -1) {
        if (shutdown(*data, SHUT_RDWR) == -1) {
            std::perror("shutdown new_socket");
        }
        if (close(*data) == -1) {
            std::perror("close new_socket");
        }
        *data = -1;
    }
}

const char* messageHandler(char* buffer)
{
    std::string option;
    for (int i = 0; buffer[i] != '\0'; ++i) {
        if (buffer[i] != '\n') {
            option += buffer[i];
        } else {
            if (option == "SEND") {
                if(clientSend(buffer)){
                    return "ERR\n";
                } else {
                    return "OK\n";
                }
            } else if (option == "LIST") {
                return clientList(buffer);
            } else if (option == "READ") {
                clientRead();
            } else if (option == "DEL") {
                clientDel();
            } else if (option == "QUIT") {
                return "QUIT";
            }
        }
    }
    return "ERR\n";
}

int createDirectory(std::string& pathname)
{
    const char* path = pathname.c_str();
    // Create a stat structure to check the directory status
    struct stat info;

    // Use stat to check the status of the directory
    if (stat(path, &info) != 0) {
        // If stat returns an error, the directory does not exist and can be created
        if (mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
            std::cerr << "Error creating directory\n";
            return 1;
        }
    } else if (info.st_mode & S_IFDIR) {
        // If the directory exists, output a message
        std::cout << "The directory already exists\n";
        return 0;
    } else {
        std::cout << "Path exists, but is not a directory\n";
        return 1;
    }
    return 0;
}

int clientSend(char* message)
{
    std::string line, sender, path_receiver, path_sender, subject;
    std::ofstream file;
    int state = 0;
    for (int i = 0; message[i] != '\0'; ++i) {
        if (message[i] != '\n') {
            line += message[i];
        } else {
            if (line == ".") {
                break;
            }
            switch (state) {
                case 0:
                    if (line == "SEND") {
                        state = 1;
                    } else {
                        return 1;
                    }
                    break;
                case 1:
                    //Sender
                    sender = line;
                    state = 2;
                    break;
                case 2:
                    //Receiver
                    path_receiver = "./user/" + line;
                    if (createDirectory(path_receiver)){
                        return 1;
                    }
                    path_sender = path_receiver + sender;
                    if(createDirectory(path_sender)){
                        return 1;
                    }
                    state = 3;
                    break;
                case 3:
                    //Subject
                    subject = path_sender + line;
                    file.open(subject);
                    state = 4;
                    break;
                case 4:
                    //Message
                    if (file.is_open()) {
                        file << line;
                        file.close();
                    } else {
                        std::cout << "Unable to open file";
                        return 1;
                    }
                    break;
            }
            line.clear();
        }
    }
    return 0;
}

const char* clientList(char* message){
    std::string line, sender, path_receiver, path_sender, subject;
    std::ofstream file;
    int state = 0;
    for (int i = 0; message[i] != '\0'; ++i) {
        if (message[i] != '\n') {
            line += message[i];
        } else {
            if (line == ".") {
                break;
            }
            switch (state) {
                case 0:
                    if (line == "LIST") {
                        state = 1;
                    } else {
                        return "0";
                    }
                    break;
                case 1:
                    //Sender
                    sender = line;
                    state = 2;
                    break;
            }
            line.clear();
        }
    }
    return 0;
}

void signalHandler(int sig)
{
    if (sig == SIGINT) {
        std::cout << "abort Requested... ";

        abortRequested = true;

        if (new_socket != -1) {
            if (shutdown(new_socket, SHUT_RDWR) == -1) {
                std::perror("shutdown new_socket");
            }
            if (close(new_socket) == -1) {
                std::perror("close new_socket");
            }
            new_socket = -1;
        }

        if (create_socket != -1) {
            if (shutdown(create_socket, SHUT_RDWR) == -1) {
                std::perror("shutdown create_socket");
            }
            if (close(create_socket) == -1) {
                std::perror("close create_socket");
            }
            create_socket = -1;
        }
    } else {
        exit(sig);
    }
}
