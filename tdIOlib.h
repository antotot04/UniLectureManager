typedef struct {
    char *buffer; // pointer to the memory in the heap
    size_t read; // bytes read in buffer 
    size_t size; // max size of the buffer
    int ref_count; 
}IObuff_t;


void IObuff_init(IObuff_t *IObuff, size_t buffer_size);
void IObuff_remove(IObuff_t *buff);
ssize_t read_everything(int fd, char *dest, size_t dest_size);
int td_readline(int fd, char *dest, size_t *dest_size, IObuff_t *buff);
ssize_t write_everything(int fd, char *src, size_t src_len);
int send_all_file(int src_d, int dst_d);
