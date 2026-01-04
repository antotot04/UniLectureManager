#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include "tdIOlib.h"

/* ************************* basics *************************** */

/* Thi function initialize an IObuff_t buffer */
void IObuff_init(IObuff_t *IObuff, size_t buffer_size){
    IObuff->buffer = malloc(buffer_size);
    if(IObuff->buffer == NULL){
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    IObuff->read = 0;
    IObuff->size = buffer_size;
    IObuff->ref_count = 1;

    memset(IObuff->buffer, 0, buffer_size);
}

/* This function manages the reference counting of IObuff.
It automatiacally free buffer space if reference count is 0 */
void IObuff_remove(IObuff_t *buff){
    buff->ref_count--;
    if(buff->ref_count == 0){
        free(buff->buffer);
        buff->buffer = NULL;
    }
}

/* ************************* read functions *************************** */

/* This function read every single byte from the given "fd" to "dest" buffer. 
Returns:
-> total bytes read: if successuful 
-> -1: if an error occurred */
ssize_t read_everything(int fd, char *dest, size_t dest_size){

    size_t size_to_read = dest_size, total_rdbytes = 0;
    char *dest_ptr = dest;

    while(size_to_read > 0){
        ssize_t curr_rdbytes = read(fd, dest_ptr, size_to_read);
        if(curr_rdbytes < 0){
            if(errno == EINTR) continue; // continue if a signal terminated read
            return -1; // read error 
        }
        else if(curr_rdbytes == 0) break; // nothing left to read

        dest_ptr += (int) curr_rdbytes;
        total_rdbytes += (size_t) curr_rdbytes;
        size_to_read -= (size_t) curr_rdbytes;
    }

    return total_rdbytes;
}


/* This function read a single terminated data line from the given file descriptor "fd"
to the assigned destination buff "dest". Assume that every single line to read has a '\n' at 
the end. 
Returns:
-> 1 if end of the file
-> 0 if successful 
-> -1 if an error occurred 
NOTE: This function remove '\n' from the line and does NOT terminate it with '\0' */
int td_readline(int fd, char *dest, size_t *dest_size, IObuff_t *buff){

    if(*dest_size == 0 || dest == NULL || dest_size == NULL) return -1;

    while(1){
        char *td_ptr;
        td_ptr = memchr(buff->buffer, '\n', buff->read);
        if(td_ptr != NULL){ /* td found */
            size_t len = (size_t) (td_ptr - buff->buffer);

            if(len > *dest_size) return -1; // line to big for dest_size

            memcpy(dest, buff->buffer, len);
            *dest_size = len;

            // update buff removing len + '\n'
            size_t remaining = buff->read - (len + 1);
            if(remaining > 0)
                memmove(buff->buffer, td_ptr + 1, remaining);
            buff->read = remaining;
            
            return 0;
        }
        // nothing in buff... reading bytes from fd to buff
        ssize_t bytes_read = read(fd, buff->buffer+buff->read, buff->size-buff->read);
        if(bytes_read < 0){
            if(errno == EINTR) continue;
            return -1; 
        }
        
        if (bytes_read == 0) {

            if (buff->read == 0) return 1; // EOF, nothing to read anymore

            if (buff->read >= *dest_size) return -1; // dest buffer to small

            // still bytes to read in buff
            memcpy(dest, buff->buffer, buff->read);
            *dest_size = buff->read;
            buff->read = 0;
            return 0;
        }

        buff->read += (size_t) bytes_read;
    }

    return 0;
}

/* ************************* write functions *************************** */

/* This function writes every single byte from the given "src" buffer to "fd". 
Returns:
-> total bytes wrote: if successuful 
-> -1: if an error occurred */
ssize_t write_everything(int fd, char *src, size_t src_len){

    size_t size_to_write = src_len, total_wrbytes = 0;
    char *src_ptr = src;

    while(size_to_write > 0){
        ssize_t curr_wrbytes = write(fd, src_ptr, size_to_write);
        if(curr_wrbytes < 0){
            if(errno == EINTR) continue; // continue if a signal terminated write
            return -1;
        }
        else if(curr_wrbytes == 0) break;

        src_ptr += (int) curr_wrbytes;
        total_wrbytes += (size_t) curr_wrbytes;
        size_to_write -= (size_t) curr_wrbytes;
    }

    return total_wrbytes;
}

/* This function send all the file of src_d from start to finish to dst_d.
Behaviours are the same of sendfile() regarding return values. */
int send_all_file(int src_d, int dst_d){
    off_t off = 0;
    return sendfile(src_d, dst_d, (off_t) 0, &off, (struct sf_hdtr *) NULL, 0);
}
