#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <ctype.h>

#define DEFAULT_LISTEN_ADDRESS "0.0.0.0"
#define DEFAULT_PORT 8080
#define BUFSIZE 4096
#define MAX_HEADER_LENGTH 4096

// function prototype
void url_decode(char *dst, const char *src);
void handle_get_request(int client_socket, const char *path);
void handle_put_request(int client_socket, const char *path);
void handle_request(int client_socket, const char *path);
long int get_content_length(const char *headers);
int get_header_length(const char *headers);
bool isValidUTF8(unsigned char *s, size_t length);
bool isUTF8File(FILE *fp);
void sigint_handler(int sig_num);

char HTTP_400[] = "HTTP/1.1 400 Bad Request\r\n\r\n";
char HTTP_404[] = "HTTP/1.1 404 Not Found\r\n\r\n";
char HTTP_403[] = "HTTP/1.1 403 Forbidden\r\n\r\n";
char HTTP_500[] = "HTTP/1.1 500 Internal Server Error\r\n\r\n";

char UPLOAD_HTML[BUFSIZE] =
"<!DOCTYPE html>\n"
"<html lang=\"en\">\n"
"<head>\n"
"    <meta charset=\"UTF-8\">\n"
"    <title>File Upload</title>\n"
"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
"</head>\n"
"<body>\n"
"    <h1>File Upload</h1>\n"
"    <form id=\"uploadForm\">\n"
"        <label for=\"urlBox\">URL:</label><br>\n"
"        <input type=\"text\" id=\"urlBox\" name=\"url\"><br><br>\n"

"        <label for=\"fileBox\">Select file:</label><br>\n"
"        <input type=\"file\" id=\"fileBox\" name=\"file\"><br><br>\n"

"        <input type=\"button\" value=\"Upload\" onclick=\"uploadFile()\">\n"
"    </form>\n"

"    <script>\n"
"        function uploadFile() {\n"
"            var url = document.getElementById('urlBox').value;\n"
"            var fileInput = document.getElementById('fileBox');\n"
"            var file = fileInput.files[0];\n"

"            fetch(url, {\n"
"                method: 'PUT',\n"
"                body: file,\n"
"                headers: {\n"
"                    'Content-Type': 'application/octet-stream'\n"
"                }\n"
"            })\n"
"            .then(response => response.text())\n"
"            .then(data => {\n"
"                console.log(data);\n"
"            })\n"
"            .catch(error => {\n"
"                console.error(error);\n"
"            });\n"
"        }\n"
"    </script>\n"
"</body>\n"
"</html>\n";

// global variable
volatile sig_atomic_t keep_running = 1;

bool isValidUTF8(unsigned char *s, size_t length) {
    size_t i = 0;
    while (i < length) {
        if ((s[i] & 0x80) == 0) {         // 0xxxxxxx
            i++;
        } else if ((s[i] & 0xE0) == 0xC0) { // 110xxxxx 10xxxxxx
            if (i + 1 >= length || (s[i + 1] & 0xC0) != 0x80) {
                return false;
            }
            i += 2;
        } else if ((s[i] & 0xF0) == 0xE0) { // 1110xxxx 10xxxxxx 10xxxxxx
            if (i + 2 >= length || (s[i + 1] & 0xC0) != 0x80 || (s[i + 2] & 0xC0) != 0x80) {
                return false;
            }
            i += 3;
        } else if ((s[i] & 0xF8) == 0xF0) { // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
            if (i + 3 >= length || (s[i + 1] & 0xC0) != 0x80 || (s[i + 2] & 0xC0) != 0x80 || (s[i + 3] & 0xC0) != 0x80) {
                return false;
            }
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

bool isUTF8File(FILE *fp) {
    if (fp == NULL) {
        return false;
    }

    // Save current position
    long originalPosition = ftell(fp);
    if (originalPosition == -1L) {
        perror("Error getting file position");
        return false;
    }
    
    // Check UTF-8
    fseek(fp, 0L, SEEK_SET);
    unsigned char buffer[1024];
    size_t bytesRead;
    bool isUTF8 = true;

    while ((bytesRead = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        if (!isValidUTF8(buffer, bytesRead)) {
            isUTF8 = false;
            break;
        }
    }

    // Restore file pointer
    fseek(fp, originalPosition, SEEK_SET);

    return isUTF8;
}

void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a') a -= 'a'-'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a'-'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';

            *dst++ = 16*a+b;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst++ = '\0';
}


void handle_get_request(int client_socket, const char *path) {
  char file_path[BUFSIZE];
  char header[MAX_HEADER_LENGTH];
  memset(header, 0, sizeof(header));
  memset(file_path, 0, sizeof(file_path));
  strcpy(file_path, path);

  char *request = strtok(NULL, " ");
  if (request != NULL) {
    // strcat(file_path, request);
    url_decode(file_path + strlen(file_path), request);
  } else {
    request = "/";
    url_decode(file_path + strlen(file_path), request);
    // the path is empty, ign
    //sprintf(header, "HTTP/1.1 400 Bad Request\r\n\r\n");
    //send(client_socket, header, strlen(header), 0);
    //printf("GET %s - 400 Bad Request\n", file_path);
    //return;
  }

  if (strcmp(request, "/upload") == 0) {
    sprintf(header, "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n");
    send(client_socket, header, strlen(header), 0);
    printf("GET %s - 200 OK\n", file_path);

    char buffer[BUFSIZE];
    sprintf(buffer, UPLOAD_HTML);
    send(client_socket, buffer, strlen(buffer), 0);
    return;
  }

  if (strcmp(request, "/stop") == 0) {
    sprintf(header, "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n");
    send(client_socket, header, strlen(header), 0);
    printf("GET %s - 200 OK\n", file_path);

    char buffer[BUFSIZE];
    sprintf(buffer, "<html><body><h1>Server Stopped</h1></body></html>");
    send(client_socket, buffer, strlen(buffer), 0);
    keep_running = 0;
    return;
  }

  DIR *dir = opendir(file_path);
  if (dir) {
    sprintf(header, "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n");
    send(client_socket, header, strlen(header), 0);
    printf("GET %s - 200 OK\n", file_path);

    char buffer[BUFSIZE];
    sprintf(buffer, "<html><body><h1>Directory Listing</h1>");
    sprintf(buffer + strlen(buffer), "%s", request);
    sprintf(buffer + strlen(buffer), "<hr><ul>");
    send(client_socket, buffer, strlen(buffer), 0);
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
      char entry_path[BUFSIZE];
      // sprintf(entry_path, "%s/%s", file_path, entry->d_name);
      sprintf(entry_path, "%s%s/", request, entry->d_name);
      if (entry->d_type == DT_DIR) {
        // sprintf(buffer, "<li><a href='%s/'>%s/</a></li>", entry->d_name, entry->d_name);
        sprintf(buffer, "<li><a href='%s/'>%s/</a></li>", entry->d_name, entry->d_name);
      } else {
        // sprintf(buffer, "<li><a href='%s'>%s</a></li>", entry->d_name, entry->d_name);
        sprintf(buffer, "<li><a href='%s'>%s</a></li>", entry->d_name, entry->d_name);
      }
      send(client_socket, buffer, strlen(buffer), 0);
    }
    sprintf(buffer, "</ul>");
    // add a separator
    sprintf(buffer + strlen(buffer), "<hr>");
    // add a link to the upload page
    sprintf(buffer + strlen(buffer), "<a href='/upload'>Upload File</a>");

    sprintf(buffer + strlen(buffer), "</body></html>");
    send(client_socket, buffer, strlen(buffer), 0);

    closedir(dir);
  } else {
    FILE *fp = fopen(file_path, "r");
    if (fp == NULL) {
      if (errno == EACCES) {
        send(client_socket, HTTP_403, strlen(HTTP_403), 0);
        printf("GET %s - 403 Forbidden\n", file_path);
      } else {
        send(client_socket, HTTP_404, strlen(HTTP_404), 0);
        printf("GET %s - 404 Not Found\n", file_path);
      }
      return;
    }

    bool isUTF8 = isUTF8File(fp);
    // Compute file size
    fseek(fp, 0L, SEEK_END);
    int file_size = ftell(fp);
    fseek(fp, 0L, SEEK_SET);
    // Compose and send header
    // char header[256];
    sprintf(header, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\n\r\n", 
            isUTF8 ? "text/plain; charset=UTF-8" : "application/octet-stream", file_size);
    send(client_socket, header, strlen(header), 0);
    printf("GET %s - 200 OK\n", file_path);

    char buffer[BUFSIZE];
    memset(buffer, 0, sizeof(buffer));
    int bytes_read = 0;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
      send(client_socket, buffer, bytes_read, 0);
      memset(buffer, 0, sizeof(buffer));
    }

    send(client_socket, "", 0, 0);

    fclose(fp);
  }
}

/*
void handle_put_request(int client_socket, const char *path) {
  char file_path[BUFSIZE];
  strcpy(file_path, path);

  char *request = strtok(NULL, " ");
  if (request != NULL) {
    // strcat(file_path, request);
    url_decode(file_path + strlen(file_path), request);
  }

  FILE *fp = fopen(file_path, "w");
  if (fp == NULL) {
    char header[MAX_HEADER_LENGTH];
    sprintf(header, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
    send(client_socket, header, strlen(header), 0);
    printf("PUT %s - 500 Internal Server Error\n", file_path);
    return;
  }

  char buffer[BUFSIZE];
  int bytes_read;
  while ((bytes_read = recv(client_socket, buffer, BUFSIZE, 0)) > 0) {
    fwrite(buffer, 1, bytes_read, fp);
  }

  fclose(fp);

  char header[MAX_HEADER_LENGTH];
  sprintf(header, "HTTP/1.1 200 OK\r\n\r\n");
  send(client_socket, header, strlen(header), 0);
  printf("PUT %s - 200 OK\n", file_path);
}
*/
long int get_content_length(const char *headers) {
    const char *header_start = headers;
    const char *header_end;

    while ((header_end = strstr(header_start, "\r\n")) != NULL) {
        if (strncmp(header_start, "Content-Length:", 15) == 0) {
            long int content_length = strtol(header_start + 15, NULL, 10);
            return content_length;
        }
        header_start = header_end + 2; // Skip the "\r\n"
    }

    return -1; // Content-Length not found
}

int get_header_length(const char *headers) {
    const char *header_start = headers;
    const char *header_end;

    while ((header_end = strstr(header_start, "\r\n")) != NULL) {
        if (header_start == header_end) {
            return header_end - headers + 2; // Skip the "\r\n"
        }
        header_start = header_end + 2; // Skip the "\r\n"
    }

    return -1; // Header not found
}

void handle_put_request(int client_socket, const char *path) {
    char buffer[BUFSIZE];
    char header[MAX_HEADER_LENGTH];
    int bytes_read = recv(client_socket, buffer, BUFSIZE, MSG_PEEK);
    long int content_length = get_content_length(buffer);
    int header_length = get_header_length(buffer);
    recv(client_socket, buffer, header_length, 0); // Read the headers (and discard them)

    if (bytes_read <= 0) {
        return;
    }

    buffer[bytes_read] = '\0';

    char *request = strtok(buffer, " ");
    request = strtok(NULL, " "); // This should now point to the URI

    char file_path[BUFSIZE];
    memset(file_path, 0, sizeof(file_path));
    strcpy(file_path, path);
    if (request != NULL) {
        url_decode(file_path + strlen(file_path), request);
    } else {
        // file path is missing
        send(client_socket, HTTP_400, strlen(HTTP_400), 0);
        printf("PUT %s - 400 Bad Request\n", file_path);
        return;
    }
    
    // check if file path ends with a slash
    if (file_path[strlen(file_path) - 1] == '/') {
        // mkdir
        mkdir(file_path, 0777);
        sprintf(header, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n");
        printf("PUT %s - 200 OK\n", file_path);
        send(client_socket, header, strlen(header), 0);
        return;
    }

    if (content_length < 0) {
      // bad request
      send(client_socket, HTTP_400, strlen(HTTP_400), 0);
      printf("PUT %s - 400 Bad Request\n", file_path);
      printf("Content-Length: %ld\n", content_length);
      return;
    }

    FILE *fp = fopen(file_path, "w");
    if (fp == NULL) {
      // Internal server error
      send(client_socket, HTTP_500, strlen(HTTP_500), 0);
      printf("PUT %s - 500 Internal Server Error\n", file_path);
      return;
    }

    long int total_bytes_written = 0;
    while (total_bytes_written < content_length) {
        bytes_read = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_read <= 0) {
            break;
        }
        fwrite(buffer, 1, bytes_read, fp);
        total_bytes_written += bytes_read;
    }

    fclose(fp);

    // Send response to client
    if (total_bytes_written == content_length) {
        sprintf(header, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n");
        printf("PUT %s - 200 OK\n", file_path);
    } else {
        sprintf(header, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
        printf("PUT %s - 500 Internal Server Error\n", file_path);
    }
    send(client_socket, header, strlen(header), 0);
}


void handle_request(int client_socket, const char *path) {
  char buffer[BUFSIZE];
  memset(buffer, 0, sizeof(buffer));
  int bytes_read = recv(client_socket, buffer, BUFSIZE, MSG_PEEK);
  if (bytes_read <= 0) {
    return;
  }

  char *request_type = strtok(buffer, " ");
  if (strcmp(request_type, "GET") == 0) {
    handle_get_request(client_socket, path);
  } else if (strcmp(request_type, "PUT") == 0) {
    handle_put_request(client_socket, path);
  } else {
    char header[MAX_HEADER_LENGTH];
    memset(header, 0, sizeof(header));
    sprintf(header, "HTTP/1.1 400 Bad Request\r\n\r\n");
    printf("%s - 400 Bad Request\n", request_type);
    send(client_socket, header, strlen(header), 0);
  }
}

// Signal handler
void sigint_handler(int sig_num) {
    keep_running = 0; // Set to false to stop the server loop
}

int main(int argc, char *argv[]) {
  const char *listen_address = DEFAULT_LISTEN_ADDRESS;
  int port = DEFAULT_PORT;
  char path[BUFSIZE] = ".";

  signal(SIGPIPE, SIG_IGN);
  signal(SIGINT, sigint_handler);
  signal(SIGTERM, sigint_handler);

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--listen") == 0 && i + 1 < argc) {
      listen_address = argv[++i];
    } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      port = atoi(argv[++i]);
    } else {
      strcpy(path, argv[i]);
    }
  }

  int listen_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_socket < 0) {
    perror("socket");
    return 1;
  }

  struct sockaddr_in server_address;
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(port);
  server_address.sin_addr.s_addr = inet_addr(listen_address);

  if (bind(listen_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
    perror("bind");
    return 1;
  }

  if (listen(listen_socket, 5) < 0) {
    perror("listen");
    return 1;
  }

  // load upload.html
  FILE *fp = fopen("upload.html", "r");
  if (fp != NULL) {
    // perror("fopen");
    fread(UPLOAD_HTML, 1, BUFSIZE, fp);
    fclose(fp);
  }

  printf("Upload HTML:\n%s\n", UPLOAD_HTML);

  printf("Listening on %s:%d\n", listen_address, port);

  while (1) {
    int client_socket = accept(listen_socket, NULL, NULL);
    if (client_socket < 0) {
      perror("accept");
      continue;
    }

    if (!keep_running) {
      // end server loop
      break;
    }

    handle_request(client_socket, path);

    shutdown(client_socket, SHUT_RDWR);

    close(client_socket);
  }

  printf("Stopping server...\n");
  close(listen_socket);
  return 0;
}
