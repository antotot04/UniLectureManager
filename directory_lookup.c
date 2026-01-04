#define _POSIX_C_SOURCE 200809L 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include "tdIOlib/tdIOlib.h"

#define FEATURE_MAX 256
#define BUFF_MAX_SIZE (FEATURE_MAX * 2)
#define END_MARKER "__STOP__"

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
    if(err < 0){
        perror("bind");
        close(sp);
        freeaddrinfo(res);
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(res);

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
        }else if(p_main == 0){ /* directory task here */
            close(sp);

            sigemptyset(&sig.sa_mask);
            sig.sa_handler = SIG_DFL;

            err = sigaction(SIGCHLD, &sig, NULL);
            if(err < 0){
                perror("sigaction PIPE");
                close(sa);
                continue;
            }

            IObuff_t IObuff;
            IObuff_init(&IObuff, BUFF_MAX_SIZE);

            while(1){
                int p1p2[2], status;
                pid_t p1, p2;
                char course[FEATURE_MAX+1], lesson[FEATURE_MAX+1];
                size_t s_course = sizeof(course)-1, s_lesson = sizeof(lesson)-1;

                // extracting features
                memset(course, 0, sizeof(course));
                err = td_readline(sa, course, &s_course, &IObuff);
                if(err < 0){
                    fprintf(stderr, "readline\n");
                    break;
                }

                memset(lesson, 0, sizeof(lesson));
                err = td_readline(sa, lesson, &s_lesson, &IObuff);
                if(err < 0){
                    fprintf(stderr, "readline\n");
                    break;
                }

                err = pipe(p1p2);
                if(err < 0){
                    perror("pipe");
                    break;
                }

                p1 = fork();
                if(p1 < 0){
                    perror("fork");
                    break;
                }else if(p1 == 0){
                    close(sa);
                    close(p1p2[0]);

                    close(1);
                    err = dup(p1p2[1]);
                    if(err < 0){
                        perror("dup");
                        close(p1p2[1]);
                        exit(EXIT_FAILURE);
                    }
                    close(p1p2[1]);

                    execlp("grep", "grep", course, "./dir_register.txt", (char *)0);
                    perror("exec grep");
                    exit(EXIT_FAILURE);
                }

                p2 = fork();
                if(p2 < 0){
                    perror("fork");
                    break;
                }else if(p2 == 0){
                    close(p1p2[1]);

                    close(0);
                    err = dup(p1p2[0]);
                    if(err < 0){
                        perror("dup");
                        close(p1p2[0]);
                        close(sa);
                        exit(EXIT_FAILURE);
                    }
                    close(p1p2[0]);

                    close(1);
                    err = dup(sa);
                    if(err < 0){
                        perror("dup");
                        close(sa);
                        exit(EXIT_FAILURE);
                    }
                    close(sa);

                    execlp("grep", "grep", lesson, (char *)0);
                    perror("exec grep");
                    exit(EXIT_FAILURE);
                }

                close(p1p2[1]);
                close(p1p2[0]);

                waitpid(p1, &status, 0);
                waitpid(p2, &status, 0);

                // needed so the client can clearly understand if he has received the desired path or not
                char *end_marker = END_MARKER "\n";
                err = write_everything(sa, end_marker, strlen(end_marker));
                if(err < 0){
                    perror("write");
                    break;
                }
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
