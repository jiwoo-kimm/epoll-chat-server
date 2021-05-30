#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024

static void die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

int server_socket_fd;
struct sockaddr_in server_socket_addr;
int server_socket_len;

void init_fds(fd_set fds)
{
    FD_ZERO(&fds);
    FD_SET(server_socket_fd, &fds);
    FD_SET(0, &fds);
}

int main(int argc, char **argv)
{
    /* Check Parameters */
    if (argc != 3)
    {
        die("[FAIL] server ip address & port number required\n");
    }

    /* Create Server Socket */
    if ((server_socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        die("[FAIL] socket creation failed\n");
    }
    server_socket_addr.sin_family = AF_INET;
    server_socket_addr.sin_addr.s_addr = inet_addr(argv[1]);
    server_socket_addr.sin_port = htons(atoi(argv[2]));
    server_socket_len = sizeof(server_socket_addr);

    /* Set Up Connection with Server Socket */
    if (connect(server_socket_fd, (struct sockaddr *)&server_socket_addr, server_socket_len) < 0)
    {
        die("[FAIL] connection set-up failed\n");
    }

    /* ~~~ */
    fd_set readfds, oldfds;
    int max_fd;
    int event_fd;
    int readn, writen;

    char buffer[BUFFER_SIZE];

    init_fds(readfds);
    oldfds = readfds;

    max_fd = server_socket_fd;

    while (1)
    {
        readfds = oldfds;
        
        event_fd = select(max_fd + 1, &readfds, NULL, NULL, NULL);

        /* Check if it's initial connection */
        if (FD_ISSET(server_socket_fd, &readfds))
        {
            memset(buffer, 0x00, BUFFER_SIZE);
            readn = read(server_socket_fd, buffer, BUFFER_SIZE);
            if (readn <= 0)
            {
                return 1;
            }
            writen = write(1, buffer, readn);
            if (writen != readn)
            {
                return 1;
            }
        }
        if (FD_ISSET(0, &readfds)) // raedfds의 0번째 인자가 set?
        {
            memset(buffer, 0x00, BUFFER_SIZE);
            readn = read(0, buffer, BUFFER_SIZE);
            printf("> %s", buffer);
            if (readn <= 0)
            {
                return 1;
            }
            writen = write(server_socket_fd, buffer, readn);
            if (readn != writen)
            {
                return 1;
            }
        }
    }
}