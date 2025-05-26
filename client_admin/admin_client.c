#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SOCK_PATH "../server/admin.sock"

int main() {
    int sock_fd;
    struct sockaddr_un addr;
    char buffer[256];

    // Creeaza socket UNIX
    sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("[ADMIN CLIENT] Eroare socket");
        exit(1);
    }

    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCK_PATH);

    // Conecteaza la socket
    if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(struct sockaddr_un)) < 0) {
        perror("[ADMIN CLIENT] Eroare conectare la admin.sock");
        close(sock_fd);
        exit(1);
    }

    // Trimite comanda
    printf("Comanda admin: ");
    fgets(buffer, sizeof(buffer), stdin);
    buffer[strcspn(buffer, "\n")] = 0; // elimina \n

    write(sock_fd, buffer, strlen(buffer));

    // Primeste raspunsul
    int bytes = read(sock_fd, buffer, sizeof(buffer) - 1);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        printf("[ADMIN CLIENT] Raspuns: %s\n", buffer);
    } else {
        printf("[ADMIN CLIENT] Nu am primit raspuns.\n");
    }

    close(sock_fd);
    return 0;
}
