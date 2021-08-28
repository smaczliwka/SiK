#ifndef _DEFINITIONS_
#define _DEFINITIONS_

#include <string>
#include <map>

#define QUEUE_LENGTH     5
#define MAX_BUF_LENGTH  4096

const std::string CRLF = "\r\n";
const std::string SP = " ";
const std::string HTTP = "HTTP/1.1";

struct request_line_t {
    std::string method;
    std::string request_target;
};

struct http_message_t {
    request_line_t start_line;
    std::map <std::string, std::string> header_fields;
    std::string message_body;
};

#endif