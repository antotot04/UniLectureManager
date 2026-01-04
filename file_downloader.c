#define _POSIX_C_SOURCE 200809L 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/fcntl.h>
#include <errno.h>
#include "tdIOlib/tdIOlib.h"

#define FEATURE_MAX 256
#define LINE_MAX_SIZE 1024
#define BUFF_LINE_MAX (LINE_MAX_SIZE * 2)
#define END_MARKER "__STOP__"

int main(int argc, char **argv){
    if(argc != 5){
        fprintf(stderr, "Usage: %s <directory lookup server> <directory lookup server port> <database server> <database server port> \n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // defs
    int err, sock;
    struct addrinfo hints, *res, *ptr;
    struct sigaction sig;

    // handling SIGPIPE
    memset(&sig, 0, sizeof(sig));
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

    err = getaddrinfo(argv[1], argv[2], &hints, &res);
    if(err != 0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        exit(EXIT_FAILURE);
    }

    /* fallback connection */
    for(ptr = res; ptr != NULL; ptr = ptr->ai_next){
        sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if(sock < 0) continue;

        err = connect(sock, ptr->ai_addr, ptr->ai_addrlen);
        if(err == 0) break;

        close(sock);
    }
    if(ptr == NULL){
        fprintf(stderr, "connection with directory lookup failed\n");
        freeaddrinfo(res);
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(res);

    IObuff_t IObuff;
    char path[LINE_MAX_SIZE+1], course[FEATURE_MAX+1], lesson[FEATURE_MAX+1];
    IObuff_init(&IObuff, BUFF_LINE_MAX);
    for(;;){

        printf("Enter course (e.g., networks_and_internet, calculus_1, calculus_2, ...): ");
        memset(course, 0, sizeof(course));
        if(fgets(course, sizeof(course)-1, stdin) == NULL){
            fprintf(stderr, "Input error from user\n");
            IObuff_remove(&IObuff);
            close(sock);
            exit(EXIT_FAILURE);
        }

        printf("Enter lecture to search (e.g., lecture1, lecture2, ...): ");
        memset(lesson, 0, sizeof(lesson));
        if(fgets(lesson, sizeof(lesson)-1, stdin) == NULL){
            fprintf(stderr, "Input error from user\n");
            IObuff_remove(&IObuff);
            close(sock);
            exit(EXIT_FAILURE);
        }

        err = write_everything(sock, course, strlen(course));
        if(err < 0){
            fprintf(stderr, "write error\n");
            exit(EXIT_FAILURE);
        }

        err = write_everything(sock, lesson, strlen(lesson));
        if(err < 0){
            fprintf(stderr, "write error\n");
            exit(EXIT_FAILURE);
        }

        memset(path, 0, sizeof(path));
        int counter = 0;
        for(;;){
            char line[LINE_MAX_SIZE];
            size_t s_line = sizeof(line)-1;

            memset(line, 0, sizeof(line));
            err = td_readline(sock, line, &s_line, &IObuff);
            if(err < 0){
                fprintf(stderr, "readline\n");
                IObuff_remove(&IObuff);
                close(sock);
                exit(EXIT_FAILURE);
            }

            if(strcmp(line, END_MARKER) == 0) break;
            
            strcpy(path, line);
            counter++;
        }

        if(counter != 0){
            printf("--lecture found--\n");
            break;
        }
            
        printf("lecture not found. Please try again with a correct one.\n\n");
    }

    close(sock);

    char download[32];
    do {
        printf("do you want to proceed with the lecture download? (s/n): ");

        memset(download, 0, sizeof(download));
        if (fgets(download, sizeof(download), stdin) == NULL) {
            fprintf(stderr, "Input error from user\n");
            IObuff_remove(&IObuff);
            exit(EXIT_FAILURE);
        }
        download[strcspn(download, "\n")] = '\0';

        if (strcmp(download, "n") == 0)
            return EXIT_SUCCESS;
    } while (strcmp(download, "n") != 0 && strcmp(download, "s") != 0);


    /* connecting to database server for download operation */
    res = NULL, ptr = NULL; // for safety
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    err = getaddrinfo(argv[3], argv[4], &hints, &res);
    if(err != 0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        IObuff_remove(&IObuff);
        exit(EXIT_FAILURE);
    }

    /* fallback connection */
    for(ptr = res; ptr != NULL; ptr = ptr->ai_next){
        sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if(sock < 0) continue;

        err = connect(sock, ptr->ai_addr, ptr->ai_addrlen);
        if(err == 0) break;

        close(sock);
    }
    if(ptr == NULL){
        fprintf(stderr, "connection with database failed\n");
        freeaddrinfo(res);
        IObuff_remove(&IObuff);
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(res);

    // sending path to database 
    strcat(path, "\n");
    err = write_everything(sock, path, strlen(path));
    if(err < 0){
        fprintf(stderr, "write error\n");
        close(sock);
        IObuff_remove(&IObuff);
        exit(EXIT_FAILURE);
    }

    // read file size:
    char file_size[33];
    size_t s_file_size = sizeof(file_size)-1;
    memset(file_size, 0, sizeof(file_size));
    err = td_readline(sock, file_size, &s_file_size, &IObuff);
    if(err < 0){
        fprintf(stderr, "readline error\n");
        close(sock);
        IObuff_remove(&IObuff);
        exit(EXIT_FAILURE);
    }

    char *endptr = NULL;
    long long fsize = (long long) strtol(file_size, &endptr, 10);

    // download
    ssize_t bytes_read;
    char new_file[(FEATURE_MAX*2)+2], buffer[4096];
    memset(new_file, 0, sizeof(new_file));

    course[strlen(course)-1] = '\0';
    lesson[strlen(lesson)-1] = '\0';
    snprintf(new_file, sizeof(new_file), "%s_%s.pdf", course, lesson);

    int fd_down = open(new_file, O_WRONLY | O_CREAT | O_TRUNC, 0666); // -rw -rw -rw
    if(fd_down < 0){
        fprintf(stderr, "download error\n");
        close(sock);
        IObuff_remove(&IObuff);
        exit(EXIT_FAILURE);
    }

    int bytes_read_counter = 0,
    hash_printed = 0,
    tot_hash = 10;
    while((bytes_read = read_everything(sock, buffer, sizeof(buffer))) > 0){
        if(bytes_read < 0){
            fprintf(stderr, "read error in download\n");
            close(sock);
            IObuff_remove(&IObuff);
            exit(EXIT_FAILURE);
        }
        bytes_read_counter += bytes_read;

        if(fsize > 0){
            float percent_read = (float) bytes_read_counter / (float) fsize;
            int hash_to_print = (int) (percent_read * tot_hash);

            while(hash_printed < hash_to_print && hash_printed < tot_hash){
                int c = 1;
                hash_printed++;
                printf("%d0%% ", hash_printed);
                while(c <= hash_printed){
                    printf("# ");
                    c++;
                }
                printf("\n");
            }

            if(hash_printed == tot_hash) printf("\n");
        }
        
        err = write_everything(fd_down, buffer, bytes_read);
        if(err < 0){
            fprintf(stderr, "write error in download\n");
            close(sock);
            IObuff_remove(&IObuff);
            exit(EXIT_FAILURE);
        }
    }

    IObuff_remove(&IObuff);
    close(fd_down);
    close(sock);
    return EXIT_SUCCESS;
}
