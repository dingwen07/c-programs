#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "buffer.h"


buffer_t *buffer_new(void) {
    buffer_t *buffer = malloc(sizeof(buffer_t));
    buffer->data = malloc(0);
    buffer->len = 0;
    buffer->capacity = 0;
    return buffer;
}


void buffer_free(buffer_t *buffer) {
    free(buffer->data);
    free(buffer);
}


void buffer_reset(buffer_t *buffer) {
    buffer->len = 0;
    buffer_null_terminate(buffer);
}


void buffer_ensure(buffer_t *buffer, unsigned int size) {
    if (buffer->capacity == 0) {
        buffer->data = malloc(BUF_INIT_SIZE);
        buffer->capacity = BUF_INIT_SIZE;
    }
    unsigned int total_size = buffer->len + size;
    if (total_size > buffer->capacity) {
        unsigned int new_capacity = buffer->capacity;
        while (total_size > buffer->capacity) {
            new_capacity *= BUF_INCR_RATE;
        }
        buffer->data = realloc(buffer->data, new_capacity);
        buffer->capacity = new_capacity;
    }
}


void buffer_null_terminate(buffer_t *buffer) {
    buffer_ensure(buffer, 1);
    ((char *)buffer->data)[buffer->len] = '\0';
}


int buffer_append(buffer_t *buffer, const void *data, unsigned int len) {
    if (buffer == NULL || data == NULL) {
        return -1;
    }

    // If the buffer is too small, resize it
    buffer_ensure(buffer, len);

    memcpy(buffer->data + buffer->len, data, len);
    buffer->len += len;
    buffer_null_terminate(buffer);
    return buffer->len;
}


int buffer_copy(buffer_t *dest, const buffer_t *src) {
    if (dest == NULL || src == NULL) {
        return -1;
    }

    buffer_append(dest, src->data, src->len);
    return dest->len;
}


int buffer_compare(const buffer_t *buf1, const buffer_t *buf2) {
    if (buf1 == NULL || buf2 == NULL) {
        return -1;
    }

    if (buf1->len != buf2->len) {
        return buf1->len - buf2->len;
    }

    return memcmp(buf1->data, buf2->data, buf1->len);
}


int buffer_write(int fd, buffer_t *buffer) {
    if (buffer == NULL) {
        return -1;
    }
    if (buffer->len <= 0) {
        return 0;
    }

    int ret = write(fd, buffer->data, buffer->len);
    return ret;
}


int buffer_read(int fd, buffer_t *buffer, unsigned int count) {
    if (buffer == NULL) {
        return -1;
    }

    buffer_ensure(buffer, count);
    int ret = read(fd, buffer->data + buffer->len, count);
    if (ret > 0) {
        buffer->len += ret;
        buffer_null_terminate(buffer);
    }
    return ret;
}


int buffer_getline(int fd, buffer_t *buffer) {
    if (buffer == NULL) {
        return -1;
    }

    if (fd == STDIN_FILENO) {
        fflush(stdout);
    }

    char c;
    while (read(fd, &c, 1) > 0) {
        if (c == EOF) {
            break;
        }
        buffer_append(buffer, &c, 1);
        if (c == '\n') {
            break;
        }
    }
    return buffer->len;
}

void buffer_inspect(buffer_t *buffer, int mode) {
    FILE *fp = popen("timeout 1 xxd 1>&2", "w");
    if (fp == NULL) {
        return;
    }
    unsigned int len;
    if (mode == BUF_INSP_BUF) {
        len = buffer->len;
    } else if (mode == BUF_INSP_EXT) {
        len = buffer->len + 1;
    } else if (mode == BUF_INSP_ALL) {
        len = buffer->capacity;
    } else {
        return;
    }
    fwrite(buffer->data, 1, len, fp);
    pclose(fp);
}