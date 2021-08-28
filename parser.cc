#include "parser.h"
#include <algorithm>

// usuwa ciąg spacji na początku i na końcu
void trim(std::string& s) {
    size_t begin = 0, end = s.length();
    while (begin < s.length() && s[begin] == ' ') 
        begin++; //begin zatrzyma się na pierwszej nie-spacji lub s.length()
    while (end > 0 && s[end - 1] == ' ')
		end--; // end zatrzyma się na pierwszej spacji w końcowym bloku lub na 0

    if (end <= begin) 
        s = "";
    else 
        s = s.substr(begin, end - begin);
}

// zamienia wszystkie duże litery słowa na małe
void normalize(std::string& s) {
    for (size_t i = 0; i < s.length(); i++) {
        if (s[i] >= 'A' && s[i] <= 'Z') {
            s[i] += 32;
        }
    }
}

// bierze wiadomość bez ciała
// zwraca parę <method, request_target>
// zakładam, że wiadomość bez ciała końćzy się CRLF
// rzuca błąd 400 zrywający połączenie
std::pair <std::string, std::string> get_request_line(std::string& message) {
    //std::cout<<"get_request_line\n";
    size_t start = 0;
    size_t end = message.find("\r\n", start); // na pewno znajdzie

    std::string start_line = message.substr(start, end - start); // start line już bez CRLF
    //std::cout<<"start_line:"<<start_line<<std::endl;

    // odcinam start line od wiadomości
    message = message.substr(end + 2);

    start = 0;
    end = start_line.find(" ", start);
    if (end == std::string::npos) {
        throw "400 Bad Request";
    }
    std::string method = start_line.substr(start, end - start);

    start = end + 1;
    end = start_line.find(" ", start);
    if (end == std::string::npos) {
        //std::cout<<"brak spacji po request target\n";
        throw "400 Bad Request";
    }
    std::string request_target = start_line.substr(start, end - start);

    // najpierw sprawdzamy, czy jest / na początku
    // jeśli nie, to error 400
    if (request_target.length() == 0 || request_target[0] != '/') {
        //printf("wrong request target format\n");
        throw "400 Bad Request";
    }

    start = end + 1; // maksymalnie s.length()
    if (start_line.substr(start) != HTTP) {
        throw "400 Bad Request";
    }

    return make_pair(method, request_target);
}

bool valid_field_name(const std::string& s) {
    if (s.length() == 0) { // UWAGA: ZAKŁADAM, ŻE PUSTE FIELD NAME JEST NIEPOPRAWNE
        return false;
    }
    for (size_t i = 0; i < s.length(); i++) {
        if (isalnum(s[i]) == 0 && s[i] != '_' && s[i] != '-') {
            return false;
        }
    }
    return true;
}

std::map <std::string, std::string> get_header_fields(std::string& message) {
    std::map <std::string, std::string> header_fields;

    size_t start = 0;
    size_t end = message.find("\r\n", start);
    while (end != std::string::npos) {
        std::string header = message.substr(start, end - start);
        
        size_t colon = header.find(':');
        if (colon == std::string::npos) { // zakładam, że każdy nagłówek musi mieć dwukropek
            throw "400 Bad Request";
        }

        std::string field_name = header.substr(0, colon);
        normalize(field_name);

        // sprawdzam czy nazwa headera zawiera dozwolone znaki
        if (valid_field_name(field_name) == false) {
            throw "400 Bad Request";
        }

        std::string field_value = header.substr(colon + 1);
        trim(field_value); //usuwam spacje z field value na początku i na końcu

        normalize(field_value); // UWAGA: NIE WIEM CZY FIELD VALUE JEST CASE SENSITIVE

        if (field_name == "connection") {
            // nie sprawdzam czy poprawna wartość dla nagłówka danego typu
            if (field_value != "close" && field_value != "keep-alive") {
                throw "400 Bad Request";
            }
        }
        else if (field_name == "content-length") {
            try {
                size_t pos;
                int len = stoi(field_value, &pos); // sprawdzam czy liczba całkowita i czy nieujemna

                if (pos != field_value.length()) throw "400 Bad Request"; // wartość 00abcab uznajemy za niepoprawną

                if (len < 0) throw "400 Bad Request"; // ten nie zostanie złapany i pójdzie dalej
                
                // Ponieważ w założeniu nasza implementacja obsługuje tylko metody GET i HEAD, 
                // to nasz serwer ma prawo z góry odrzucać wszystkie komunikaty klienta, które zawierają ciało komunikatu.
                // Odrzucenie żądania klienta powinno skutkować wysłaniem przez serwer wiadomości z błędem numer 400.
                if (len > 0) throw "400 Bad Request";
            }
            catch (const std::exception& e) { // to gdyby stoi rzuciło wyjątkiem
                throw "400 Bad Request";
            }
        }
        else { // ignoruję nagłówki niewyspecyfikowane w treści zadania
            start = end + 2;
            end = message.find("\r\n", start);
            continue; 
        }

        if (header_fields.find(field_name) != header_fields.end()) {
            throw "400 Bad Request";
        }
        header_fields[field_name] = field_value;
        
        start = end + 2;
        end = message.find("\r\n", start);
    }
    return header_fields;
}

// ta funkcja może rzucać błąd 400 zrywający połączenie
std::vector<http_message_t> get_messages(std::string& rcv) {
    std::vector<http_message_t> messages;
    size_t start = 0;
    size_t end = rcv.find("\r\n\r\n", start); //zawsze na końcu podwójne CRLF
    
    while (end != std::string::npos) {
        std::string message = rcv.substr(start, end - start + 2); //biorę jedno z dwóch końcowych CRLF do wiadomości
        // teraz message sobie mogę modyfikować jak chcę
        // i tak nie zmienia mi to rcv
        
        std::pair<std::string, std::string> request_line_params = get_request_line(message); // to może rzucić error 400!!!

        request_line_t request_line;
        request_line.method = request_line_params.first;
        request_line.request_target = request_line_params.second;

        // teraz wiadomość obcięta o start line - zostały same headery
        std::map <std::string, std::string> header_fields = get_header_fields(message); // to też może rzucić error 400!!!

        // end + 4 to pozycja zaraz za headerami
        // start to początek tej wiadomości

        std::string message_body = "";
        int len;
        // jeśli wiadomość ma ciało
        if (header_fields.find("content-length") != header_fields.end() && (len = stoi(header_fields["content-length"])) != 0) {
            // Uwaga: tutaj zakładam, że jeśli wartość content-length nie miała poprawnego formatu, to get_header_fields rzuciło error 400
            if (end + 4 + len > rcv.length()) { // jeśli nie da się przeczytać ciała w całości
           
                rcv = rcv.substr(start); // obcinamy rcv do początku tej wiadomości
                return messages; // nie dodajemy tej wiadomości, zwracamy to co już mamy
            }
            message_body = rcv.substr(end + 4, len);
            //std::cout<<"message_body = "<<message_body<<std::endl;
            start = end + 4 + len; // przsuwam start o długość komunikatu
        }
        else {
          start = end + 4; // start za podwójnym CRLD  
        }

        http_message_t http_message;
        http_message.start_line = request_line;
        http_message.header_fields = header_fields;
        http_message.message_body = message_body;
        messages.push_back(http_message);
        
        end = rcv.find("\r\n\r\n", start);
    }
    rcv = rcv.substr(start);
    return messages;
}