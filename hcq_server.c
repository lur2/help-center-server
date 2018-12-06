#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "hcq.h"


#ifndef PORT
  #define PORT 30000
#endif
#define MAX_BACKLOG 5
#define MAX_CONNECTIONS 12
#define BUF_SIZE 128


struct sockname {
    int sock_fd;
    char *username;
    int type;
    char *buf;
};

// Use global variables so we can have exactly one TA list and one student list
Ta *ta_list = NULL;
Student *stu_list = NULL;

Course *courses;  
int num_courses = 3;

/* Accept a connection. Note that a new file descriptor is created for
 * communication with the client. The initial socket descriptor is used
 * to accept connections, but the new socket is used to communicate.
 * Return the new client's file descriptor or -1 on error.
 */
int accept_connection(int fd, struct sockname *usernames) {
    int user_index = 0;
    while (user_index < MAX_CONNECTIONS && usernames[user_index].sock_fd != -1) {
        user_index++;
    }

    if (user_index == MAX_CONNECTIONS) {
        fprintf(stderr, "server: max concurrent connections\n");
        return -1;
    }

    int client_fd = accept(fd, NULL, NULL);
    if (client_fd < 0) {
        perror("server: accept");
        close(fd);
        exit(1);
    }
    // instruction
    dprintf(client_fd, "Welcome to the Help Centre, what is your name?\r\n");
 

    usernames[user_index].sock_fd = client_fd;
    usernames[user_index].username = NULL;
    return client_fd;
}

/* Helper function to reset the initialized socket_name
 * after socket is closed.
 */
void clean_sock(int index, struct sockname *usernames){
    close(usernames[index].sock_fd);
    usernames[index].sock_fd = -1;
    free(usernames[index].username);
    usernames[index].username = NULL;
    usernames[index].type = -1; 
    free(usernames[index].buf);
    usernames[index].buf = NULL;
}

/* Helper for reset buffer after a line is read
 * from buf.
 */
void clean_buf(char *buf, int len_next){
    int i = 0;
    while (i < len_next) {
        buf[i] = buf[strlen(buf) + 2 + i];
        i ++;
    }
    while (i < BUF_SIZE) {
        buf[i] = '\0';
        i ++;
    }
}
/*
 * Search the first n characters of buf for a network newline (\r\n).
 * Return one plus the index of the '\n' of the first network newline,
 * or -1 if no network newline is found.
 * Definitely do not use strchr or other string functions to search here. (Why not?)
 */
int find_network_newline(const char *buf, int n) {
    int i = 0;
    while (i + 1 < n){
        if (buf[i] == '\r' && buf[i+1] == '\n') {
            return i + 2;
        }
        i++;
    }
    return -1;
}

int read_line(char *buf, int index, struct sockname *usernames){

    int fd = usernames[index].sock_fd;
    // length of holding
    int inbuf = strlen(buf);           
    // length of remaining
    int room = BUF_SIZE - inbuf;  
    // cursor
    char *after = &buf[inbuf];       
    // position of network newline    
    int where = -1;

    int nbytes;
    while ((nbytes = read(fd, after, room)) >= 0) {
        if (nbytes == 0) {
            for (int i = 0; i< BUF_SIZE; i++){
                buf[i] = '\0'; 
            } 
            inbuf = 0;
            room = BUF_SIZE;
            after = &buf[0];
            continue;
        }
        inbuf += nbytes;

        while ((where = find_network_newline(buf, inbuf)) > 0) {
            // replace network newline with null terminator
            buf[where - 2] = '\0';
            
            // save the rest and clean 
            inbuf = inbuf - where;
            return inbuf;
        }
        // no newline: continue
        room = BUF_SIZE - inbuf;
        after = &buf[inbuf];
    }

    // exit condition: closed
    return -1;
}



/* Read a message from client_index and echo it back to all clients.
 * Return the fd if it has been closed or 0 otherwise.
 */
int read_from(int client_index, struct sockname *usernames, fd_set *listen_fds) {
    int fd = usernames[client_index].sock_fd;

   
    // Receive messages
    char *buf = usernames[client_index].buf;
    int len_next = -1;
    if ((len_next = read_line(buf, client_index, usernames)) < 0){
        // closed
        return fd;
    } else {
        // check buf content
        int c1 = strcmp("stats", buf);
        int c2 = usernames[client_index].type;
        int c3 = strcmp("next", buf);
        if (c2 < 1) {
            perror("wrong type of client");
        }
        if (c2 == 1 && c3 == 0){
            if (stu_list){
                int stu_index = stu_list[0].fd;
                int stu_fd = usernames[stu_index].sock_fd;
                if (next_overall(usernames[client_index].username, &ta_list, &stu_list) == 1) {
                    perror("Invalid TA name.");
                    exit(1);
                }
                dprintf(stu_fd, "Your turn to see the TA. \r\nWe are disconnecting you from the server now. Press Ctrl-C to close nc\r\n");
                clean_sock(stu_index, usernames);
                clean_buf(buf, len_next);
                return stu_fd;
            }
            if (next_overall(usernames[client_index].username, &ta_list, &stu_list) == 1) {
                    perror("Invalid TA name.");
                    exit(1);
            }
            

        } else if (c1 == 0) { 
            char *output; 
            //
            if (c2 == 1){
                output = print_full_queue(stu_list);
            } else if (c2 == 2){
                output = print_currently_serving(ta_list);
            }
           
            // output to client
            if (write(fd, output, strlen(output)) != strlen(output)){
                // closed
                return fd;
            }
            free(output);
        } else {
            dprintf(fd, "wrong syntax\r\n");
        }

        // prepare for next line
        clean_buf(buf, len_next);    
        return 0;
    }
}

/* Read the username from client_index, and modified the structaccordingly.
 * Return the fd if it is closed or 0 otherwise.
 */
int read_name(int client_index, struct sockname *usernames) {
    // check
    if (usernames[client_index].username != NULL){
        perror("client already named.");
        exit(1);
    }
    // set up
    int fd = usernames[client_index].sock_fd;

   
    // read name
    char *buf = malloc(sizeof(char) * BUF_SIZE);
    buf[0] = '\0';
    usernames[client_index].buf = buf;
    int len_next = -1;
    if ((len_next = read_line(buf, client_index, usernames)) < 0){
        // closed
        return fd;
    } else {
        char* name = malloc(sizeof(char)*strlen(buf));
        name[strlen(buf)] = '\0';
        strncpy(name, buf, strlen(buf));
        usernames[client_index].username = name;
        // instruction
        dprintf(fd, "Hello, %s. Are you a TA or a Student? (enter T or S)\r\n", usernames[client_index].username);

        clean_buf(buf, len_next);
        return 0;
    }     
}



/* Read the type from client_index, and modified the struct accordingly and write to help_centre.
 * Return the fd if it is closed or 0 otherwise.
 */
int read_type(int client_index, struct sockname *usernames) {
    // check
    if (usernames[client_index].username == NULL) {
        perror("client unnamed.");
        exit(1);
    }
    if (usernames[client_index].type != -1){
        perror("client already typed.");
        exit(1);
    }
    //fd
    int fd = usernames[client_index].sock_fd;

    char *buf = usernames[client_index].buf;
    int len_next = -1;
    if ((len_next = read_line(buf, client_index, usernames)) < 0){
        // closed
        return fd;
    } else {
        if (strlen(buf) != 1) {
            dprintf(fd, "Invalid role (enter T or S)\r\n");
        } else if (buf[0] == 'T') {
            usernames[client_index].type = 1;
            add_ta(&ta_list, usernames[client_index].username);
            // instruction
            dprintf(fd, "Valid commands for TA:\r\n     stats\r\n     next\r\n      (or usr Ctrl-C to leave)\r\n");
        } else if (buf[0] == 'S') {
            usernames[client_index].type = 0;
            dprintf(fd, "Valid courses: CSC108, CSC148, CSC209\nWhich course are you asking about?\r\n");
        } else {
            dprintf(fd, "Invalid role (enter T or S)\r\n");
        }

        // prepare for next line
        clean_buf(buf, len_next);    
        return 0;
    }
}

          

/* Read the type from client_index, and modified the struct accordingly and write to help_centre.
 * Return the fd if it is closed or 0 otherwise.
 */
int read_course(int client_index, struct sockname *usernames) {
    // check
    if (usernames[client_index].username == NULL) {
        perror("client unnamed.");
        exit(1);
    }
    if (usernames[client_index].type != 0){
        perror("client not student.");
        exit(1);
    }

    // set up
    int fd = usernames[client_index].sock_fd;
    char *buf = usernames[client_index].buf;
    int len_next = -1;
    if ((len_next = read_line(buf, client_index, usernames)) < 0){
        // closed
        return fd;
    } else {
        if (strlen(buf) != 6){
            dprintf(fd, "This is not a valid course.\r\n");
        } else if ((strncmp("CSC108", buf, 6) == 0) || 
            (strncmp("CSC148", buf, 6)) == 0 || 
            (strncmp("CSC209", buf, 6) == 0)) {
            usernames[client_index].type = 2;
        int result = add_student(&stu_list, client_index, usernames[client_index].username, buf, courses, num_courses); 
        if (result == 1) {
            dprintf(fd, "You are already in line. \r\n");
        } else if (result == 2) {
            dprintf(fd, "This is not a valid course.\r\n");
        }

        // instruction
        dprintf(fd, "%s, you have been entered into the queue. While you wait, you can use the command stats to see which TAs are currently serving students.\r\n", usernames[client_index].username);

        } else {
            dprintf(fd, "This is not a valid course.\r\n");
        }
        // prepare for next line
        clean_buf(buf, len_next);    
        return 0;
    }
}


int main(void) {

    // set up courses
    if ((courses = malloc(sizeof(Course) * 3)) == NULL) {
        perror("malloc for course list\n");
        exit(1);
    }
    strcpy(courses[0].code, "CSC108");
    strcpy(courses[1].code, "CSC148");
    strcpy(courses[2].code, "CSC209");


    // server set up
    struct sockname usernames[MAX_CONNECTIONS];
    for (int index = 0; index < MAX_CONNECTIONS; index++) {
        usernames[index].sock_fd = -1;
        usernames[index].username = NULL;
        usernames[index].type = -1;
        usernames[index].buf = NULL; 
    }

    // Create the socket FD.
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("server: socket");
        exit(1);
    }

    // Set information about the port (and IP) we want to be connected to.
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = INADDR_ANY;

    // This should always be zero. On some systems, it won't error if you
    // forget, but on others, you'll get mysterious errors. So zero it.
    memset(&server.sin_zero, 0, 8);

    // Bind the selected port to the socket.
    if (bind(sock_fd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("server: bind");
        close(sock_fd);
        exit(1);
    }

    // Announce willingness to accept connections on this socket.
    if (listen(sock_fd, MAX_BACKLOG) < 0) {
        perror("server: listen");
        close(sock_fd);
        exit(1);
    }

    // The client accept - message accept loop. First, we prepare to listen to multiple
    // file descriptors by initializing a set of file descriptors.
    int max_fd = sock_fd;
    fd_set all_fds;
    FD_ZERO(&all_fds);
    FD_SET(sock_fd, &all_fds);

    while (1) {
        // select updates the fd_set it receives, so we always use a copy and retain the original.
        fd_set listen_fds = all_fds;
        int nready = select(max_fd + 1, &listen_fds, NULL, NULL, NULL);
        if (nready == -1) {
            perror("server: select");
            exit(1);
        }

        // Is it the original socket? Create a new connection ...
        if (FD_ISSET(sock_fd, &listen_fds)) {
            int client_fd = accept_connection(sock_fd, usernames);
            if (client_fd > max_fd) {
                max_fd = client_fd;
            }
            FD_SET(client_fd, &all_fds);
            printf("Accepted connection\n");
        }

        int done = 0;
        // Next, check the clients.
        // NOTE: We could do some tricks with nready to terminate this loop early.
        for (int index = 0; index < MAX_CONNECTIONS && done < nready; index++) {
            if (usernames[index].sock_fd > -1 && FD_ISSET(usernames[index].sock_fd, &listen_fds)) {
                done += 1;
                // New client
                if (usernames[index].username == NULL){
                // Name
                    read_name(index, usernames);
                } else if (usernames[index].type == -1){
                // TA / Student
                    read_type(index, usernames);
                } else if (usernames[index].type == 0){
                // Student course
                    read_course(index, usernames);
                } else {
                    // Note: never reduces max_fd
                    int client_closed = read_from(index, usernames, &listen_fds);
                    if (client_closed > 0) {
                        FD_CLR(client_closed, &all_fds);
                        printf("%s: Client %d disconnected\n", usernames[index].username, client_closed);
                        if (client_closed == usernames[index].sock_fd) {
                            if (usernames[index].type == 1){
                                if (remove_ta(&ta_list, usernames[index].username) == 1) {
                                    perror("Invalid TA name.");
                                }
                            } else if (usernames[index].type == 2){
                                if (give_up_waiting(&stu_list, usernames[index].username) == 1) {
                                    perror("There was no student by that name waiting in the queue.");
                                }
                            }
                            clean_sock(index, usernames);
                        }  
                        
                    } else {
                        printf("%s: Answering request to %d client %d\n", usernames[index].username, usernames[index].type, usernames[index].sock_fd);
                    }
                }
            }
        }
    }


    // Should never get here.
    return 1;
    
}
