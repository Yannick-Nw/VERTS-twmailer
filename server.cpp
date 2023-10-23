#include <arpa/inet.h>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <errno.h>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
//#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
//#include <sys/types.h>
#include <unistd.h>
#include <vector>

#define BUF 1024

bool abortRequested = false;
int create_socket = -1;
int new_socket = -1;

void clientCommunication(int* data, std::string mailSpoolDir);

void signalHandler(int sig);

std::string messageHandler(char* buffer, std::string mailSpoolDir);

int createDirectory(std::string& path);

int clientSend(char* message, std::string mailSpoolDir);

std::string clientList(char* message, std::string mailSpoolDir);

std::string clientRead(char* message, std::string mailSpoolDir);

int clientDel(char* message, std::string mailSpoolDir);

std::string searchSubjects(std::string& username, std::string mailSpoolDir, int number = -1);

///////////////////////////////////////////////////////////////////////////////

int main(int argc, char* argv[])
{
    if (argc != 3) {
        std::cout << "Usage: " << argv[0] << "<port> <mail-spool-directoryname>\n";
        return 1;
    }
    int port = std::stoi(argv[1]);
    if (port == 0) {
        return 1;
    }
    std::string mailSpoolDir = argv[2];

    socklen_t address_length;
    sockaddr_in address, client_address;
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
    address.sin_port = htons(port);

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

        address_length = sizeof(struct sockaddr_in);
        if ((new_socket = accept(create_socket,
                (struct sockaddr*) &client_address,
                &address_length)) == -1) {
            if (abortRequested) {
                std::perror("accept error after aborted");
            } else {
                std::perror("accept error");
            }
            break;
        }

        std::cout << "Client connected from " << inet_ntoa(client_address.sin_addr) << ":"
                  << ntohs(client_address.sin_port) << "...\n";
        clientCommunication(&new_socket, mailSpoolDir);
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

void clientCommunication(int* data, std::string mailSpoolDir)
{
    char buffer[BUF];
    //int* current_socket = (int*) data;

    strcpy(buffer, "Welcome to the Mail-server!\nPlease enter your commands...\n");
    if (send(*data, buffer, strlen(buffer), 0) == -1) {
        std::perror("send failed");
        return;
    }

    do {
        int totalBytesRecv = 0;
        int bytesLeftRecv = BUF;
        int bytesRecv;

        int messageLength;
        recv(*data, &messageLength, sizeof(int), 0);

        while (totalBytesRecv < messageLength) {
            bytesRecv = recv(*data, buffer + totalBytesRecv, bytesLeftRecv, 0);
            if (bytesRecv == -1) {
                if (abortRequested) {
                    std::perror("recv error after aborted");
                } else {
                    std::perror("recv error");
                }
                break;
            }

            if (bytesRecv == 0) {
                std::cout << "Client closed remote socket\n";
                break;
            }

            totalBytesRecv += bytesRecv;
            bytesLeftRecv -= bytesRecv;
        }

        buffer[totalBytesRecv] = '\0';
        std::cout << "Message received: " << buffer << "\n";
        std::string path = mailSpoolDir + "/users";
        if (createDirectory(path)) {
            std::cerr << "mail-spool-directoryname error\n";
            return;
        }
        std::string s_answer = messageHandler(buffer, mailSpoolDir);
        if (s_answer != "QUIT") {
            const char* answer = s_answer.c_str();
            int totalBytesSent = 0;
            int bytesLeftSent = BUF;
            int bytesSent;

            int answerLength = strlen(answer);
            send(*data, &answerLength, sizeof(int), 0);

            while (totalBytesSent < answerLength) {
                bytesSent = send(*data, answer + totalBytesSent, bytesLeftSent, 0);
                if (bytesSent == -1) {
                    std::perror("send answer failed");
                    return;
                }
                totalBytesSent += bytesSent;
                bytesLeftSent -= bytesSent;
            }
        }
    } while (strcmp(buffer, "QUIT") != 0 && !abortRequested);

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

std::string messageHandler(char* buffer, std::string mailSpoolDir)
{
    std::string option;
    for (int i = 0; buffer[i] != '\0'; ++i) {
        if (buffer[i] != '\n') {
            option += buffer[i];
        } else {
            if (option == "SEND") {
                if (clientSend(buffer, mailSpoolDir)) {
                    return "ERR\n";
                } else {
                    return "OK\n";
                }
            } else if (option == "LIST") {
                return clientList(buffer, mailSpoolDir);
            } else if (option == "READ") {
                std::string message = clientRead(buffer, mailSpoolDir);
                if (message == "0") {
                    return "ERR\n";
                }
                std::string ok = "OK\n";
                return ok.append(message);
            } else if (option == "DEL") {
                if (clientDel(buffer, mailSpoolDir)) {
                    return "ERR\n";
                } else {
                    return "OK\n";
                }
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

int clientSend(char* message, std::string mailSpoolDir)
{
    std::string line, path_receiver, subject;
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
                    state = 2;
                    break;
                case 2:
                    //Receiver
                    path_receiver = mailSpoolDir + line;
                    if (createDirectory(path_receiver)) {
                        return 1;
                    }
                    state = 3;
                    break;
                case 3:
                    //Subject

                    subject = path_receiver + line;
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

std::string searchSubjects(std::string& username, std::string mailSpoolDir, int number)
{
    std::string s_path = mailSpoolDir + username;
    const char* path = s_path.c_str();
    DIR* dirp = opendir(path);
    if (dirp == NULL) {
        perror("Failed to open directory");
        return "0";
    }

    std::vector<std::string> subjects;
    struct dirent* direntp;
    direntp = readdir(dirp);
    while (direntp != NULL) {
        subjects.push_back(direntp->d_name);
        direntp = readdir(dirp);
    }

    while (errno == EINTR) {
        if (closedir(dirp) != -1) break;
    }

    std::string output = std::to_string(subjects.size()) + "\n";
    for (size_t i = 0; i < subjects.size(); i++) {
        if (number == -1) {
            std::string line_number = std::to_string(i + 1);
            std::string line = line_number + " - " + subjects[i] + "\n";
            output += line;
        } else if (number == static_cast<int>(i + 1)) {
            return subjects[i];
        }
    }
    return output;
}

std::string clientList(char* message, std::string mailSpoolDir)
{
    std::string line, username;
    int state = 0;
    for (int i = 0; message[i] != '\0'; ++i) {
        if (message[i] != '\n') {
            line += message[i];
        } else {
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
                    username = line;
                    return searchSubjects(username, mailSpoolDir);
            }
            line.clear();
        }
    }
    return "0";
}

std::string clientRead(char* message, std::string mailSpoolDir)
{
    std::string message_line, username, path, file_line, content, file_name;
    std::ifstream file;
    int state = 0;
    for (int i = 0; message[i] != '\0'; ++i) {
        if (message[i] != '\n') {
            message_line += message[i];
        } else {
            switch (state) {
                case 0:
                    if (message_line == "READ") {
                        state = 1;
                    } else {
                        return "0";
                    }
                    break;
                case 1:
                    //user
                    username = message_line;
                    path = mailSpoolDir;
                    path += "/";
                    path += username;
                    state = 2;
                    break;
                case 2:
                    //Number
                    int number = std::stoi(message_line);
                    file_name += searchSubjects(username, mailSpoolDir, number);
                    if (file_name == "0") {
                        return "0";
                    }
                    path += file_name;
                    file.open(path);
                    if (file.is_open()) {
                        while (std::getline(file, file_line)) {
                            content += file_line + '\n';
                        }
                        file.close();
                    } else {
                        std::cerr << "File could not be opened\n";
                        return "0";
                    }
                    return content;
            }
            message_line.clear();
        }
    }
    return "0";
}

int clientDel(char* message, std::string mailSpoolDir)
{
    std::string message_line, username, path, file_name;
    std::ifstream file;
    int state = 0;
    for (int i = 0; message[i] != '\0'; ++i) {
        if (message[i] != '\n') {
            message_line += message[i];
        } else {
            switch (state) {
                case 0:
                    if (message_line == "READ") {
                        state = 1;
                    } else {
                        return 1;
                    }
                    break;
                case 1:
                    //user
                    username = message_line;
                    path = mailSpoolDir;
                    path += "/";
                    path += username;
                    state = 2;
                    break;
                case 2:
                    //number
                    int number = std::stoi(message_line);
                    file_name += searchSubjects(username, mailSpoolDir, number);
                    if (file_name == "0") {
                        return 1;
                    }
                    path += file_name;
                    if (remove(path.c_str()) != 0) {
                        std::cerr << "Error deleting file";
                        return 1;
                    }
                    return 0;
            }
            message_line.clear();
        }
    }
    return 1;
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
