#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <ctype.h>

#define MAX_CHUNK_SIZE 1024
#define MAX_PATH_LEN 512

void receive_file_chunk(int port, int client_socket, const char *filename) {
    char path[MAX_PATH_LEN];
    const char *folder = "server_folder";

    char buffer[MAX_CHUNK_SIZE];
    ssize_t bytes_received;

    for (int i = 0; i < 2; i++) {
        int chunk1_net;
        if (recv(client_socket, &chunk1_net, sizeof(chunk1_net), 0) != sizeof(chunk1_net)) {
            perror("Error receiving chunk");
            return;
        }

        int chunk1 = ntohl(chunk1_net);

        printf("Received chunk: %d\n", chunk1);

        char base[MAX_PATH_LEN];
        char ext[MAX_PATH_LEN] = "";  

        // Check if filename contains a dot
        if (strchr(filename, '.') != NULL) {
            // Split filename into base and extension
            sscanf(filename, "%[^.].%s", base, ext);
            snprintf(path, MAX_PATH_LEN, "%s_%d/%s_%d.%s", folder, port, base, chunk1, ext);
        } else {
            // Filename has no extension, so copy the whole filename to base
            strncpy(base, filename, MAX_PATH_LEN);
            base[MAX_PATH_LEN - 1] = '\0';  
            snprintf(path, MAX_PATH_LEN, "%s_%d/%s_%d", folder, port, base, chunk1);
        }

        printf("Store in filename - %s\n", path);
        FILE *file = fopen(path, "wb");  
        if (file == NULL) {
            perror("Error opening file");
            return;
        }

        int chunk_size_net;
        if (recv(client_socket, &chunk_size_net, sizeof(chunk_size_net), 0) != sizeof(chunk_size_net)) {
            perror("Error receiving chunk");
            return;
        }

        int chunk_size = ntohl(chunk_size_net);
        printf("Received chunk size: %d\n", chunk_size);

        memset(buffer, 0, sizeof(buffer));
        int total_bytes_received = 0;
        
        while (total_bytes_received < chunk_size) {
            int total_bytes_to_receive = 0;
            if ((chunk_size - total_bytes_received) < MAX_CHUNK_SIZE) {
                total_bytes_to_receive = chunk_size - total_bytes_received;
            } else {
                total_bytes_to_receive = MAX_CHUNK_SIZE;
            }
            printf("Total bytes to receive - %d\n", total_bytes_to_receive);
            bytes_received = recv(client_socket, buffer, total_bytes_to_receive, 0);
            if (bytes_received <= 0) {
                perror("Error receiving data");
                return;
            }
            buffer[bytes_received] = '\0';
            printf("Size of buffer - %ld\n", bytes_received);
            // printf("writing - %s\n", buffer);
            fwrite(buffer, 1, bytes_received, file);
            memset(buffer, 0, sizeof(buffer));

            total_bytes_received += bytes_received;
        }
        printf("Done reading chunk %d\n", chunk1);

        if (bytes_received == -1) {
            perror("Error receiving file chunk");
        }
        fclose(file);
    }
}

int extract_chunk_number(const char *filename) {
    const char *ptr = filename;
    const char *last_digit_ptr = NULL;

    while (*ptr != '\0') {
        if (isdigit(*ptr)) {
            if (last_digit_ptr == NULL || !isdigit(*(ptr + 1))) {
                last_digit_ptr = ptr;
            }
        }
        ptr++;
    }

    if (last_digit_ptr != NULL) {
        // Extract the last numeric sequence from the filename
        return atoi(last_digit_ptr);
    } else {
        return -1; 
    }
}

void send_file_chunk(int port, int client_socket, const char *filename) {
    char path[MAX_PATH_LEN];
    snprintf(path, MAX_PATH_LEN, "server_folder_%d", port);
    DIR *dir = opendir(path);
    printf("Path - %s\n", path);
    if (dir == NULL) {
        perror("Error opening directory");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) { // Check if it is a regular file
            char *name = entry->d_name;
            char prefix[MAX_PATH_LEN];
            if (strncmp(name, filename, strlen(filename)) == 0) {
                printf("File - %s\n", name);
                char filepath[MAX_PATH_LEN];
                snprintf(filepath, MAX_PATH_LEN, "%s/%s", path, name);
                FILE *file = fopen(filepath, "rb");
                if (file == NULL) {
                    perror("Error opening file");
                    continue;
                }

                int chunk_number = extract_chunk_number(name);
                printf("Chunk number - %d\n", chunk_number);
                // Send chunk number to client
                int chunk_number_net = htonl(chunk_number);
                if (send(client_socket, &chunk_number_net, sizeof(chunk_number_net), 0) == -1) {
                    perror("Error sending chunk number");
                    break;
                }
                fseek(file, 0, SEEK_END);
                long file_size = ftell(file);
                rewind(file);
                // Send file size to client
                int file_size_net = htonl(file_size);
                if (send(client_socket, &file_size_net, sizeof(file_size_net), 0) == -1) {
                    perror("Error sending file size");
                    break;
                }

                char buffer[MAX_CHUNK_SIZE];
                int remaining_bytes = file_size;

                memset(buffer, 0, sizeof(buffer));
                while (remaining_bytes > 0) {
                    int bytes_to_send = (remaining_bytes < MAX_CHUNK_SIZE) ? remaining_bytes : MAX_CHUNK_SIZE;
                    printf("Sent - %d bytes\n", bytes_to_send);
                    int bytes_read = fread(buffer, 1, bytes_to_send, file);
                    buffer[bytes_read] = '\0';
                    printf("Sent buffer - %s\n", buffer);
                    if (send(client_socket, buffer, bytes_read, 0) == -1) {
                        perror("Error sending file chunk");
                        break;
                    }
                    remaining_bytes -= bytes_to_send;
                    memset(buffer, 0, sizeof(buffer));
                }

                fclose(file);
            }
        }
    }

    closedir(dir);
}

void list_all_files(int port, int client_socket) {
    char path[MAX_PATH_LEN];
    snprintf(path, MAX_PATH_LEN, "server_folder_%d", port);
    DIR *dir = opendir(path);
    printf("Path - %s\n", path);
    if (dir == NULL) {
        perror("Error opening directory");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) { 
            int name_len = strlen(entry->d_name);
            printf("Sending length - %d\n", name_len);
            int name_len_net = htonl(name_len); 
            send(client_socket, &name_len_net, sizeof(name_len_net), 0);
            printf("Sending - %s\n", entry->d_name);
            send(client_socket, entry->d_name, strlen(entry->d_name), 0);
        }
    }
    int eoc_len = strlen("EOC");
    eoc_len = htonl(eoc_len);
    send(client_socket, &eoc_len, sizeof(eoc_len), 0);
    send(client_socket, "EOC", strlen("EOC"), 0); // End of file list
    closedir(dir);
    close(client_socket);
}

void handle_client(int client_socket, int port) {
    ssize_t bytes_received;

    uint32_t command_len_net;
    recv(client_socket, &command_len_net, sizeof(command_len_net), 0);
    uint32_t command_len = ntohl(command_len_net);  

    char command[command_len + 1];

    if ((bytes_received = recv(client_socket, command, command_len, 0)) > 0) {
        command[command_len] = '\0';
        printf("Command - %s\n", command);

        if (strstr(command, "list") == command) {
            list_all_files(port, client_socket);
        } else if (strstr(command, "get ") == command) {
            // Extract filename from command (format: "get filename")
            const char *filename = command + 4;

            send_file_chunk(port, client_socket, filename);
        } else if (strstr(command, "put ") == command) {
            // Extract filename from command (format: "put filename")
            const char *filename = command + 4;

            receive_file_chunk(port, client_socket, filename);
            printf("Received file chunk for '%s'\n", filename);
        } else {
            printf("Unknown command received - %s\n", command);
        }
    }

    if (bytes_received == 0) {
        printf("Client disconnected\n");
    } else {
        perror("Receive failed");
    }

    // close(client_socket);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: ./dfs <server_name> <port>\n");
        exit(EXIT_FAILURE);
    }

    const char *server_name = argv[1];
    int port = atoi(argv[2]);

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, 5) == -1) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("DFS Server %s listening on port %d...\n", server_name, port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_socket == -1) {
            perror("Accept failed");
            continue;
        }

        // Handle client requests in a new process
        pid_t pid = fork();
        if (pid == 0) {
            // Child process
            close(server_socket);
            printf("Handle client\n");
            handle_client(client_socket, port);
            exit(EXIT_SUCCESS);
        } else if (pid > 0) {
            // Parent process
            close(client_socket);
        } else {
            perror("Fork failed");
        }
    }

    close(server_socket);
    return 0;
}