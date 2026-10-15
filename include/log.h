// log.h
#ifndef LOG_H
#define LOG_H
#include "common.h"

void log_connection(const char *username, const char *ip);
void log_disconnection(const char *username, const char *ip);
void log_server_start(int port);
void log_event(const char *fmt, ...); 
void send_log_to_client(client_t *cli);
void timestamp(char *buffer, size_t len);
#endif
