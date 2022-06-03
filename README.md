# chat-server
The chat server was made by C++ language for Linux
*The excample and idea of this project was got from book Andrey Stolyarov "Programming introduction to the profession 3 part".*
Run a program on your PC and connect to this server using telnet

## Build a project
To build a project type on a terminal:
```
g++ ./server.cpp -c
g++ ./main.cpp ./server.o -o main
```

## Run a project
To run a project type `./main`.

## To connect to this server
To connect to this server type `telnet <ip> <port>` (make sure that you have installed a telnet).

## Main instructions for this server
- After connecting to server by telnet type your name without spaces and '/'
- Type `/s <<name>> <some message>` to send private message to only-one user
- Type `/q` to leave the chat
- Type `<some message>` to send all members of this chat a message
