# Project TWMAILER - Grundlagen verteilter Systeme (VERTS)

Group project for third semester course "Grundlagen verteilter Systeme" (VERTS) at UAS Technikum Wien.

Authors: Matthias Kemmer, Yannick Nwankwo

### Task

Write a socket-based client-server application in C/C++ to send and receive messages like an internal mail-server. The project has a basic and a pro version.

### Build with terminal

Build command server: `g++ -g -Wall -o myfind twserver.cpp`

Build command client: `g++ -g -Wall -o myfind twclient.cpp`

### Build with Makefile

Build all command: `make all`

Clean command: `make clean`

### Usage

Usage Client: `./twmailer-client <ip> <port>`

Usage Server: `./twmailer-server <port> <mail-spool-directoryname>`

- SEND: Client sends a message to the server.
- LIST: Lists all received messages of a specific user from his inbox.
- READ: Display a specific message of a specific user.
- DEL: Removes a specific message.
- QUIT: Logout the client.

The pro version extens the server to work concurrent and a LOGIN command to check the credentials using the internal LDAP server.