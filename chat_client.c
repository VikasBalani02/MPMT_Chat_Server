#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <sys/un.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/udp.h>
#include <pthread.h>
#include <signal.h>
#include <wait.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/sem.h>

//define the server port number
#define SERVPORT 60000
int main(){
    struct sockaddr_in servaddr;
    servaddr.sin_family=AF_INET;
    servaddr.sin_port=htons(60000);
    //for testing put the correct IP address in the below statement
    servaddr.sin_addr.s_addr=inet_addr("127.0.0.1");

    int sockfd=socket(AF_INET,SOCK_STREAM,0);
    int ret=connect(sockfd,(struct sockaddr*)&servaddr,sizeof(servaddr));
    char* stdinbuf=malloc(1000);
    char* readbuf=malloc(1000);
    fcntl(sockfd,F_SETFL,O_NONBLOCK);
    fd_set rset;
    FD_ZERO(&rset);
    
    FD_SET(STDIN_FILENO,&rset);
    FD_SET(sockfd,&rset);
    int nfds=sockfd+1;
    fcntl(STDIN_FILENO,F_SETFL,O_NONBLOCK);
    while(1){
        memset(stdinbuf,'\0',1000);
        memset(readbuf,'\0',1000);
        fd_set rset_temp=rset;
        if(select(nfds,&rset_temp,NULL,NULL,NULL)>0){
            if(FD_ISSET(STDIN_FILENO,&rset)==1){
                int bytes_read=read(STDIN_FILENO,stdinbuf,999);
                if(bytes_read>1){
                    stdinbuf[bytes_read-1]='\0';
                    write(sockfd,stdinbuf,strlen(stdinbuf)+1);
                }
            }
            if(FD_ISSET(sockfd,&rset)==1){
                int bytes_read=read(sockfd,readbuf,999);
                if(bytes_read>0){
                    readbuf[bytes_read]='\0';
                    write(STDOUT_FILENO,readbuf,strlen(readbuf)+1);
                    write(STDOUT_FILENO,"\n",1);
                }
                if(bytes_read==-1 && errno!=EAGAIN){
                    exit(0);
                }
            }
        }
    }
}