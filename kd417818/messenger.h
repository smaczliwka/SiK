#ifndef _MESSENGER_
#define _MESSENGER_

#include "definitions.h"

// te wszystkie biblioteki potrzebne serwerowi
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>

bool try_write(int msg_sock, const char* buff, size_t buff_len);

bool send_error_message(const std::string& error, int msg_sock, bool close);

bool send_error_message(const char* error, int msg_sock, bool close);

bool process_message(const http_message_t& message, int msg_sock, const char* CATALOG, const std::map <std::string, std::pair <std::string, std::string>>& correlated);

#endif
