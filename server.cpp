#include <arpa/inet.h>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <errno.h>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <ldap.h>
#include <map>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#define BUF 1024

bool abortRequested = false;
int create_socket = -1;
int new_socket = -1;
std::map<std::string, time_t> blacklist;
int loginFailed = 0;

void clientCommunication(int* data, std::string mailSpoolDir, std::string userIP);

// Signal handler for SIGINT (Ctrl+C)
void signalHandler(int sig);

// Function to handle client's messages
std::string messageHandler(char* buffer, std::string mailSpoolDir, std::string userIP, std::string userName = "");

// Function to create a directory
int createDirectory(std::string& path);

// Function to send a message to the client
int clientSend(char* message, std::string mailSpoolDir, std::string userName);

// Function to list the client's messages
std::string clientList(char* message, std::string mailSpoolDir, std::string userName);

// Function to read a client's message
std::string clientRead(char* message, std::string mailSpoolDir, std::string userName);

// Function to delete a client's message
int clientDel(char* message, std::string mailSpoolDir, std::string userName);

// Function to search for subjects in the user's mailbox
std::string searchSubjects(std::string& username, std::string mailSpoolDir, int number = -1);

// Function to login client
int clientLogin(char* message, std::string mailSpoolDir);

// Function to blacklist a user
void blacklistUser(std::string userIP);

// Function to check if a user is blacklisted
bool checkBlacklist(std::string userIP);

// Main function
int main(int argc, char* argv[])
{
    // Check if the correct number of arguments are provided
    if (argc != 3) {
        std::cout << "Usage: " << argv[0] << "<port> <mail-spool-directoryname>\n";
        return 1;
    }

    // Convert the port number from string to integer
    int port = std::stoi(argv[1]);

    // Check if the port number is valid
    if (port == 0) {
        return 1;
    }

    // Get the mail spool directory from the arguments
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

    // Set socket options for reusing address and port and check if it was successful
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

    // Listen for incoming connections and check if it was successful
    if (listen(create_socket, 5) == -1) {
        std::perror("listen error");
        return EXIT_FAILURE;
    }

    // Main loop for accepting connections and handling client communication
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

        // Create a new process for each client connection
        pid_t pid = fork();
        if (pid < 0) {
            std::perror("fork error");
            return EXIT_FAILURE;
        } else if (pid == 0) {  // Child process
            if (create_socket != -1) {
                if (shutdown(create_socket, SHUT_RDWR) == -1) {
                    std::perror("shutdown create_socket");
                }
                if (close(create_socket) == -1) {
                    std::perror("close create_socket");
                }
                create_socket = -1;
            }
            clientCommunication(&new_socket, mailSpoolDir, inet_ntoa(client_address.sin_addr));
            if (new_socket != -1) {
                if (shutdown(new_socket, SHUT_RDWR) == -1) {
                    std::perror("shutdown new_socket");
                }
                if (close(new_socket) == -1) {
                    std::perror("close new_socket");
                }
                new_socket = -1;
            }
            return EXIT_SUCCESS;  // End the child process
        } else {  // Parent process
            close(new_socket);  // Close the client socket in the parent process
        }
    }

    // Close the socket after all connections have been handled
    if (create_socket != -1) {
        if (shutdown(create_socket, SHUT_RDWR) == -1) {
            std::perror("shutdown create_socket");
        }
        if (close(create_socket) == -1) {
            std::perror("close create_socket");
        }
        create_socket = -1;
    }

    // Wait for all remaining child processes
    while (wait(NULL) > 0);

    return EXIT_SUCCESS;
}

void clientCommunication(int* data, std::string mailSpoolDir, std::string userIP)
{
    char buffer[BUF];
    //int* current_socket = (int*) data;

    strcpy(buffer, "Welcome to the Mail-server!\nPlease enter your commands...\n");
    if (send(*data, buffer, strlen(buffer), 0) == -1) {
        std::perror("send failed");
        return;
    }
    std::string userName = "";
    bool loggedIn = false;
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
        std::string s_answer = messageHandler(buffer, path, userIP, userName);
        if (s_answer != "QUIT") {
            if (loggedIn == false) {
                if (s_answer != "ERR\n") {
                    loggedIn = true;
                    userName = s_answer;
                    s_answer = "OK\n";
                }
            }
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

std::string messageHandler(char* buffer, std::string mailSpoolDir, std::string userIP, std::string userName)
{
    std::string option;
    int loggedIn = 0;
    for (int i = 0; buffer[i] != '\0'; ++i) {
        if (buffer[i] != '\n') {
            option += buffer[i];
        } else {
            if (userName != "" && (option != "LOGIN" || option != "QUIT")) {
                if (option == "SEND") {
                    if (clientSend(buffer, mailSpoolDir, userName)) {
                        return "ERR\n";
                    } else {
                        return "OK\n";
                    }
                } else if (option == "LIST") {
                    return clientList(buffer, mailSpoolDir, userName);
                } else if (option == "READ") {
                    std::string message = clientRead(buffer, mailSpoolDir, userName);
                    if (message == "0") {
                        return "ERR\n";
                    }
                    std::string ok = "OK\n";
                    return ok.append(message);
                } else if (option == "DEL") {
                    if (clientDel(buffer, mailSpoolDir, userName)) {
                        return "ERR\n";
                    } else {
                        return "OK\n";
                    }
                }
            } else if (option == "LOGIN") {
                if (checkBlacklist(userIP)){
                    std::cout << "Login blocked. Blacklisted user." << std::endl;
                    return "ERR\n";
                }
                if (clientLogin(buffer, mailSpoolDir)){
                    loginFailed++;
                    if (loginFailed >= 3){
                        blacklistUser(userIP);
                        loginFailed = 0;
                        std::cout << "Login blocked. Add user to blacklist." << std::endl;
                    }
                    return "ERR\n";
                } else {
                    loginFailed = 0;
                    return userName;
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

int clientSend(char* message, std::string mailSpoolDir, std::string userName)
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
                    /*
                    //Sender
                    state = 2;
                    break;
                case 2:
                    */
                    //Receiver
                    path_receiver = mailSpoolDir;
                    path_receiver += "/";
                    path_receiver += line;
                    if (createDirectory(path_receiver)) {
                        return 1;
                    }
                    state = 2;
                    break;
                case 2:
                    //Subject

                    subject = path_receiver;
                    subject.append("/");
                    subject.append(line);
                    subject.append(" - ");
                    subject.append(userName);
                    file.open(subject);
                    state = 3;
                    break;
                case 3:
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
    std::string s_path = mailSpoolDir;
    s_path += "/";
    s_path += username;
    const char* path = s_path.c_str();

    // Open the directory as a file to acquire a lock
    int dirfd = open(path, O_RDONLY);
    if (dirfd == -1) {
        perror("Failed to open directory");
        return "0";
    }

    // Acquire an advisory lock on the directory
    struct flock fl;
    fl.l_type = F_RDLCK;  // Read lock
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;  // Lock the whole file
    if (fcntl(dirfd, F_SETLKW, &fl) == -1) {  // F_SETLKW will block until the lock is acquired
        perror("Failed to lock directory");
        close(dirfd);
        return "0";
    }

    DIR* dirp = fdopendir(dirfd);
    if (dirp == NULL) {
        perror("Failed to open directory");
        close(dirfd);
        return "0";
    }

    std::vector<std::string> subjects;
    struct dirent* direntp;
    direntp = readdir(dirp);
    while (direntp != NULL) {
        subjects.push_back(direntp->d_name);
        direntp = readdir(dirp);
    }

    // Release the lock and close the directory
    fl.l_type = F_UNLCK;
    if (fcntl(dirfd, F_SETLK, &fl) == -1) {
        perror("Failed to unlock directory");
    }
    if (closedir(dirp) == -1) {
        perror("Failed to close directory");
    }

    std::string output = std::to_string(subjects.size()) + "\n";
    for (size_t i = 0; i < subjects.size(); i++) {
        if (number == -1) {
            std::string line_number = std::to_string(i + 1);
            std::string line = line_number + " - " + subjects[i] + "\n";
            output += line;
        } else if (number == static_cast<int>(i + 1)) {
            std::cout << subjects[i];
            return subjects[i];
        }
    }
    return output;
}

std::string clientList(char* message, std::string mailSpoolDir, std::string userName)
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
                    //username = line;
                    return searchSubjects(userName, mailSpoolDir);
            }
            line.clear();
        }
    }
    return "0";
}

std::string clientRead(char* message, std::string mailSpoolDir, std::string userName)
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
                    //username = message_line;
                    username = userName;
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
                    path += "/";
                    path += file_name;

                    // Open the file with a file descriptor for locking
                    int fd = open(path.c_str(), O_RDONLY);
                    if (fd == -1) {
                        perror("Failed to open file");
                        return "0";
                    }

                    // Acquire a read lock on the file
                    struct flock fl;
                    fl.l_type = F_RDLCK;
                    fl.l_whence = SEEK_SET;
                    fl.l_start = 0;
                    fl.l_len = 0;
                    if (fcntl(fd, F_SETLKW, &fl) == -1) {
                        perror("Failed to lock file");
                        close(fd);
                        return "0";
                    }

                    // Now it's safe to read the file
                    file.open(path);
                    if (file.is_open()) {
                        while (std::getline(file, file_line)) {
                            content += file_line + '\n';
                        }
                        file.close();
                    } else {
                        std::cerr << "File could not be opened\n" << path;
                        close(fd);
                        return "0";
                    }

                    // Release the lock and close the file descriptor
                    fl.l_type = F_UNLCK;
                    if (fcntl(fd, F_SETLK, &fl) == -1) {
                        perror("Failed to unlock file");
                    }
                    close(fd);

                    return content;
            }
            message_line.clear();
        }
    }
    return "0";
}

int clientDel(char* message, std::string mailSpoolDir, std::string userName)
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
                    //username = message_line;
                    username = userName;
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
                    path += "/";
                    path += file_name;

                    // Open the file with a file descriptor for locking
                    int fd = open(path.c_str(), O_RDONLY);
                    if (fd == -1) {
                        perror("Failed to open file");
                        return 1;
                    }

                    // Acquire a write lock on the file
                    struct flock fl;
                    fl.l_type = F_WRLCK;
                    fl.l_whence = SEEK_SET;
                    fl.l_start = 0;
                    fl.l_len = 0;
                    if (fcntl(fd, F_SETLKW, &fl) == -1) {
                        perror("Failed to lock file");
                        close(fd);
                        return 1;
                    }

                    // Now it's safe to delete the file
                    if (remove(path.c_str()) != 0) {
                        std::cerr << "Error deleting file";
                        close(fd);
                        return 1;
                    }

                    // Release the lock and close the file descriptor
                    fl.l_type = F_UNLCK;
                    if (fcntl(fd, F_SETLK, &fl) == -1) {
                        perror("Failed to unlock file");
                    }
                    close(fd);

                    return 0;
            }
            message_line.clear();
        }
    }
    return 1;
}

int clientLogin(char* message, std::string mailSpoolDir)
{
    std::string path = mailSpoolDir;
    std::string message_line, ldapUser, ldapBindPassword;
    int state = 0;
    for (int i = 0; message[i] != '\0'; ++i) {
        if (message[i] != '\n') {
            message_line += message[i];
        } else {
            switch (state) {
                case 0:
                    if (message_line == "LOGIN") {
                        state = 1;
                    } else {
                        return 1;
                    }
                    break;
                case 1:
                    // LDAP username
                    ldapUser = message_line;
                    state = 2;
                    break;
                case 2:
                    // LDAP password
                    ldapBindPassword = message_line;
            }
            message_line.clear();
        }
    }
    // LDAP config
    const char *ldapUri = "ldap://ldap.technikum-wien.at:389";
    const int ldapVersion = LDAP_VERSION3;

    // Set username
    char ldapBindUser[256];
    sprintf(ldapBindUser, "uid=%s,ou=people,dc=technikum-wien,dc=at", ldapUser.c_str());
    printf("user set to: %s\n", ldapBindUser);

    // General
    int rc = 0; // return code

    // Setup LDAP connection
    LDAP *ldapHandle;
    rc = ldap_initialize(&ldapHandle, ldapUri);
    if (rc != LDAP_SUCCESS)
    {
        fprintf(stderr, "ldap_init failed\n");
        return -1;
    }
    printf("connected to LDAP server %s\n", ldapUri);

    // LDAP set options
    rc = ldap_set_option(
        ldapHandle,
        LDAP_OPT_PROTOCOL_VERSION, // OPTION
        &ldapVersion);             // IN-Value
    if (rc != LDAP_OPT_SUCCESS)
    {
        // https://www.openldap.org/software/man.cgi?query=ldap_err2string&sektion=3&apropos=0&manpath=OpenLDAP+2.4-Release
        fprintf(stderr, "ldap_set_option(PROTOCOL_VERSION): %s\n", ldap_err2string(rc));
        ldap_unbind_ext_s(ldapHandle, NULL, NULL);
        return EXIT_FAILURE;
    }

    // LDAP start connection secure (initialize TLS)
    rc = ldap_start_tls_s(
        ldapHandle,
        NULL,
        NULL);
    if (rc != LDAP_SUCCESS)
    {
        fprintf(stderr, "ldap_start_tls_s(): %s\n", ldap_err2string(rc));
        ldap_unbind_ext_s(ldapHandle, NULL, NULL);
        return EXIT_FAILURE;
    }

    // LDAP bind credentials
    BerValue bindCredentials;
    bindCredentials.bv_val = (char*)ldapBindPassword.c_str();;
    bindCredentials.bv_len = ldapBindPassword.length();
    BerValue *servercredp; // server's credentials
    rc = ldap_sasl_bind_s(
        ldapHandle,
        ldapBindUser,
        LDAP_SASL_SIMPLE,
        &bindCredentials,
        NULL,
        NULL,
        &servercredp);
    if (rc != LDAP_SUCCESS)
    {
        fprintf(stderr, "LDAP bind error: %s\n", ldap_err2string(rc));
        ldap_unbind_ext_s(ldapHandle, NULL, NULL);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
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
    } else if (sig == SIGCHLD) {
        // Clean up zombie processes
        pid_t childpid;
        while ((childpid = waitpid(-1, NULL, WNOHANG)) > 0) {
            if ((childpid == -1) && (errno != EINTR)) {
                break;
            }
        }
    } else {
        exit(sig);
    }
}

void blacklistUser(std::string userIP){
    time_t currentTime;
    time(&currentTime);
    blacklist[userIP] = currentTime;
}

bool checkBlacklist(std::string userIP){
    auto entry = blacklist.find(userIP);
    if (entry == blacklist.end()) {
        return false;
    }

    time_t blacklistTime = entry->second;
    time_t currentTime;
    time(&currentTime);

    if((currentTime - blacklistTime) < 60){
        return true;
    }

    return false;
}