#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <openssl/md5.h> 
#include <openssl/evp.h>
#include <stdbool.h>

#define MAX_CHUNK_SIZE 1024
#define NUM_DFS_SERVERS 4
#define TIMEOUT_SECONDS 1
#define MAX_PATH_LEN 512

typedef struct {
    char server_name[20];
    char ip_address[16];
    int port;
} ServerInfo;

ServerInfo dfs_servers[NUM_DFS_SERVERS];

void md5hash(const char *url, char *hash) {
    EVP_MD_CTX *mdctx;
    const EVP_MD *md;
    unsigned char md_value[EVP_MAX_MD_SIZE];
    unsigned int md_len;

    md = EVP_md5(); 
    mdctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(mdctx, md, NULL);
    EVP_DigestUpdate(mdctx, url, strlen(url));
    EVP_DigestFinal_ex(mdctx, md_value, &md_len);
    EVP_MD_CTX_free(mdctx);

    for (int i = 0; i < md_len; i++) {
        sprintf(&hash[i*2], "%02x", md_value[i]);
    }
}

void parse_config_file(const char *config_file) {
    FILE *fp = fopen(config_file, "r");
    if (fp == NULL) {
        perror("Error opening config file");
        exit(EXIT_FAILURE);
    }

    char line[100];
    int index = 0;
    while (fgets(line, sizeof(line), fp) != NULL && index < NUM_DFS_SERVERS) {
        sscanf(line, "server %s %15[^:]:%d", dfs_servers[index].server_name, dfs_servers[index].ip_address, &dfs_servers[index].port);
        printf("Server - %s - %s - %d\n", dfs_servers[index].server_name, dfs_servers[index].ip_address, dfs_servers[index].port);
        index++;
    }

    fclose(fp);
}

int connect_to_server(const char *ip_address, int port) {
    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1) {
        perror("Socket creation failed");
        close(client_socket);
        return -1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip_address, &server_addr.sin_addr);

    struct timeval timeout;
    timeout.tv_sec = TIMEOUT_SECONDS;
    timeout.tv_usec = 0;

    if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        perror("Set socket timeout failed");
        close(client_socket);
        return -1;
    }

    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Connection failed");
        close(client_socket);
        return -1;
    }

    return client_socket;
}

void send_command(int client_socket, const char *command_type, const char *filename, const char *server_name) {
    // Send get command to server
    char command[MAX_PATH_LEN];
    if (strcmp(command_type, "list") == 0) {
        snprintf(command, MAX_PATH_LEN, "%s", command_type);
    } else {
        snprintf(command, MAX_PATH_LEN, "%s %s", command_type, filename);
    }
    printf("Sending command to %s - %s\n", server_name, command);
    uint32_t command_len = strlen(command);
    uint32_t command_len_net = htonl(command_len); 
    send(client_socket, &command_len_net, sizeof(command_len_net), 0);
    send(client_socket, command, strlen(command), 0);
}

int send_data_in_chunks(int client_socket, const char *data, int data_size) {
    int total_bytes_sent = 0;
    int bytes_to_send;

    // Loop through the data and send in chunks of MAX_CHUNK_SIZE bytes
    for (int offset = 0; offset < data_size; offset += MAX_CHUNK_SIZE) {
        bytes_to_send = (data_size - offset < MAX_CHUNK_SIZE) ? (data_size - offset) : MAX_CHUNK_SIZE;

        // Send the chunk of data
        // printf("Sending data: %.1024s\n", data + offset);
        int bytes_sent = send(client_socket, data + offset, bytes_to_send, 0);

        if (bytes_sent == -1) {
            perror("send");
            return -1;
        }

        total_bytes_sent += bytes_sent;

        printf("Sent %d bytes: '", bytes_sent);
        // for (int i = 0; i < bytes_sent; i++) {
        //     printf("%c", data[offset + i]);
        // }
        printf("'\n");
    }

    return total_bytes_sent;
}

void calculate_chunks(int file_size, int num_chunks, int *chunk_sizes) {
    int base_size = file_size / num_chunks;  // Size of each base chunk
    int extra_bytes = file_size % num_chunks; // Remaining bytes to distribute

    for (int i = 0; i < num_chunks; i++) {
        if (i < extra_bytes) {
            // Add one extra byte to the chunk size for the first 'extra_bytes' chunks
            chunk_sizes[i] = base_size + 1;
        } else {
            // All subsequent chunks have the base size
            chunk_sizes[i] = base_size;
        }
    }
}

void put_file(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);

    int chunk_sizes[NUM_DFS_SERVERS];
    calculate_chunks(file_size, NUM_DFS_SERVERS, chunk_sizes);

    printf("File size - %ld\n", file_size);
    printf("Chunk sizes:\n");
    for (int i = 0; i < NUM_DFS_SERVERS; i++) {
        printf("Chunk %d: %d bytes\n", i + 1, chunk_sizes[i]);
    }
    
    int server_index = 0;
    int remaining_bytes = file_size;

    char **chunks = malloc(NUM_DFS_SERVERS * sizeof(char *));
    if (chunks == NULL) {
        perror("Error allocating memory for chunks");
        exit(EXIT_FAILURE);
    }
    int chunk_index = 0;

    char hash_result[MD5_DIGEST_LENGTH * 2 + 1];
    int x;
    md5hash(filename, hash_result);

    // Convert the first two bytes of the hash to an integer for modulus calculation
    sscanf(hash_result, "%2x", &x);
    
    while (remaining_bytes > 0) {
        int bytes_to_read = chunk_sizes[chunk_index];

        chunks[chunk_index] = malloc((bytes_to_read + 1) * sizeof(char));
        if (chunks[chunk_index] == NULL) {
            fprintf(stderr, "Memory allocation failed for chunk %d.\n", chunk_index);
            break;
        }

        // Dynamically allocate buffer (Needed for large file sizes to avoid stack overflow)
        char *buffer = malloc(bytes_to_read * sizeof(char));
        if (buffer == NULL) {
            fprintf(stderr, "Memory allocation failed for buffer.\n");
            break;
        }
        fread(buffer, 1, bytes_to_read, file);

        printf("Chunk data size - %ld \n", sizeof(buffer));
        // printf("Buffer - %s\n", buffer);
        memcpy(chunks[chunk_index], buffer, bytes_to_read);
        chunks[chunk_index][bytes_to_read] = '\0';
        free(buffer);

        remaining_bytes -= bytes_to_read;
        chunk_index++;
    }

    int chunk_pairs[NUM_DFS_SERVERS][4][2] = {
        {{1, 2}, {2, 3}, {3, 4}, {4, 1}},  
        {{4, 1}, {1, 2}, {2, 3}, {3, 4}},  
        {{3, 4}, {4, 1}, {1, 2}, {2, 3}},  
        {{2, 3}, {3, 4}, {4, 1}, {1, 2}}   
    };

    int hash_value = x % NUM_DFS_SERVERS;
    printf("Hash result = %d\n", hash_value);

    int (*pairs)[2] = chunk_pairs[hash_value];
    int chunk_sent[NUM_DFS_SERVERS] = {0}; // Track sent status for each chunk

    for (int i = 0; i < NUM_DFS_SERVERS; ++i) {
        int client_socket = connect_to_server(dfs_servers[i].ip_address, dfs_servers[i].port);
        if (client_socket == -1) {
            fprintf(stderr, "Error connecting to server - %s: %s\n", dfs_servers[i].server_name, strerror(errno));
            continue; // Skip to next server on connection failure
        }
        
        send_command(client_socket, "put", filename, dfs_servers[i].server_name);

        int dfs_server = i + 1; 
        for (int j = 0; j < 2; j++) {
            // Send Chunk number
            int chunk_number = pairs[i][j];
            int chunk_number_net = htonl(chunk_number);
            printf("Sending chunk number %d \n", ntohl(chunk_number_net));

            if (send(client_socket, &chunk_number_net, sizeof(chunk_number_net), 0) != sizeof(chunk_number_net)) {
                perror("Error sending chunk number");
                continue;
            }

            // Send Chunk size
            int chunk_size = chunk_sizes[chunk_number-1];
            int chunk_size_net = htonl(chunk_size);
            printf("Sending chunk size - %d\n", ntohl(chunk_size_net));
            if (send(client_socket, &chunk_size_net, sizeof(chunk_size_net), 0) != sizeof(chunk_size_net)) {
                perror("Error sending chunk size");
                continue;
            }

            // Send chunk data
            int result = send_data_in_chunks(client_socket, chunks[chunk_number-1], chunk_size);
            if (result != -1) {
                chunk_sent[chunk_number - 1] = 1; // Mark chunk as sent
            } 
        }
        // int dfs_server = i + 1;  // DFS Server number (1-based index)
        // int chunk_number_1 = pairs[i][0];  // First chunk in the pair
        // int chunk_number_2 = pairs[i][1];  // Second chunk in the pair
    
        // printf("DFS Server %d: Pair (%d, %d)\n", dfs_server, chunk_number_1, chunk_number_2);
        close(client_socket);
    }

    // Check if all required chunks are received
    int all_chunks_sent = 1;
    for (int i = 0; i < NUM_DFS_SERVERS; i++) {
        if (chunk_sent[i] == 0) {
            printf("Error: %s put failed, missing chunk %d\n", filename, i + 1);
            all_chunks_sent = 0;
            break;
        }
    }

    if (all_chunks_sent) {
        printf("File '%s' Uploaded successfully to DFS servers.\n", filename);
    } 

    fclose(file);

    for (int i = 0; i < NUM_DFS_SERVERS; i++) {
        free(chunks[i]);
    }
    free(chunks);
}

void get_file(const char *filename) {
    char path[MAX_PATH_LEN];
    const char *folder = "client_folder/";
    snprintf(path, MAX_PATH_LEN, "%s%s", folder, filename);
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        perror("Error creating file for download");
        exit(EXIT_FAILURE);
    }

    // Array to hold pointers to dynamically allocated chunk buffers
    char *chunks[NUM_DFS_SERVERS] = {NULL};
    int chunk_sizes[NUM_DFS_SERVERS] = {0};
    int chunk_received[NUM_DFS_SERVERS] = {0}; // Track received status for each chunk

    for (int i = 0; i < NUM_DFS_SERVERS; i++) {
        int client_socket = connect_to_server(dfs_servers[i].ip_address, dfs_servers[i].port);
        if (client_socket == -1) {
            fprintf(stderr, "Error connecting to server - %s: %s\n", dfs_servers[i].server_name, strerror(errno));
            continue; // Skip to next server on connection failure
        }

        // Send get command to server
        send_command(client_socket, "get", filename, dfs_servers[i].server_name);

        for (int j = 0; j < 2; j++) {
            // Receive chunk metadata from server
            int chunk_number, file_size;
            if (recv(client_socket, &chunk_number, sizeof(chunk_number), 0) != sizeof(chunk_number) ||
                recv(client_socket, &file_size, sizeof(file_size), 0) != sizeof(file_size)) {
                perror("Error receiving chunk metadata");
                close(client_socket);
                continue; // Skip to next chunk on receive error
            }

            chunk_number = ntohl(chunk_number);
            file_size = ntohl(file_size);
            printf("Received chunk number: %d, size: %d\n", chunk_number, file_size);

            chunks[chunk_number - 1] = (char *)malloc(file_size);
            if (chunks[chunk_number - 1] == NULL) {
                perror("Error allocating memory for chunk");
                close(client_socket);
                continue; // Skip to next chunk on memory allocation failure
            }

            ssize_t total_bytes_received = 0;
            while (total_bytes_received < file_size) {
                ssize_t bytes_to_receive = file_size - total_bytes_received;
                if (bytes_to_receive > MAX_CHUNK_SIZE) {
                    bytes_to_receive = MAX_CHUNK_SIZE;
                }

                ssize_t bytes_received = recv(client_socket, chunks[chunk_number - 1] + total_bytes_received,
                                              bytes_to_receive, 0);
                if (bytes_received <= 0) {
                    perror("Error receiving data");
                    close(client_socket);
                    free(chunks[chunk_number - 1]); 
                    break; 
                }
                total_bytes_received += bytes_received;
                if (total_bytes_received < file_size) {
                    chunks[chunk_number - 1][total_bytes_received] = '\0'; // Null terminate
                }
            }

            chunk_sizes[chunk_number - 1] = file_size;
            chunk_received[chunk_number - 1] = 1; // Mark chunk as received
        }

        close(client_socket);
    }

    // Check if all required chunks are received
    int all_chunks_received = 1;
    for (int i = 0; i < NUM_DFS_SERVERS; i++) {
        if (chunk_received[i] == 0) {
            printf("Error: %s is incomplete, missing chunk %d\n", filename, i + 1);
            all_chunks_received = 0;
            break;
        }
    }

    if (all_chunks_received) {
        // Write all chunks to file
        for (int i = 0; i < NUM_DFS_SERVERS; i++) {
            fwrite(chunks[i], sizeof(char), chunk_sizes[i], file);
            free(chunks[i]); 
        }

        fclose(file);
        printf("File '%s' downloaded successfully from DFS servers.\n", filename);
    } else {
        fclose(file);
        remove(path); // Delete incomplete file
    }
}

int compare_strings(const void* a, const void* b) {
    return strcmp(*(char**)a, *(char**)b);
}

char *get_base_filename(const char *filename) {
    char *base_filename = strdup(filename);

    char *last_underscore = strrchr(base_filename, '_');
    if (last_underscore != NULL) {
        *last_underscore = '\0';
    }

    return base_filename;
}

// Function to check if all four chunks are present for a given base filename
bool are_all_chunks_present(const char *base_filename, char **filenames, int num_filenames) {
    bool chunk_present[NUM_DFS_SERVERS] = {false}; 

    for (int i = 0; i < num_filenames; i++) {
        const char *filename = filenames[i];
        char *current_base_filename = get_base_filename(filename);

        if (strcmp(current_base_filename, base_filename) == 0) {
            // Extract the chunk number from the current filename
            const char *last_digit_str = filename + strlen(base_filename);
            if (*last_digit_str == '_') {
                int chunk_number = atoi(last_digit_str + 1); // Skip the underscore

                if (chunk_number >= 1 && chunk_number <= NUM_DFS_SERVERS) {
                    chunk_present[chunk_number - 1] = true;
                }
            }
        }

        free(current_base_filename); 
    }

    // Check if all required chunks are present
    for (int i = 0; i < NUM_DFS_SERVERS; i++) {
        if (!chunk_present[i]) {
            return false; 
        }
    }

    return true; 
}

void list_files() {
    char* file_list[1000];
    int file_count = 0;
    char buffer[MAX_CHUNK_SIZE];

    for (int i = 0; i < NUM_DFS_SERVERS; i++) {
        int client_socket = connect_to_server(dfs_servers[i].ip_address, dfs_servers[i].port);

        // Send list command to server
        send_command(client_socket, "list", '\0', dfs_servers[i].server_name);

        // Receive file size from server
        int file_name_length;
        ssize_t bytes_to_receive;
        while ((bytes_to_receive = recv(client_socket, &file_name_length, sizeof(file_name_length), 0)) > 0) {
            file_name_length = ntohl(file_name_length);
            // printf("Received file name length: %d\n", file_name_length);
            memset(buffer, 0, sizeof(buffer));
            ssize_t bytes_received = recv(client_socket, buffer, file_name_length, 0);
            buffer[bytes_received] = '\0';
            if (strcmp(buffer, "EOC") == 0) {
                break;
            }
            file_list[file_count] = strdup(buffer);
            file_count++;
            // printf("%s\n", buffer);
        }
    }
    qsort(file_list, file_count, sizeof(char *), compare_strings);
    // printf("Received Filenames:\n");
    // for (int i = 0; i < file_count; i++) {
    //     printf("%s\n", file_list[i]);
    // }

    bool *processed = (bool *)malloc(file_count * sizeof(bool));
    if (processed == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
    }

    for (int i = 0; i < file_count; i++) {
        processed[i] = false;
    }

    for (int i = 0; i < file_count; i++) {
        const char *filename = file_list[i];
        char *base_filename = get_base_filename(filename);

        if (!processed[i]) {
            bool all_chunks_present = are_all_chunks_present(base_filename, file_list, file_count);

            if (all_chunks_present) {
                printf("%s [complete]\n", base_filename);
            } else {
                printf("%s [incomplete]\n", base_filename);
            }

            for (int j = i; j < file_count; j++) {
                if (strcmp(base_filename, get_base_filename(file_list[j])) == 0) {
                    processed[j] = true;
                }
            }
        }
        free(base_filename); 
    }
    free(processed);
}

int main(int argc, char *argv[]) {
    const char *config_file = "dfc.conf"; 
    parse_config_file(config_file);

    char *command = argv[1];

    if (strcmp(command, "put") == 0 && argc == 3) {
        for (int i = 2; i < argc; i++) {
            const char *filename = argv[i];
            printf("Put file - %s\n", filename);
            put_file(filename);
        }
    } else if (strcmp(command, "get") == 0 && argc == 3) {
        const char *filename = argv[2];
        get_file(filename);
    } else if (strcmp(command, "list") == 0) {
        list_files();
    } else {
        fprintf(stderr, "Invalid command or arguments\n");
        exit(EXIT_FAILURE);
    }

    return 0;
}
