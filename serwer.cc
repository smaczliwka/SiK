#include "err.h"
#include "parser.h"
#include "messenger.h"

#include <fstream>

void end_connection(int msg_sock) {
    printf("ending connection\n");
    if (close(msg_sock) < 0) {
        fprintf(stderr, "closing socket failed\n");
    }
}

std::pair <std::string, std::pair <std::string, std::string>> process_line(const std::string& line) {
    size_t begin, end;

    begin = 0;
    end = line.find("\t", begin);
    std::string resource = line.substr(begin, end - begin);

    begin = end + 1;
    end = line.find("\t", begin);
    std::string server = line.substr(begin, end - begin);

    std::string port = line.substr(end + 1);
    
    return make_pair(resource, make_pair(server, port));
}

int port_num(const char* chr) {
    std::string str(chr);
    size_t pos;
    int num = -1;
    try {
        num = std::stoi(str, &pos);
        if (pos != str.length()) {
            fatal("port number must be a 16-bit number");
        }
    } catch (const std::exception& e) { //stoi może rzucić wyjątek
        fatal("port number must be a 16-bit number");
    }
    if (num >= 1<<16 || num < 0) {
        fatal("port number out of range");
    }
    return num;
}


int main(int argc, char *argv[]) {

    if (argc != 3 && argc != 4) {
        fatal("invalid number of arguments");
    }
    
    const int PORT_NUM = (argc == 4) ? port_num(argv[3]) : 8080;

    std::map <std::string, std::pair <std::string, std::string>> correlated; 
    std::string line;
    std::ifstream myfile(argv[2]);
    if (myfile.is_open()) {
        try {
            while (getline (myfile, line)) {
                std::pair <std::string, std::pair<std::string, std::string>> p = process_line(line);
                if (correlated.find(p.first) == correlated.end()) {
                    correlated.insert(p);
                }
            }
        } catch(...) {
            myfile.close();
            fatal("loading file with correlated servers to memory");
        }
        myfile.close();
    }
    else {
        fatal("unable to open file with correlated servers");
    }

    DIR *dp;
    const char* CATALOG = canonicalize_file_name(argv[1]); // ścieżka do folderu w postaci znormalizowanej
    if (CATALOG == NULL) {
        fatal("wrong catalog path");
    }   
    if ((dp = opendir(CATALOG)) == NULL) {
        fatal("unable to open catalog");
    }
    
    int sock, msg_sock;
    struct sockaddr_in server_address;
    struct sockaddr_in client_address;
    socklen_t client_address_len;
    
    ssize_t len;

    sock = socket(PF_INET, SOCK_STREAM, 0); // creating IPv4 TCP socket
    if (sock < 0)
        syserr("socket");
    
    // after socket() call; we should close(sock) on any execution path;
    // since all execution paths exit immediately, sock would be closed when program terminates

    server_address.sin_family = AF_INET; // IPv4
    server_address.sin_addr.s_addr = htonl(INADDR_ANY); // listening on all interfaces
    server_address.sin_port = htons(PORT_NUM); // listening on port PORT_NUM

    // bind the socket to a concrete address
    if (bind(sock, (struct sockaddr *) &server_address, sizeof(server_address)) < 0)
        syserr("bind");

    // switch to listening (passive open)
    if (listen(sock, QUEUE_LENGTH) < 0)
        syserr("listen");

    //const unsigned int MAX_BUF_LENGTH = 4096;
    std::vector<char> buffer(MAX_BUF_LENGTH);
    std::string rcv;
        
    printf("accepting client connections on port %hu\n", ntohs(server_address.sin_port));

    for (;;) { // pętla dla kolejnych klientów
        printf("waiting for next client\n");

        // łączymy się z klientem
        client_address_len = sizeof(client_address);
        msg_sock = accept(sock, (struct sockaddr *) &client_address, &client_address_len);
        if (msg_sock < 0)
            syserr("accept");
        
        rcv.clear(); // nowy klient, czyszczę bufor

        while (true) { //pętla dla tego jednego klienta

            len = read(msg_sock, &buffer[0], buffer.size());
            if (len < 0) { // 0 lub liczba ujemna zwrócona przez read, zamykamy połączenie

                fprintf(stderr, "reading from client socket failed\n");
                send_error_message("500 Internal Server Error", msg_sock, true);
                end_connection(msg_sock);
                break;
            }

            if (len == 0) { // len zwróci 0 kiedy klient zamknie połączenie
                end_connection(msg_sock);
                break;
            }
            
            // być może się skończyła pamięć
            try {
               rcv.append(buffer.begin(), buffer.begin() + len); 
            } catch(const std::exception& e) {
                try_write(msg_sock, "500 Internal Server Error", strlen("500 Internal Server Error"));
                end_connection(msg_sock);
                break;
            }
            

            std::vector<http_message_t> http_messages;

            try {
                http_messages = get_messages(rcv);
            } catch(const char* error_message) {
                send_error_message(error_message, msg_sock, true);
                end_connection(msg_sock);
                break;
            }
            if (http_messages.empty()) {
                continue;
            }

            bool close = false; // czy połączenie zostało zamknięte przez klienta lub czy klient wysłał nieprawidłową wiadomość

            for (size_t i = 0; i < http_messages.size(); i++) { // przy pierwszym rzuceniu 400 nie pójdzie dalej
                try {
                   close = process_message(http_messages[i], msg_sock, CATALOG, correlated);
                } catch (const char* error_message) {
                    close = true;
                    send_error_message(error_message, msg_sock, true);
                }
                if (close == true) break;
            } 
            if (close == true) {
                end_connection(msg_sock);
                break;
            }
        }
    }
    
    closedir(dp);
    return 0;
}