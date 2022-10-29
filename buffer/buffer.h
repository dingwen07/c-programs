#ifndef _BUFFER_H_
#define _BUFFER_H_

#define BUF_INIT_SIZE 128
#define BUF_INCR_RATE 2
#define BUF_INSP_BUF 0
#define BUF_INSP_EXT 1
#define BUF_INSP_ALL 2


typedef struct buffer {
    unsigned int capacity;
    void *data;
    unsigned int len;
} buffer_t;


buffer_t *buffer_new(void);
void buffer_free(buffer_t *buffer);
void buffer_reset(buffer_t *buffer);

void buffer_ensure(buffer_t *buffer, unsigned int size);
void buffer_null_terminate(buffer_t *buffer);

int buffer_append(buffer_t *buffer, const void *data, unsigned int len);
int buffer_copy(buffer_t *dest, const buffer_t *src);
int buffer_compare(const buffer_t *buf1, const buffer_t *buf2);

int buffer_write(int fd, buffer_t *buffer);
int buffer_read(int fd, buffer_t *buffer, unsigned int count);
int buffer_getline(int fd, buffer_t *buffer);
void buffer_inspect(buffer_t *buffer, int mode);

#endif
