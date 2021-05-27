#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define DEFAULT_PORT 10007 // default server port
#define MAX_EVENTS 1000    // events limit
#define MAX_CLIENTS 1000   // clients limit
#define BACKLOG 1000       // connection queue limit
#define TIME_OUT -1        // timeout
#define BUFFER_SIZE 1024   // temporary buffer limit
#define IP_LEN 20
#define EMPTY -1

int server_socket_fd;     // server socket fd
int server_port;          // server port number
int epoll_fd;             // epoll fd
char buffer[BUFFER_SIZE]; // buffer
epoll_event events[MAX_EVENTS];

struct
{
    int socket_fd;
    char ip[IP_LEN];
} clients[MAX_CLIENTS];

static void die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

static void init_socket()
{
    struct sockaddr_in server_addr;

    /* Open TCP Socket */
    if ((server_socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        die("[FAIL] server start failed : can't open stream socket\n");
    }

    /* Set Address */
    memset(&server_addr, 0, sizeof server_addr);

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(DEFAULT_PORT);

    /* Bind Socket */
    if (bind(server_socket_fd, (struct sockaddr *)&server_addr, sizeof server_addr) < 0)
    {
        close(server_socket_fd);
        die("[FAIL] server start failed : can't bind local address\n");
    }

    /* Listen */
    if (listen(server_socket_fd, BACKLOG) < 0)
    {
        close(server_socket_fd);
        die("[FAIL] server start failed : can't start listening on port\n");
    }

    printf("[SUCCESS] server socket initialized\n");
}

static void init_epoll()
{
    /* Create Epoll */
    if ((epoll_fd = epoll_create(MAX_EVENTS)) < 0)
    {
        close(server_socket_fd);
        die("[FAIL] epoll create failed\n");
    }

    /* Set Event Control */
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = server_socket_fd;

    /* Set Server Events */
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_socket_fd, &event) < 0)
    {
        close(epoll_fd);
        close(server_socket_fd);
        die("[FAIL] epoll control failed\n");
    }

    printf("[SUCCESS] epoll events initialized for the server\n");
}

static void init_clients()
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        clients[i].socket_fd = EMPTY;
    }
}

void add_client_to_epoll(struct epoll_event event)
{
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event.data.fd, &event) < 0)
    {
        printf("[FAIL] epoll control failed while adding client\n");
    }
}

void add_client_to_pool(int client_socket_fd, char *client_ip)
{
    int i;
    for (i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].socket_fd == EMPTY)
            break;
    }

    if (i >= MAX_CLIENTS)
    {
        printf("[FAIL] max clients exceeded\n");
        close(client_socket_fd);
        return;
    }

    clients[i].socket_fd = client_socket_fd;
    memset(&clients[i].ip[0], 0, IP_LEN);
    strcpy(&clients[i].ip[0], client_ip);
}

void remove_client_from_epoll(struct epoll_event event)
{
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event.data.fd, &event) < 0)
    {
        printf("[FAIL] epoll control failed while removing client\n");
    }
}

void remove_client_from_pool(int target_fd)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].socket_fd == target_fd)
        {
            clients[i].socket_fd = EMPTY;
            break;
        }
    }
}

void broadcast(int sender_fd, int len)
{
    int i;
    for (i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].socket_fd != EMPTY && clients[i].socket_fd != sender_fd)
        {
            len = write(clients[i].socket_fd, buffer, len);
        }
    }
    memset(buffer, NULL, sizeof(char) * BUFFER_SIZE);
}

void process_message(struct epoll_event event)
{
    /* Read from Socket */
    int len = read(event.data.fd, buffer, sizeof buffer);
    if (len < 0)
    {
        /* Error while Reading */
        remove_client_from_epoll(event);
        remove_client_from_pool(event.data.fd);
        close(event.data.fd);
        perror("[FAIL] read from socket failed\n");
        return;
    }
    if (len == 0)
    {
        /* Disconnect Client */
        remove_client_from_epoll(event);
        remove_client_from_pool(event.data.fd);
        close(event.data.fd);
        printf("[SUCCESS] client disconnected (fd:%d)\n", event.data.fd);
        return;
    }

    /* Broadcast to Clients */
    broadcast(event.data.fd, len);
}

void set_non_blocking(int socket)
{
    int flag = fcntl(socket, F_GETFL, 0);
    fcntl(socket, F_SETFL, flag | O_NONBLOCK);
}

void run_process()
{
    /* Wait for Events */
    int num_of_fds = epoll_wait(epoll_fd, events, MAX_EVENTS, TIME_OUT);

    if (num_of_fds < 0)
    {
        perror("[FAIL] epoll wait failed\n");
        return;
    }

    for (int i = 0; i < num_of_fds; i++)
    {
        /* Create Event */
        struct epoll_event event;
        event.events = EPOLLIN;

        if (events[i].data.fd == server_socket_fd)
        {
            /* Accept Connection Request */
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_socket_fd = accept(server_socket_fd,
                                          (struct sockaddr *)&client_addr,
                                          &client_len);

            /* Handle Connection Request */
            if (client_socket_fd < 0)
            {
                perror("[FAIL] epoll accept failed\n");
            }
            else
            {
                event.data.fd = client_socket_fd;
                set_non_blocking(client_socket_fd);
                add_client_to_epoll(event);
                add_client_to_pool(client_socket_fd, inet_ntoa(client_addr.sin_addr));

                printf("[SUCCESS] new client connected (fd:%d, ip:%s)\n",
                       client_socket_fd, inet_ntoa(client_addr.sin_addr));
            }
        }
        else
        {
            /* Read and Broadcast Message to Clients */
            event.data.fd = events[i].data.fd;
            process_message(event);
        }
    }
}

int main()
{
    init_socket();
    init_epoll();
    init_clients();

    printf("======= SERVER RUNNING =======\n");
    while (1)
    {
        run_process();
    }
    printf("======= SERVER STOPPED =======");

    return 0;
}