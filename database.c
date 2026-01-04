#define _POSIX_C_SOURCE 200809L 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include "tdIOlib/tdIOlib.h"

#define FEATURE_MAX 1024
#define BUFF_MAX_SIZE (FEATURE_MAX * 2)

void chld_handler(int signo){
    (void) signo;
    int saved_errno = errno;
    while(waitpid(-1, NULL, WNOHANG) > 0);
    errno = saved_errno;
}

int main(int argc, char **argv){
    if(argc != 2){
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // defs
    int err, sp;
    struct addrinfo hints, *res;
    struct sigaction sig;

    // handling SIGPIPE & SIGCHLD
    memset(&sig, 0, sizeof(sig));
    sigemptyset(&sig.sa_mask);
    sig.sa_handler = chld_handler;
    sig.sa_flags = SA_RESTART; // if sigchld interrupts a system call, the system call is restarted

    err = sigaction(SIGCHLD, &sig, NULL);
    if(err < 0){
        perror("sigaction CHLD");
        exit(EXIT_FAILURE);
    }

    sigemptyset(&sig.sa_mask);
    sig.sa_handler = SIG_IGN;

    err = sigaction(SIGPIPE, &sig, NULL);
    if(err < 0){
        perror("sigaction PIPE");
        exit(EXIT_FAILURE);
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    err = getaddrinfo(NULL, argv[1], &hints, &res);
    if(err != 0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        exit(EXIT_FAILURE);
    }

    sp = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if(sp < 0){
        perror("socket");
        freeaddrinfo(res);
        exit(EXIT_FAILURE);
    }

    int enable = 1;
    err = setsockopt(sp, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    if(err < 0){
        perror("setsockopt");
        close(sp);
        freeaddrinfo(res);
        exit(EXIT_FAILURE);
    }

    err = bind(sp, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if(err < 0){
        perror("bind");
        close(sp);
        exit(EXIT_FAILURE);
    }

    err = listen(sp, SOMAXCONN);
    if(err < 0){
        perror("listen");
        close(sp);
        exit(EXIT_FAILURE);
    }

    for(;;){
        int sa, nameinfoerr;
        pid_t p_main;
        char host[128], port[10];
        struct sockaddr_storage client_addr;
        socklen_t caddr_len = sizeof(client_addr);

        sa = accept(sp, (struct sockaddr *) &client_addr, &caddr_len);
        if(sa < 0){
            perror("accept");
            continue;
        }

        nameinfoerr = getnameinfo((struct sockaddr *) &client_addr, caddr_len,
                        host, sizeof(host), port, sizeof(port),
                        NI_NUMERICHOST | NI_NUMERICSERV);
        if(nameinfoerr != 0)
            fprintf(stderr, "getnameinfo: %s\n", gai_strerror(err));
        else
            printf("client: %s : %s connected\n", host, port);

        p_main = fork();
        if(p_main < 0){
            perror("fork");
            close(sa);
            continue;
        }else if(p_main == 0){ /* handling client request here */
            close(sp);

            int flag = 0;
            IObuff_t IObuff;
            IObuff_init(&IObuff, BUFF_MAX_SIZE);

            while(flag == 0){
                char row_path[FEATURE_MAX+1], path[FEATURE_MAX+3];
                size_t s_row_path = sizeof(row_path)-1;

                memset(row_path, 0, sizeof(row_path));
                err = td_readline(sa, row_path, &s_row_path, &IObuff);
                if(err < 0){
                    fprintf(stderr, "readline\n");
                    break;
                }

                memset(path, 0, sizeof(path));
                snprintf(path, sizeof(path)-1, "./%s", row_path);
                int fd = open(path, O_RDONLY);
                if(fd < 0){ // server problem: here full restart is needed
                    perror("open");
                    close(sa);
                    IObuff_remove(&IObuff);
                    exit(EXIT_FAILURE);
                }

                struct stat st;
                if(stat(path, &st) == -1){ // error
                    perror("stat");
                    break;
                }

                char file_size[34];
                memset(file_size, 0, sizeof(file_size));
                snprintf(file_size, sizeof(file_size)-1, "%lld\n", (long long) st.st_size);
                err = write_everything(sa, file_size, strlen(file_size));
                if(err < 0){
                    perror("write");
                    break;
                }

                err = send_all_file(fd, sa);
                if(err < 0){
                    perror("sendfile");
                    break;
                }

                flag++;
            }

            if(nameinfoerr == 0)
                printf("client: %s : %s disconnected\n", host, port);
            close(sa);
            IObuff_remove(&IObuff);
            exit(EXIT_SUCCESS);
        }else{
            close(sa);
        }

    }

    close(sp);
    return EXIT_SUCCESS;
}
