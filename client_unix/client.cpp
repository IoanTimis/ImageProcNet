#include <iostream>
#include <fstream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 12345

int main() {
    int sock_fd;
    struct sockaddr_in server_addr;

    // Deschide fisierul test.jpg
    std::ifstream file("test.jpg", std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[CLIENT] Nu am putut deschide test.jpg\n";
        return 1;
    }

    // Obtine dimensiunea fisierului
    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    // Citeste continutul fisierului
    char* file_data = new char[file_size];
    if (!file.read(file_data, file_size)) {
        std::cerr << "[CLIENT] Eroare la citirea fisierului\n";
        delete[] file_data;
        return 1;
    }

    // Creeaza socket
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("Eroare socket");
        delete[] file_data;
        return 1;
    }

    // Seteaza adresa serverului
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Conecteaza la server
    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Eroare conectare");
        close(sock_fd);
        delete[] file_data;
        return 1;
    }

    // Trimite tipul de procesare
    int operation;
    std::cout << "Alege procesarea imaginii:\n1. Resize (50%)\n2. Grayscale\n3. Blur\nAlegere: ";
    std::cin >> operation;
    operation -= 1; // pentru a fi 0-indexed: 0=resize, 1=gray, 2=blur
    send(sock_fd, &operation, sizeof(int), 0);

    // Trimite dimensiunea fisierului
    int size_to_send = static_cast<int>(file_size);
    send(sock_fd, &size_to_send, sizeof(int), 0);

    // Trimite continutul fisierului
    send(sock_fd, file_data, file_size, 0);
    std::cout << "[CLIENT] Am trimis test.jpg (" << file_size << " bytes)\n";

    delete[] file_data;

    // Primeste dimensiunea imaginii procesate
    int processed_size;
    if (recv(sock_fd, &processed_size, sizeof(int), 0) <= 0) {
        std::cerr << "[CLIENT] Eroare la primirea dimensiunii imaginii procesate\n";
        close(sock_fd);
        return 1;
    }
    // Verifica cod de blocare
    if (processed_size < 0) {
        std::cerr << "[CLIENT] Conexiune blocata de server\n";
        close(sock_fd);
        return 1;
    }
    std::ofstream out("received_processed.jpg", std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "[CLIENT] Nu pot crea fisierul de output\n";
        close(sock_fd);
        return 1;
    }

    char buffer[4096];
    int total = 0;
    while (total < processed_size) {
        int bytes = recv(sock_fd, buffer, sizeof(buffer), 0);
        if (bytes <= 0) break;
        out.write(buffer, bytes);
        total += bytes;
    }

    std::cout << "[CLIENT] Am primit imaginea procesata (" << processed_size << " bytes)\n";

    out.close();
    close(sock_fd);
    return 0;
}
