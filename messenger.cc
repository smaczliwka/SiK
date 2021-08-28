#include "messenger.h"
#include <signal.h> //do ignorowania digpipe podczas pisania

bool valid_request_target_sign(char c) {
    if (isalnum(c) > 0) return true;
    if (c == '.' || c == '-' || c == '/') return true;
    return false;
}

bool valid_request_target(const std::string& s) {
    if (s.length() == 0) return false;
    for (size_t i = 0; i < s.length(); i++) {
        if (valid_request_target_sign(s[i]) == false) {
            return false;
        }
    }
    return true;
}

// zwraca true jeśli udało się władować cały bufor do socketa
// false w przeciwnym przypadku
bool try_write(int msg_sock, const char* buff, size_t buff_len) {
    signal(SIGPIPE, SIG_IGN); // to może lepiej za pomocą handlera

    size_t len = 0; // tyle już wpisało
    ssize_t snd_len;

    while (len < buff_len) {
        snd_len = write(msg_sock, buff + len, buff_len - len);
        if (snd_len < 0) {
            fprintf(stderr, "writing to client socket failed\n");
            signal(SIGPIPE, SIG_DFL); // przywracam normalną obsługę sigpipe
            return false; // coś się popsuło
        }
        len += snd_len;
    }
    signal(SIGPIPE, SIG_DFL); // przywracam normalną obsługę sigpipe
    return true;
}

// wysyła wiadomość o błędzie
// error postaci code SP reason-phrase
// zwraca informację, czy udało się poprawnie wysłać wiadomość
bool send_error_message(const std::string& error, int msg_sock, bool close) {
    printf("sending status %s\n", error.substr(0, 3).c_str());
    std::string message = "HTTP/1.1 " + error + "\r\n";

    if (close == true) {
        message += "connection: close\r\n";
    }

    message += "\r\n"; // poprawna wiadomość, ale bez ciała i headerów
    const char* cmessage = message.c_str();
    return try_write(msg_sock, cmessage, strlen(cmessage));
}

bool send_error_message(const char* error, int msg_sock, bool close) {
    return send_error_message(std::string(error), msg_sock, close);
}

// wysyła wiadomość ze statusem 302
// zwraca informację, czy udało się poprawnie wysłać wiadomość
bool send_connection_message(std::map <std::string, std::pair <std::string, std::string>>::const_iterator it, int msg_sock, bool close) {
    printf("sending status 302\n");
    std::string message = "HTTP/1.1 302 Found\r\n";
    message += "location: http://" + (it->second).first + ":" + (it->second).second + it->first + "\r\n";
    if (close == true) {
        message += "connection: close\r\n";
    }
    message += "\r\n";

    const char* cmessage = message.c_str();
    return try_write(msg_sock, cmessage, strlen(cmessage));
}

// wysyła wiadomość ze statusem 200
// zwraca informację, czy udało się poprawnie wysłać wiadomość
bool send_good_message(int msg_sock, long len, bool close) {
    printf("sending status 200\n");
    std::string message = "HTTP/1.1 200 OK\r\n"; // początek wiadomości, potem będzie ciało
    message += "content-length: " + std::to_string(len) + "\r\n";

    if (close == true) message += "connection: close\r\n";

    message += "content-type: application/octet-stream\r\n\r\n";

    const char* cmessage = message.c_str();
    return try_write(msg_sock, cmessage, strlen(cmessage));
}

// zwraca informację, czy udało się wysłać wiadomość poprawnie
bool find_in_correlated(const std::map <std::string, std::pair <std::string, std::string>>& correlated, const std::string& request_target, int msg_sock, bool close) {
    std::map <std::string, std::pair <std::string, std::string>>::const_iterator it = correlated.find(request_target);
    if (it == correlated.end()) {
        //printf("file not found in correlated servers\n");
        return send_error_message("404 Not Found", msg_sock, close);   
    }
    else {
        //printf("file found in correlated server\n");
        return send_connection_message(it, msg_sock, close);
    } 
}

char str[MAX_BUF_LENGTH]; // bufor do wysyłania zawartości pliku

// może rzucać error 400
// zwraca true jeśli w poprawnej wiadomości przesłano connection close
// false jeśli nie
bool process_message(const http_message_t& message, int msg_sock, const char* CATALOG, const std::map <std::string, std::pair <std::string, std::string>>& correlated) {
    
    if (message.start_line.method != "GET" && message.start_line.method != "HEAD") { // ignoruję prawidłowe wiadomości z metodami innymi niż get i head
        if (send_error_message("501 Not Implemented", msg_sock, false) == true) { // jeśli udało się prawidłowo wysłać komunikat
            return false; // nawet jeśli było conection close, to nie biorę go pod uwagę
        }
        else {
            return true; // jeśli nie udało się wysłać komunikatu, to zamykam połączenie
        }
    }
    
    std::map<std::string, std::string>::const_iterator it;

    // UWAGA: czy jezeli klient wyslal nam connection close to w odpowiedzi tez musimy miec connection close?
    bool close = false;
    it = message.header_fields.find("connection");
    if (it != message.header_fields.end() && it->second == "close") {
        printf("client sent connection:close\n");
        close = true;
    }

    if (valid_request_target(message.start_line.request_target) == false) { // target zawiera niedozwolone znaki - wysyłamy 404, i tak nie znajdziemy
        //printf("illegal sign in request target\n");
        if (send_error_message("404 Not Found", msg_sock, close) == false) {
            close = false;
        }
        return close;
    }

    std::string file_path_string(CATALOG);
    file_path_string.append(message.start_line.request_target); // ścieżka do folderu w postaci znormalizowanej z doklejoną ścieżką względną do pliku w postaci nieznormalizowanej
    //std::cout<<"file_path = "<<file_path_string<<std::endl;

    char* normalized_file_path = canonicalize_file_name(file_path_string.c_str()); // ścieżka do pliku w postaci znormalizowanej
    if (normalized_file_path == NULL) {
        //printf("file doesn't exist\n");
        if (find_in_correlated(correlated, message.start_line.request_target, msg_sock, close) == false) {
            close = false;
        }    
        return close;
    }
    
    // Serwer nie powinien pozwalać na sięganie do zasobów spoza wskazanego katalogu.
    // W przypadku, gdy żądanie sięga poza wskazany katalog, serwer powinien odpowiedzieć kodem 404.
    std::string normalized_file_path_string(normalized_file_path);
    if (normalized_file_path_string.find(CATALOG) != 0) {
        //printf("acces denied\n");
        if (find_in_correlated(correlated, message.start_line.request_target, msg_sock, close) == false) {
            close = true;
        }   
        return close;
    }

    
    // sprawdzam czy przypadkiem nie chcę czytać folderu - nie wiem czy muszę się tym przejmować
    DIR* dp;
    if ((dp = opendir(normalized_file_path)) != NULL) {
        //printf("folder, not file\n");
        closedir(dp);
        if (find_in_correlated(correlated, message.start_line.request_target, msg_sock, close) == false) {
            close = false;
        }   
        return close;
    }
    
    FILE* content = fopen(normalized_file_path, "r");
    if (content == NULL) {
        //printf("file opening failed\n");
        if (find_in_correlated(correlated, message.start_line.request_target, msg_sock, close) == false) {
            close = true; // jeśli nie udało się wysłać wiadomości, to zamykam połączenie
        }   
        return close;
    }
    fseek(content, 0, SEEK_END);
    long fsize = ftell(content);

    fseek(content, 0, SEEK_SET);

    if (send_good_message(msg_sock, fsize, close) == false) { // nie da się pisać, więc musimy zamknąć to połączenie
        fclose(content);
        return false;       
    }
    printf("sending file to client\n");
    if (message.start_line.method == "GET") {
        ssize_t len;

        do {
            memset(str, 0, sizeof(str));
            len = fread(str, 1, sizeof(str) - 1, content); //zostawiam 1 bajt na 0 na końcu, żeby ładnie wypisywać
            //printf("%s", str);

            if (try_write(msg_sock, str, len) == false) {
                close = true; // nie da się pisać, więc musimy zamknąć to połączenie <-------------------------------------------- UWAGA: TUTAJ WCZEŚNIEJ BYŁO FALSE
                break;
            }

        } while (len > 0);
    }
    printf("%ld bytes sent \n", fsize);

    fclose(content);
    return close;
}