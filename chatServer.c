/**
 * @author Amit Cohen
 * @date 02/01/2022
 * @version 0.1
 * @brief  The function of the chat is to forward each incoming message over all client connections (i.e., to all clients) except for the client connection over which the message was received.
 */

/** Libraries **/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/time.h>
#include <netinet/in.h>
#include "chatServer.h"

/** Define **/
#define FALSE 0
#define TRUE 1


typedef struct conn conn;
typedef struct msg msg;
static int end_server = FALSE;
char global_buffer[BUFFER_SIZE];

/** Private Functions **/
void intHandler(int SIG_INT);
int create_chat_server(int port);
int find_the_second_maxFD(conn_pool_t *pool);
msg * create_new_msg(char* buffer,int len);
void remove_all_msg_for_conn(conn * con_ptr);
int add_msg_to_list(conn *pConn, char *buffer, int len);
void destroy_connection_pool(conn_pool_t * pool);
char * read_from_client(int sd,conn_pool_t * pool);

int main (int argc, char *argv[])
{
    if(argc!=2){
        printf("Usage: chatServer <port> \n");
        exit(EXIT_FAILURE);
    }
    char *ptr;
    int port=(int)strtol(argv[1], &ptr, 10);
    if(port<0){
        printf("Usage: chatServer <port> \n");
        exit(EXIT_FAILURE);
    }
    signal(SIGINT, intHandler);
    conn_pool_t* pool = malloc(sizeof(conn_pool_t));
    if(!pool){
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    init_pool(pool);
    int socket_fd=create_chat_server(port),client_fd;
    if(socket_fd<0){
        free(pool);
        exit(EXIT_FAILURE);
    }
    int s_fd=socket_fd;
    char*buffer;
    FD_SET(socket_fd,&pool->read_set);
    pool->maxfd=socket_fd;
    int count=0;
    do
    {
        if(!pool->maxfd)
            pool->maxfd=socket_fd;
        pool->ready_write_set=pool->write_set;
        pool->ready_read_set=pool->read_set;
        printf("Waiting on select()...\nMaxFd %d\n", pool->maxfd);
        pool->nready=select(pool->maxfd+1,&pool->ready_read_set,&pool->ready_write_set,NULL,NULL);
        if(pool->nready==-1){
            destroy_connection_pool(pool);
            exit(EXIT_FAILURE);
        }
        for (int fd=0; fd <= pool->maxfd && pool->nready > 0; fd++)
        {
            if (FD_ISSET(fd, &pool->ready_read_set))
            {
                count++;
                pool->nready--;
                if(fd == socket_fd) {
                    client_fd = accept(socket_fd, NULL, NULL);
                    printf("New incoming connection on sd %d\n", client_fd);
                    if (client_fd < 0)
                        continue;
                    add_conn(client_fd,pool);
                    continue;
                }
                buffer=read_from_client(fd,pool);
                if(buffer) {
                    add_msg(fd, buffer, (int) strlen(buffer), pool);
                    free(buffer);
                }
                continue;
            }
            if (FD_ISSET(fd, &pool->ready_write_set)&&count>10) {
                pool->nready--;
                write_to_client(fd, pool);
            }
        }
    } while (end_server == FALSE);
   destroy_connection_pool(pool);
    printf("removing connection with sd %d \n", s_fd);
    exit(EXIT_SUCCESS);
}
int init_pool(conn_pool_t* pool) {
    pool->conn_head=NULL;
    pool->nready=0;
    pool->maxfd=0;
    pool->nr_conns=0;
    FD_ZERO(&pool->read_set);
    FD_ZERO(&pool->ready_read_set);
    FD_ZERO(&pool->write_set);
    FD_ZERO(&pool->ready_write_set);
    return 0;
}
int add_conn(int sd, conn_pool_t* pool) {
    conn *new_conn = (conn *) malloc(sizeof(conn));
    if(!new_conn)
        return -1;
    new_conn->write_msg_tail=NULL;
    new_conn->write_msg_head=NULL;
    new_conn->next=NULL;
    new_conn->prev=NULL;
    new_conn->fd = sd;
    FD_SET(sd,&pool->read_set);
    if(pool->conn_head) {
        new_conn->next = pool->conn_head;
        pool->conn_head->prev = new_conn;
        pool->conn_head = new_conn;
        pool->nr_conns++;
        if (sd > pool->maxfd)
            pool->maxfd = sd;
        return 0;
    }
    pool->conn_head=new_conn;
    pool->conn_head->next=NULL;
    pool->conn_head->prev=NULL;
    pool->maxfd=sd;
    pool->nr_conns++;
    return 0;
}
int remove_conn(int sd, conn_pool_t* pool) {
    printf("removing connection with sd %d \n", sd);
    conn * ptr=pool->conn_head;
    if(pool->maxfd==sd&&!end_server)
        pool->maxfd= find_the_second_maxFD(pool);
    if(ptr->fd==sd) {
        pool->conn_head = ptr->next;
        if(pool->conn_head)
            pool->conn_head->prev=NULL;
    }
    while (ptr!=NULL) {
        if (ptr->fd == sd)
            break;
        ptr = ptr->next;
    }
    if(ptr->prev&&pool->nr_conns>1)
        ptr->prev->next=ptr->next;
    remove_all_msg_for_conn(ptr);
    free(ptr);
    ptr=NULL;
    FD_CLR(sd,&pool->read_set);
    FD_CLR(sd,&pool->write_set);
    close(sd);
    pool->nr_conns--;
    return 0;
}
int add_msg(int sd,char* buffer,int len,conn_pool_t* pool) {
    conn *ptr=pool->conn_head;
    while (ptr!=NULL){
        if(ptr->fd!=sd) {
            if ((add_msg_to_list(ptr, buffer, len)) < 0)
                return -1;
            FD_SET(ptr->fd,&pool->write_set);
        }
        ptr=ptr->next;
    }
    return 0;
}
int write_to_client(int sd,conn_pool_t* pool) {
    conn *sd_ptr = pool->conn_head;
    while (sd_ptr) {
        if (sd_ptr->fd == sd) {
            msg *msg_ptr = sd_ptr->write_msg_tail;
            if (write(sd_ptr->fd, msg_ptr->message, msg_ptr->size) < 0)
                return -1;
            if (sd_ptr->write_msg_tail != sd_ptr->write_msg_head) {
                sd_ptr->write_msg_tail = sd_ptr->write_msg_tail->prev;
                if (sd_ptr->write_msg_tail)
                    sd_ptr->write_msg_tail->next = NULL;
            }
            else{
                sd_ptr->write_msg_head=NULL;
                sd_ptr->write_msg_tail=NULL;
            }
            free(msg_ptr->message);
            free(msg_ptr);
            if (!sd_ptr->write_msg_head)
                FD_CLR(sd_ptr->fd, &pool->write_set);
            break;
        }
        sd_ptr = sd_ptr->next;
    }
    return 0;
}
/**
 *This function receives one client and adds one message to the top of its message queue.
 * @param pConn the client
 * @param buffer The message
 * @param len size of the  message
 * @return 0 If successful, otherwise -1.
 */
int add_msg_to_list(conn *pConn, char *buffer, int len) {
    msg * new_msg= create_new_msg(buffer,len);
    if(!new_msg)
        return -1;
    new_msg->next=NULL;
    if(!pConn->write_msg_head){
        pConn->write_msg_head=new_msg;
        pConn->write_msg_tail=new_msg;
        pConn->write_msg_head->next=pConn->write_msg_tail;
        pConn->write_msg_tail->prev=pConn->write_msg_head;
        pConn->write_msg_tail->next=NULL;
        return 0;
    }
    new_msg->next=pConn->write_msg_head;
    pConn->write_msg_head->prev=new_msg;
    pConn->write_msg_head=new_msg;
    return 0;
}
void intHandler(int SIG_INT){
    end_server=TRUE;
}
/**
 *This function finds the second maximum in the fd
 * @param pool the queue.
 * @return the second max fd.
 */
int find_the_second_maxFD(conn_pool_t *pool){
    int s_max=0,max=pool->maxfd;
    conn * ptr=pool->conn_head;
    while (ptr!=NULL){
        if(ptr->fd>s_max&&ptr->fd!=max)
            s_max=ptr->fd;
        ptr=ptr->next;
    }
    return s_max;
}
/**
 *This function produces a msg type structure and returns it.
 * @param buffer The message
 * @param len size of the  message
 * @return the new msg.
 */
msg * create_new_msg(char* buffer,int len){
    msg * new_msg=(msg*) malloc(sizeof (msg));
    if(!new_msg)
        return NULL;
    new_msg->size=len;
    new_msg->message=(char*) malloc(sizeof (char)*len+1);
    if(!new_msg->message)
        return NULL;
    memset(new_msg->message,'\0',len+1);
    strcpy(new_msg->message,buffer);
    new_msg->next=NULL;
    new_msg->prev=NULL;
    return new_msg;
}
/**
 *This function removes all messages from the client
 * @param con_ptr the client
 */
void remove_all_msg_for_conn(conn * con_ptr){
    msg *current = NULL, *next = NULL;
    current = con_ptr->write_msg_head;
    while (current != NULL) {
        next = current->next;
        free(current->message);
        free(current);
        current = next;
    }
}
/**
 *This function deletes the pool of clients and frees up all memory.
 * @param pool the pool of clients
 */
void destroy_connection_pool(conn_pool_t * pool){
    conn *current = NULL, *next = NULL;
    current = pool->conn_head;
    while (current != NULL) {
        next = current->next;
        remove_conn(current->fd,pool);
        current = next;
    }
    free(pool);
}
/**
 *This function creates a server-socket and returns its fd.
 * @param port the port to open the server-socket.
 * @return the fd.
 */
int create_chat_server(int port) {
    struct sockaddr_in serv_addr;
    int  sockfd=socket(AF_INET,SOCK_STREAM,0),on=1;
    if(sockfd<0) {
        perror("error: opening socket\n");
        return -1;
    }
    if(ioctl(sockfd,(int)FIONBIO,(char*)&on)<0){
        perror("error: ioctl\n");
        return -1;
    }
    serv_addr.sin_family =AF_INET;
    serv_addr.sin_addr.s_addr =INADDR_ANY;
    serv_addr.sin_port=htons(port);
    if(bind(sockfd,(struct  sockaddr*)&serv_addr,sizeof (serv_addr))<0){
        perror("error: on binding\n");
        return -1;
    }
    if(listen(sockfd,5)<0){
        perror("error: on listening\n");
        return -1;
    }
    return sockfd;
}
/**
 *This function receives an sd which calls it a buffer and returns the buffer.
 * @param sd the client.
 * @param pool the queue.
 * @return the buffer.
 */
char * read_from_client(int sd,conn_pool_t * pool){
    char * buffer=(char*) malloc(sizeof (char )*BUFFER_SIZE+1),*ptr;
    if(!buffer)
        return NULL;
    memset(buffer,'\0',BUFFER_SIZE+1);
    int count_read,offset=0;
    printf("Descriptor %d is readable\n", sd);
    count_read=(int)read(sd,buffer,BUFFER_SIZE);
    if(!count_read){
        printf("Connection closed for sd %d\n",sd);
        free(buffer);
        remove_conn(sd,pool);
        return NULL;
    }
    int size=(int)strlen(buffer);
    printf("%d bytes received from sd %d\n",size, sd);
    return buffer;
}