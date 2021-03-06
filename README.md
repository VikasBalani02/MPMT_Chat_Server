# MPMT_Chat_Server

This is an implementation of an APACHE-MPM like server architechture. The server application preforks `N` processes each of which then spawn `T` threads, which makes this model preforked and prethreaded. The server can handle atmost `N*T` clients at a time. After the threads of a process have collectively handled `CPP` many clients, the process is killed and replaced with a new process to avoid memory leaks.

The server is designed for a chat application where the users can JOIN with a username and CHAT with other active users. 

The implementation of the server makes use of Linux Socket API, Shared Memory, Mutexes, Semaphores, Forking, Multi-threading, Non-Blocking IO, buffer management, etc.<br />
Implementation details can be found in Design Document.pdf


### Client Commands
**JOIN** : A user can register themself with the mpts server by issuing a `JOIN < username >` command. The usernames of active users are unique and hence users are prompted to use a different alias if their username is already in use. <br />

**CHAT**: `CHAT < username > < message >` is used to send a message to another user.

**LEAV** `LEAV` is used to exit from the application and de-register self from the server.

### Usage
**Server**

```
make 
./mpts <port number> < number of processes 'N' > < number of threads 'T' > <clients per process 'CPP' >
```

**Client**

```
make client
./chat_client < server ip > < server port >
```
