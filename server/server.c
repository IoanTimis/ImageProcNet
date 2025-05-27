#define _POSIX_C_SOURCE 199309L // pentru clock_gettime
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>  // pentru timestamp



#define PORT 12345
#define BACKLOG 5
#define MAX_TASKS 100

// Structura task = un fisier primit de la un client
typedef struct {
    char filepath[256];
    int filesize;
    int client_fd;
    int operation; // 0 = resize, 1 = grayscale, 2 = blur
} Task;

typedef struct {
    char ip[64];
    int port;
    int socket_fd;
} ClientInfo;

typedef struct {
    char filename[256];
    int original_size;
    int processed_size;
    double duration_sec;
} HistoryEntry;

HistoryEntry history[100];
int history_count = 0;

pthread_mutex_t history_mutex = PTHREAD_MUTEX_INITIALIZER;

ClientInfo clients[100];
int client_count = 0;

pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;
// Listeaza IP-uri blocate
char blocked_ips[100][64];
int blocked_count = 0;
pthread_mutex_t block_mutex = PTHREAD_MUTEX_INITIALIZER;

// Verifica daca un IP este blocat
int is_blocked(const char* ip) {
    pthread_mutex_lock(&block_mutex);
    for (int i = 0; i < blocked_count; i++) {
        if (strcmp(blocked_ips[i], ip) == 0) {
            pthread_mutex_unlock(&block_mutex);
            return 1;
        }
    }
    pthread_mutex_unlock(&block_mutex);
    return 0;
}

Task queue[MAX_TASKS];
int front = 0, rear = 0, count = 0;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

// Adauga un task in coada
void enqueue(Task t) {
    pthread_mutex_lock(&mutex);
    while (count == MAX_TASKS) {
        pthread_cond_wait(&cond, &mutex);
    }
    queue[rear] = t;
    rear = (rear + 1) % MAX_TASKS;
    count++;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
}

// Extrage un task din coada
Task dequeue() {
    pthread_mutex_lock(&mutex);
    while (count == 0) {
        pthread_cond_wait(&cond, &mutex);
    }
    Task t = queue[front];
    front = (front + 1) % MAX_TASKS;
    count--;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
    return t;
}

// Worker thread care proceseaza imaginile
void* worker_thread(void* arg) {
    while (1) {
        Task t = dequeue();

        printf("[WORKER] Procesez %s (%d bytes)\n", t.filepath, t.filesize);

        char processed_path[512];
        snprintf(processed_path, sizeof(processed_path), "%s_processed.jpg", t.filepath);

        // Timp start
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        char cmd[1024];
        switch (t.operation) {
            case 0:
                snprintf(cmd, sizeof(cmd), "convert \"%s\" -resize 50%% \"%s\"", t.filepath, processed_path);
                break;
            case 1:
                snprintf(cmd, sizeof(cmd), "convert \"%s\" -colorspace Gray \"%s\"", t.filepath, processed_path);
                break;
            case 2:
                snprintf(cmd, sizeof(cmd), "convert \"%s\" -blur 0x8 \"%s\"", t.filepath, processed_path);
                break;
            default:
                fprintf(stderr, "[WORKER] Operatie necunoscuta: %d\n", t.operation);
                close(t.client_fd);
                continue;
        }
        int ret = system(cmd);
        if (ret != 0) {
            perror("[WORKER] Eroare la procesare imagine");
            close(t.client_fd);
            continue;
        }

        clock_gettime(CLOCK_MONOTONIC, &end);
        double duration = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

        printf("[WORKER] Procesare finalizata: %s\n", processed_path);

        int fd_in = open(processed_path, O_RDONLY);
        if (fd_in < 0) {
            perror("[WORKER] Nu pot deschide imaginea procesata");
            close(t.client_fd);
            continue;
        }

        off_t file_size = lseek(fd_in, 0, SEEK_END);
        lseek(fd_in, 0, SEEK_SET);

        int size = (int)file_size;
        if (send(t.client_fd, &size, sizeof(int), 0) <= 0) {
            perror("[WORKER] Eroare trimitere dimensiune");
            close(fd_in);
            close(t.client_fd);
            continue;
        }

        char buffer[4096];
        int bytes;
        while ((bytes = read(fd_in, buffer, sizeof(buffer))) > 0) {
            if (send(t.client_fd, buffer, bytes, 0) <= 0) {
                perror("[WORKER] Eroare trimitere date");
                break;
            }
        }

        close(fd_in);
        close(t.client_fd);

        printf("[WORKER] Am trimis imaginea procesata catre client.\n");

        // Salvare in istoric
        pthread_mutex_lock(&history_mutex);
        if (history_count < 100) {
            HistoryEntry h;
            strncpy(h.filename, t.filepath, sizeof(h.filename));
            h.original_size = t.filesize;
            h.processed_size = size;
            h.duration_sec = duration;
            history[history_count++] = h;
        }
        pthread_mutex_unlock(&history_mutex);
    }
    return NULL;
}



// Fir dedicat per client
void* client_handler(void* arg) {
    int client_fd = *(int*)arg;
    free(arg);

    char buffer[4096];
    int operation_type;
    // Primeste tipul de procesare
    if (recv(client_fd, &operation_type, sizeof(int), 0) <= 0) {
        perror("[SERVER] Eroare primire operatie");
        close(client_fd);
        return NULL;
    }
    int file_size;
    // Primeste dimensiune
    if (recv(client_fd, &file_size, sizeof(int), 0) <= 0) {
        perror("[SERVER] Eroare primire dimensiune");
        close(client_fd);
        return NULL;
    }

    // Salveaza in user_temp/<pid>.jpg
    char path[256];
    snprintf(path, sizeof(path), "../user_temp/%d.jpg", getpid());
    int fd_out = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (fd_out < 0) {
        perror("[SERVER] Eroare creare fisier output");
        close(client_fd);
        return NULL;
    }

    int total_received = 0;
    while (total_received < file_size) {
        int bytes = recv(client_fd, buffer, sizeof(buffer), 0);
        if (bytes <= 0) break;
        write(fd_out, buffer, bytes);
        total_received += bytes;
    }

    close(fd_out);
    // close(client_fd);
    // nu inchidem socketul, il pasam catre worker


    // Adauga in coada cu operatia primita anterior
    Task t;
    strncpy(t.filepath, path, sizeof(t.filepath));
    t.filesize = file_size;
    t.client_fd = client_fd;
    t.operation = operation_type;
    enqueue(t);


    printf("[SERVER] Task adaugat in coada: %s\n", path);
    return NULL;
}

void* admin_handler_thread(void* arg) {
    int admin_fd, client_fd;
    struct sockaddr_un addr;
    char buffer[256];

    unlink("admin.sock");

    admin_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (admin_fd < 0) {
        perror("[ADMIN] Eroare creare socket UNIX");
        return NULL;
    }

    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, "admin.sock");

    if (bind(admin_fd, (struct sockaddr*)&addr, sizeof(struct sockaddr_un)) < 0) {
        perror("[ADMIN] Eroare bind admin.sock");
        close(admin_fd);
        return NULL;
    }

    if (listen(admin_fd, 5) < 0) {
        perror("[ADMIN] Eroare listen admin.sock");
        close(admin_fd);
        return NULL;
    }

    printf("[ADMIN] Ascult pe socketul UNIX admin.sock...\n");

    while (1) {
        client_fd = accept(admin_fd, NULL, NULL);
        if (client_fd < 0) {
            perror("[ADMIN] Eroare accept");
            continue;
        }

        memset(buffer, 0, sizeof(buffer));
        read(client_fd, buffer, sizeof(buffer) - 1);

        printf("[ADMIN] Comanda primita: %s\n", buffer);

        if (strcmp(buffer, "LIST") == 0) {
            char msg[1024] = "Conexiuni active:\n";

            pthread_mutex_lock(&client_mutex);
            for (int i = 0; i < client_count; i++) {
                char line[128];
                snprintf(line, sizeof(line), "- %s:%d\n", clients[i].ip, clients[i].port);
                strcat(msg, line);
            }
            pthread_mutex_unlock(&client_mutex);

            write(client_fd, msg, strlen(msg));
        }
        else if (strcmp(buffer, "HISTORY") == 0) {
            char msg[2048] = "Istoric procesari:\n";

            pthread_mutex_lock(&history_mutex);
            for (int i = 0; i < history_count; i++) {
                char line[512];
                snprintf(line, sizeof(line),
                    "- %s | %d bytes -> %d bytes | %.2f sec\n",
                    history[i].filename,
                    history[i].original_size,
                    history[i].processed_size,
                    history[i].duration_sec);
                strcat(msg, line);
            }
            pthread_mutex_unlock(&history_mutex);

            write(client_fd, msg, strlen(msg));
        } else if (strcmp(buffer, "STATS") == 0) {
            double total = 0.0;
            int count = 0;

            pthread_mutex_lock(&history_mutex);
            for (int i = 0; i < history_count; i++) {
                total += history[i].duration_sec;
                count++;
            }
            pthread_mutex_unlock(&history_mutex);

            char msg[128];
            if (count == 0) {
                snprintf(msg, sizeof(msg), "Nicio procesare inregistrata.\n");
            } else {
                snprintf(msg, sizeof(msg), "Durata medie de procesare: %.3f sec\n", total / count);
            }

            write(client_fd, msg, strlen(msg));
        }
        else if (strncmp(buffer, "KICK ", 5) == 0) {
            char ip_to_kick[64];
            sscanf(buffer + 5, "%63s", ip_to_kick);
            int found = 0;
            pthread_mutex_lock(&client_mutex);
            for (int i = 0; i < client_count; i++) {
                if (strcmp(clients[i].ip, ip_to_kick) == 0) {
                    if (clients[i].socket_fd != client_fd) {
                        close(clients[i].socket_fd);
                    }
                    printf("[ADMIN] Client %s deconectat fortat.\n", clients[i].ip);
                    for (int j = i; j < client_count - 1; j++) clients[j] = clients[j+1];
                    client_count--;
                    found = 1;
                    break;
                }
            }
            pthread_mutex_unlock(&client_mutex);
            if (found) {
                char msg[128];
                snprintf(msg, sizeof(msg), "Clientul %s a fost deconectat.\n", ip_to_kick);
                write(client_fd, msg, strlen(msg));
            } else {
                char msg[128];
                snprintf(msg, sizeof(msg), "Clientul %s nu a fost gasit.\n", ip_to_kick);
                write(client_fd, msg, strlen(msg));
            }
        } else if (strncmp(buffer, "LIMIT ", 6) == 0) {
            char ip_to_block[64];
            sscanf(buffer + 6, "%63s", ip_to_block);
            pthread_mutex_lock(&block_mutex);
            int already = 0;
            for (int i = 0; i < blocked_count; i++) {
                if (strcmp(blocked_ips[i], ip_to_block) == 0) {
                    already = 1;
                    break;
                }
            }
            if (!already && blocked_count < 100) {
                strncpy(blocked_ips[blocked_count++], ip_to_block, sizeof(blocked_ips[0]));
                pthread_mutex_unlock(&block_mutex);
                char msg[128];
                snprintf(msg, sizeof(msg), "Conexiunile de la %s au fost blocate.\n", ip_to_block);
                write(client_fd, msg, strlen(msg));
            } else {
                pthread_mutex_unlock(&block_mutex);
                char msg[128];
                snprintf(msg, sizeof(msg), "IP-ul %s este deja blocat sau limita atinsa.\n", ip_to_block);
                write(client_fd, msg, strlen(msg));
            }
        } else if (strncmp(buffer, "UNBLOCK ", 8) == 0) {
            char ip_to_unblock[64];
            sscanf(buffer + 8, "%63s", ip_to_unblock);

            int found = 0;
            pthread_mutex_lock(&block_mutex);
            for (int i = 0; i < blocked_count; i++) {
                if (strcmp(blocked_ips[i], ip_to_unblock) == 0) {
                    // Scoate IP-ul din lista
                    for (int j = i; j < blocked_count - 1; j++) {
                        strcpy(blocked_ips[j], blocked_ips[j + 1]);
                    }
                    blocked_count--;
                    found = 1;
                    break;
                }
            }
            pthread_mutex_unlock(&block_mutex);

            char msg[128];
            if (found) {
                snprintf(msg, sizeof(msg), "IP-ul %s a fost deblocat.\n", ip_to_unblock);
            } else {
                snprintf(msg, sizeof(msg), "IP-ul %s nu se afla in lista de IP-uri blocate.\n", ip_to_unblock);
            }
            write(client_fd, msg, strlen(msg));
        } else if (strcmp(buffer, "QUIT") == 0) {
            char quit_msg[] = "Conexiune inchisa.\n";
            write(client_fd, quit_msg, strlen(quit_msg));
            close(client_fd);
            break; // Iesi din bucla de acceptare conexiuni
        } else {
            char msg[] = "Comanda necunoscuta.\n";
            write(client_fd, msg, strlen(msg));
        }

        close(client_fd);
    }

    close(admin_fd);
    return NULL;
}



int main() {
    int server_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size;

    // Creeaza socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Eroare socket");
        exit(1);
    }

    // Configureaza adresa
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Eroare bind");
        close(server_fd);
        exit(1);
    }

    if (listen(server_fd, BACKLOG) < 0) {
        perror("Eroare listen");
        close(server_fd);
        exit(1);
    }

    printf("[SERVER] Ascult pe portul %d...\n", PORT);

    // Porneste firul de procesare
    pthread_t worker;
    pthread_create(&worker, NULL, worker_thread, NULL);

    pthread_t admin;
    pthread_create(&admin, NULL, admin_handler_thread, NULL);

    // Accepta conexiuni multiple
    while (1) {
        addr_size = sizeof(client_addr);
        int* client_fd = malloc(sizeof(int));
        *client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_size);
        // Refuza conexiuni de la IP-uri blocate
        char* ip = inet_ntoa(client_addr.sin_addr);
        if (is_blocked(ip)) {
            printf("[SERVER] Conexiune blocata de la %s\n", ip);
            int err_code = -1;
            send(*client_fd, &err_code, sizeof(err_code), 0);
            close(*client_fd);
            free(client_fd);
            continue;
        }
         // Salveaza clientul in lista
         pthread_mutex_lock(&client_mutex);
        if (client_count < 100) {
            strcpy(clients[client_count].ip, inet_ntoa(client_addr.sin_addr));
            clients[client_count].port = ntohs(client_addr.sin_port);
            clients[client_count].socket_fd = *client_fd;
            client_count++;
        }
        pthread_mutex_unlock(&client_mutex);

        if (*client_fd < 0) {
            perror("Eroare accept");
            free(client_fd);
            continue;
        }

        printf("[SERVER] Client conectat: %s:%d\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port));

        pthread_t tid;
        pthread_create(&tid, NULL, client_handler, client_fd);
        pthread_detach(tid);
    }

    close(server_fd);
    return 0;
}
