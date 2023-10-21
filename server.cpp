#include <iostream>
#include <string>
#include <cstring>
#include <csignal>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define BUF 1024
#define PORT 6543

bool abortRequested = false;
int create_socket = -1;
int new_socket = -1;

void clientCommunication(int* data);
void signalHandler(int sig);

void messageHandler(char *buffer);

///////////////////////////////////////////////////////////////////////////////

int main(void)
{
    socklen_t addrlen;
   sockaddr_in address, cliaddress;
    int reuseValue = 1;

   if (signal(SIGINT, signalHandler) == SIG_ERR)
   {
      std::cerr << "signal can not be registered";
        return EXIT_FAILURE;
    }

   if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
   {
      std::perror("Socket error");
        return EXIT_FAILURE;
    }

    if (setsockopt(create_socket,
            SOL_SOCKET,
            SO_REUSEADDR,
            &reuseValue,
                  sizeof(reuseValue)) == -1)
   {
      std::perror("set socket options - reuseAddr");
        return EXIT_FAILURE;
    }

    if (setsockopt(create_socket,
            SOL_SOCKET,
            SO_REUSEPORT,
            &reuseValue,
                  sizeof(reuseValue)) == -1)
   {
      std::perror("set socket options - reusePort");
        return EXIT_FAILURE;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

   if (bind(create_socket, (struct sockaddr *)&address, sizeof(address)) == -1)
   {
      std::perror("bind error");
        return EXIT_FAILURE;
    }

   if (listen(create_socket, 5) == -1)
   {
      std::perror("listen error");
        return EXIT_FAILURE;
    }

   while (!abortRequested)
   {
      std::cout << "Waiting for connections...\n";

        addrlen = sizeof(struct sockaddr_in);
        if ((new_socket = accept(create_socket,
                (struct sockaddr*) &cliaddress,
                &addrlen)) == -1) {
         if (abortRequested)
         {
            std::perror("accept error after aborted");
         }
         else
         {
            std::perror("accept error");
            }
            break;
        }

      std::cout << "Client connected from " << inet_ntoa(cliaddress.sin_addr) << ":" << ntohs(cliaddress.sin_port) << "...\n";
      clientCommunication(&new_socket);
        new_socket = -1;
    }

   if (create_socket != -1)
   {
      if (shutdown(create_socket, SHUT_RDWR) == -1)
      {
         std::perror("shutdown create_socket");
        }
      if (close(create_socket) == -1)
      {
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
    if (send(*data, buffer, strlen(buffer), 0) == -1)
    {
        std::perror("send failed");
        return;
    }

    do
    {
        size = recv(*data, buffer, BUF - 1, 0);
        if (size == -1)
        {
            if (abortRequested)
            {
                std::perror("recv error after aborted");
            }
            else
            {
                std::perror("recv error");
            }
            break;
        }

        if (size == 0)
        {
            std::cout << "Client closed remote socket\n";
            break;
        }

        buffer[size] = '\0';
        std::cout << "Message received: " << buffer << "\n";

        if (send(*data, "OK", 3, 0) == -1)
        {
            std::perror("send answer failed");
            return;
        }
    } while (strcmp(buffer, "quit") != 0 && !abortRequested);

    if (*data != -1)
    {
        if (shutdown(*data, SHUT_RDWR) == -1)
        {
            std::perror("shutdown new_socket");
        }
        if (close(*data) == -1)
        {
            std::perror("close new_socket");
        }
        *data = -1;
    }
}

void messageHandler(char *buffer) {
    
}

void signalHandler(int sig)
{
    if (sig == SIGINT)
    {
        std::cout << "abort Requested... ";

        abortRequested = true;

        if (new_socket != -1)
        {
            if (shutdown(new_socket, SHUT_RDWR) == -1)
            {
                std::perror("shutdown new_socket");
            }
            if (close(new_socket) == -1)
            {
                std::perror("close new_socket");
            }
            new_socket = -1;
        }

        if (create_socket != -1) {
            if (shutdown(create_socket, SHUT_RDWR) == -1)
            {
                std::perror("shutdown create_socket");
            }
            if (close(create_socket) == -1)
            {
                std::perror("close create_socket");
            }
            create_socket = -1;
        }
    } else {
        exit(sig);
    }
}
