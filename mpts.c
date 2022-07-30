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

#define MAX_NAME_SIZE 100
#define SERVPORT 60500
#define MAX_DGRAM_SIZE 4096
#define MAX_MSG_SIZE 4000
#define READ_SOCK 0
#define WRITE_SOCK 1

typedef struct client_entries
{
    char name[MAX_NAME_SIZE];
    int p_id;
    int t_id;
} client_entries;

typedef struct dgram
{
    int dest_pid;
    int dest_tid;
    char src_name[MAX_NAME_SIZE];
    char dest_name[MAX_NAME_SIZE];
    int msglen;
    char msg[0];
} dgram;
void create_process_pool(pid_t **pool);
pid_t spawn_process(int id);
void sigchld_handler(int sig);
void sigint_handler(int sig);
void sigpipe_handler(int sig);
void clear_client_entry(client_entries *clients, char *name);
int check_client_entry(client_entries *clients, char *name,int * tid, int*pid);
int insert_client_entry(client_entries *clients, char *name, int p_id, int t_id);
void process(int id);
int **create_socket_pool(int num);
void *thread(void *tid);
void accept_lock_init();
void accept_lock_wait();
void accept_lock_release();
void send_dgram(char *src_name, char *dest_name, char *msg, int dest_pid, int dest_tid);
int read_connfd(int connfd,char**name,char**dest_name,char**msg,int*msg_type);
void handle_client(int connfd, int t_id, int sockfd);

int **UNIX_socketpair_list_process = NULL;
int **UNIX_socketpair_list_threads = NULL;
pid_t *pool = NULL;

int shmid, semid, semid_sockpool;
client_entries *clients;

int listenfd;

pthread_mutex_t mutex_rem_clients = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t *mutex_accept;

int p_id = -1;
int rem_clients;
int finished=0;
int N = 10, T = 20, CPP = 3;

void sigchld_handler(int sig)
{
    printf("\nSIGCHLD HANDLER INVOKED");
    if (sig == SIGCHLD)
    {
        if (pool == NULL)
            return;
        for (int i = 0; i < N; i++)
        {
            if (waitpid(pool[i], NULL, WNOHANG) == pool[i])
            {
                pool[i] = spawn_process(i);
            }
        }
    }
}
void sigint_handler(int sig)
{
    printf("\nSIGINT Handler invoked");
    signal(SIGCHLD,sigpipe_handler);
    {
        if (pool != NULL)
        {
            for (int i = 0; i < N; i++)
            {
                int ret=kill(pool[i], SIGKILL);
            }
        }
        shmctl(shmid, IPC_RMID, NULL);
        semctl(semid, 0, IPC_RMID);
        semctl(semid_sockpool,0,IPC_RMID);
    }
    exit(0);
}

void sigpipe_handler(int sig){
    return;
}
void accept_lock_init()
{
    int fd;
    pthread_mutexattr_t mattr;
    fd = open("/dev/zero", O_RDWR, 0);
    mutex_accept = mmap(0, sizeof(pthread_mutex_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    close(fd);
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(mutex_accept, &mattr);
}
void accept_lock_wait()
{
    pthread_mutex_lock(mutex_accept);
}
void accept_lock_release()
{
    pthread_mutex_unlock(mutex_accept);
}

void create_process_pool(pid_t **pool)
{
    *pool = (pid_t *)calloc(N, sizeof(pid_t));
    for (int i = 0; i < N; i++)
    {
        (*pool)[i] = spawn_process(i);
    }
}

pid_t spawn_process(int id)
{
    pid_t ret = fork();
    if (ret < 0)
    {
        perror("fork");
    }
    if (ret == 0)
    {
        process(id);
    }
    return ret;
}
void clear_client_entry(client_entries *clients, char *name)
{
    struct sembuf operation[3];
    operation[0].sem_num = 1;
    operation[0].sem_op = 0;
    operation[1].sem_num = 0;
    operation[1].sem_op = 0;
    operation[2].sem_num = 1;
    operation[2].sem_op = 1;
    semop(semid, operation, 3);
    for (int i = 0; i < N * T; i++)
    {
        if (strcmp(clients[i].name, name) == 0)
        {
            memset(&clients[i], '\0', sizeof(client_entries));
            operation[0].sem_num = 1;
            operation[0].sem_op = -1;
            semop(semid, operation, 1);
            return;
        }
    }
    operation[0].sem_num = 1;
    operation[0].sem_op = -1;
    semop(semid, operation, 1);
}
int check_client_entry(client_entries *clients, char *name,int * tid, int*pid)
{
    struct sembuf operation[2];
    operation[0].sem_num = 1;
    operation[0].sem_op = 0;
    operation[1].sem_num = 0;
    operation[1].sem_op = 1;
    int ret=semop(semid, operation, 2);
    for (int i = 0; i < N * T; i++)
    {
        if (strcmp(clients[i].name, name) == 0)
        {
            operation[0].sem_num = 0;
            operation[0].sem_op = -1;
            semop(semid, operation, 1);
            *tid=clients[i].t_id;
            *pid=clients[i].p_id;
            return i;
        }
    }
    operation[0].sem_num = 0;
    operation[0].sem_op = -1;
    semop(semid, operation, 1);
    return -1;
}

int insert_client_entry(client_entries *clients, char *name, int p_id, int t_id)
{
    struct sembuf operation[3];
    operation[0].sem_num = 1;
    operation[0].sem_op = 0;
    operation[1].sem_num = 0;
    operation[1].sem_op = 0;
    operation[2].sem_num = 1;
    operation[2].sem_op = 1;
    int ret=semop(semid, operation, 3);

    for(int i=0;i<N*T;i++){
        if(strcmp(clients[i].name, name) == 0){
            operation[0].sem_num = 1;
            operation[0].sem_op = -1;
            ret=semop(semid, operation, 1);
            return -1; //client entry already exists
        }
    }
    for (int i = 0; i < N * T; i++)
    {
        if (strlen((char *)&(clients[i])) == 0)
        {
            clients[i].t_id = t_id;
            clients[i].p_id = p_id;
            strcpy(clients[i].name, name);

            operation[0].sem_num = 1;
            operation[0].sem_op = -1;
            ret=semop(semid, operation, 1);
            return 1;
        }
    }
}
void process(int id)
{
    
    p_id = id;
    rem_clients = CPP;
    UNIX_socketpair_list_threads = create_socket_pool(T);

    //create T threads
    for (int i = 0; i < T; i++)
    {
        pthread_t thread_id;
        int t_id=i;
        int ret = pthread_create(&thread_id, NULL, thread, (void*)t_id);
        
        if (ret != 0)
        {
            perror("thread_create");
        }
    }
    dgram *recvbuf = (dgram *)malloc(MAX_DGRAM_SIZE);
    while (1)
    {
        //wait on its unix domain socket
        int dgram_size = recv(UNIX_socketpair_list_process[p_id][READ_SOCK], (void *)recvbuf, MAX_DGRAM_SIZE, 0);
        if (dgram_size > 0)
        {
            send(UNIX_socketpair_list_threads[recvbuf->dest_tid][WRITE_SOCK], (void *)recvbuf, dgram_size, 0);
        }
    }
}


int **create_socket_pool(int num)
{
    int **socket_pool = (int **)calloc(num, sizeof(int *));
    for (int i = 0; i < num; i++)
    {
        socket_pool[i] = (int *)calloc(2, sizeof(int));
    }

    for (int i = 0; i < num; i++)
    {
        int sockets[2];
        if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets) >= 0)
        {
            socket_pool[i][0] = sockets[0];
            socket_pool[i][1] = sockets[1];
        }
        else
        {
            perror("socketpair");
        }
    }
    return socket_pool;
}

void *thread(void *tid)
{
    int t_id = (int)tid;
    struct sockaddr_in pclientaddr;
    int len = sizeof(pclientaddr);
    char *name = (char *)malloc(100);
    printf("\np_id: %d, t_id: %d Created", p_id, t_id);
    while (1)
    {
        accept_lock_wait();

        pthread_mutex_lock(&mutex_rem_clients);
        if (rem_clients == 0)
        {
            accept_lock_release();
            pthread_mutex_unlock(&mutex_rem_clients);
            printf("\np_id: %d, t_id: %d CPP clients already handled.... thread exiting", p_id, t_id);
            pthread_exit(NULL);
            printf("p_id: %d, t_id: %d EXIT FAILED,\n", p_id, t_id);

            continue;
        }
        pthread_mutex_unlock(&mutex_rem_clients);

        printf("\np_id: %d, t_id: %d Waiting for connection....", p_id, t_id);

        int connfd = accept(listenfd, (struct sockaddr *)&pclientaddr, &len);
        if (connfd < 0)
            perror("accept");
        if (connfd > 0)
        {
            printf("\np_id: %d, t_id: %d Connection established", p_id, t_id);
            pthread_mutex_lock(&mutex_rem_clients);
            rem_clients--;
            printf("\np_id: %d remaining clients that can be handled= %d", p_id, rem_clients);
            pthread_mutex_unlock(&mutex_rem_clients);
        }

        accept_lock_release();
        printf("\np_id: %d, t_id%d: Accept lock released", p_id, t_id);
        if (connfd <= 0)
            continue;
        handle_client(connfd,t_id,UNIX_socketpair_list_threads[t_id][READ_SOCK]);
        pthread_mutex_lock(&mutex_rem_clients);
        finished += 1;
        printf("\npid:%d ,tid:%d finished handling client, Total clients finished handling by p_id =%d",p_id,t_id,finished);
        if (finished == CPP)
        {
            exit(0);
        }
        pthread_mutex_unlock(&mutex_rem_clients);
    }
}
void handle_client(int connfd, int t_id, int sockfd)
{
    
    fd_set rset, tempset;
    FD_ZERO(&rset);
    FD_ZERO(&tempset);
    FD_SET(sockfd, &rset);
    FD_SET(connfd, &rset);
    dgram *recvbuf = (dgram *)malloc(MAX_DGRAM_SIZE);
    char *name = malloc(MAX_NAME_SIZE);
    char *dest_name = malloc(MAX_NAME_SIZE);
    char *msg = malloc(MAX_MSG_SIZE);
    memset(name,'\0',MAX_NAME_SIZE);
    int msg_type;
    while (1)
    {
        memset(dest_name,'\0',MAX_NAME_SIZE);
        memset(msg,'\0',MAX_MSG_SIZE);
        memset(recvbuf,'\0',MAX_DGRAM_SIZE);
        tempset = rset;
        int nfds = connfd > sockfd ? connfd + 1 : sockfd + 1;
        // printf("p_id: %d, t_id: %d select\n", p_id, t_id);
        if (select(nfds, &tempset, NULL, NULL, NULL) > 0)
        {
            // printf("p_id: %d, t_id: %d select\n", p_id, t_id);
            if (FD_ISSET(sockfd, &tempset) > 0)
            {
                // printf("p_id: %d, t_id: %d writing to connfd", p_id, t_id);

                int dgram_size = recv(sockfd, (void *)recvbuf, MAX_DGRAM_SIZE, 0);
               // printf("%d bytes recv",dgram_size);
                if (dgram_size > 0)
                {
                    
                    if (recvbuf->dest_tid == t_id && recvbuf->dest_pid == p_id && (strcmp(recvbuf->dest_name, name) == 0))
                    {
                        char *msg = (char *)&(recvbuf->msg);
                       
                        write(connfd, recvbuf->src_name, strlen(recvbuf->src_name));
                        write(connfd, ": ", 2);
                        write(connfd, (void *)msg, strlen(msg) + 1);
                    }
                }
                else{
                    printf("ERROR ON RECV");
                }                
            }
            if (FD_ISSET(connfd, &tempset) > 0)
            {
                int ret=read_connfd(connfd,&name,&dest_name,&msg,&msg_type);
                //  printf("p_id: %d, t_id: %d reading connfd\n", p_id, t_id);
                if(ret==0){
                    //Socket closed;
                    if(strlen(name)!=0)
                    clear_client_entry(clients,name);
                    // printf("closing connfd");
                    close(connfd);
                    break;
                }
               
                else if(ret<0){
                    // printf("\nret =-1");
                    continue;
                }
                if(msg_type==1){
                    int check=insert_client_entry(clients,name,p_id,t_id);
                    if(check ==-1){
                        write(connfd,"Username already in use",24);
                    }
                    else{
                        printf("\nUser %s created", name);
                    }
                }
                else if(msg_type==3){
                    if(strlen(name)!=0)
                    clear_client_entry(clients,name);
                    // printf("closing connfd");
                    close(connfd);
                    break;
                }
                else if(msg_type==2){
                    int dest_tid;
                    int dest_pid;
                    if(strlen(name)==0){
                        write(connfd,"Please provide a user name before sending a message",52);
                        // printf("\nNo username provided");
                        continue;
                    }
                    int check=check_client_entry(clients,dest_name,&dest_tid,&dest_pid);
                    if(check==-1){
                        write(connfd,"Destination client does not exist",34);
                        // printf("\nDEStination client not existing");
                    }
                    else{
                        // printf("%d:dest_pid,%d:dest_tid,%s:src_name,%s:msg",dest_pid,dest_tid,name,msg);
                        send_dgram(name,dest_name,msg,dest_pid,dest_tid);                       
                    }
                }
            }
        }
    }
    free((void *)recvbuf);
    free((void *)name);
    free((void *)dest_name);
    free((void *)msg);
}

int read_connfd(int connfd,char**name,char**dest_name,char**msg,int*msg_type){
    char* buf=(char*)malloc(MAX_NAME_SIZE+MAX_MSG_SIZE+10);
    int i;
    for(i=0;i<MAX_MSG_SIZE+MAX_NAME_SIZE+10;i++){
        char c;
        int byte_read=read(connfd,(void*)&c,1);
        if(byte_read<=0){
            free(buf);
            return 0;
        } 
        if(c=='\0') break;
        else buf[i]=c;
    }
    if(i>=MAX_MSG_SIZE+MAX_NAME_SIZE+10){
        free((void*)buf);
        return -1; //Message length exceeded
    }
    buf[4]='\0';
    if(strcmp(buf,"JOIN")==0){
        *msg_type=1;
    }
    else if(strcmp(buf,"CHAT")==0){
        *msg_type=2;
    }
    else if(strcmp(buf,"LEAV")==0){
        free(buf);
        *msg_type=3;
        return 1;
    }
    else{
        free(buf);
        return -2;//INVALID MESSAGE TYPE
    }
    if(*msg_type==1){
        if(i-5>MAX_NAME_SIZE-1){
            free(buf);
            return -3;//name length exceeded
        }
        for(int j=5;j<=i;j++){
            if(buf[j]==' '){
                free(buf);
                return -4;//invalid name, cannot contain a space
            }
            (*name)[j-5]=buf[j];
        }
        if(strlen(*name)==0){
            free(buf);
            return -4;
        }
        return 1;
    }
    if(*msg_type==2){
        int j;
        for(j=5;buf[j]!=' '&&buf[j]!='\0';j++){
            (*dest_name)[j-5]=buf[j];
        }
        if(buf[j]=='\0'){
            free(buf);
            return -5; //Content legnth of message is 0
        }
        else{
            (*dest_name)[j-5]='\0';
        }
        for(int k=j+1;k<=i;k++){
            (*msg)[k-j-1]=buf[k];
        }
        if(strlen(*msg)==0){
            free(buf);
            return -5;
        }
       
        return 1;
    }
}
void send_dgram(char *src_name, char *dest_name, char *msg, int dest_pid, int dest_tid)
{
    dgram *sendbuf = (dgram *)malloc(MAX_DGRAM_SIZE);
    strcpy(sendbuf->dest_name, dest_name);
    strcpy(sendbuf->src_name, src_name);
    sendbuf->dest_pid = dest_pid;
    sendbuf->dest_tid = dest_tid;
    sendbuf->msglen = strlen(msg) + 1; //+1 for null character
    strcpy(sendbuf->msg, msg);
   

    //gain exclusive access to semaphore
    struct sembuf operation[2];
    operation[0].sem_num = dest_pid ;
    operation[0].sem_op = 0;
    operation[1].sem_num = dest_pid ;
    operation[1].sem_op = 1;
    semop(semid_sockpool, operation, 2);

    send(UNIX_socketpair_list_process[dest_pid][WRITE_SOCK], sendbuf, sizeof(dgram) + strlen(msg), 0);
    //unlock
    operation[0].sem_num = dest_pid;
    operation[0].sem_op = -1;
    semop(semid_sockpool, operation, 1);
}
int main(int argc,char* argv[])
{
    if(argc!=5){
        printf("\nProvide port num, N, T, CPP respectively");
        exit(0);
    }
    int port_num;
    port_num=atoi(argv[1]);
    N=atoi(argv[2]);
    T=atoi(argv[3]);
    CPP=atoi(argv[4]);
    signal(SIGPIPE,sigpipe_handler);
    struct sockaddr_in localaddr;
    localaddr.sin_addr.s_addr = INADDR_ANY;
    localaddr.sin_family = AF_INET;
    localaddr.sin_port = htons(port_num);
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0)
    {
        perror("socket");
    }
    if (bind(listenfd, (struct sockaddr *)&localaddr, sizeof(localaddr)) < 0)
    {
        perror("bind");
    }
    listen(listenfd, 5);

    accept_lock_init();
    setbuf(stdout, NULL);

    

    shmid = shmget(IPC_PRIVATE, N * T * sizeof(client_entries), IPC_CREAT | 0666);
    clients = (client_entries *)shmat(shmid, NULL, 0);

    semid = semget(IPC_PRIVATE, 2, IPC_CREAT | 0666);
    semid_sockpool = semget(IPC_PRIVATE, N, IPC_CREAT | 0666);
    semctl(semid, 1, SETVAL, 0);
    semctl(semid, 2, SETVAL, 0);
    for (int i = 1; i <= N; i++)
    {
        semctl(semid_sockpool, i, SETVAL, 0);
    }

    UNIX_socketpair_list_process = create_socket_pool(N);
    create_process_pool(&pool);
    signal(SIGCHLD, sigchld_handler);
    signal(SIGINT, sigint_handler);

    while (1)
        pause();
}