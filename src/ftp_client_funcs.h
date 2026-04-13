#ifndef _FTP_CLIENT_FUNCS_H
#define _FTP_CLIENT_FUNCS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <regex.h>
#include <stdarg.h>
#include <time.h>

#include "encode.h"
#include "common.h"

/* ==================== Socket配置 ==================== */
#define MAX_RETRY_COUNT 3              /* 最大重试次数 */
#define SOCKET_BACKLOG 5               /* listen backlog */

/* ==================== 全局变量 ==================== */
extern char recv_buffer[BUFFER_SIZE];

/* ==================== 缓冲区管理 ==================== */
void empty_buffer(void);
void close_cmd_socket(void);

/* ==================== 命令发送/接收 ==================== */
int send_cmd(const char* format, ...);
int get_response(void);
int get_response_with_timeout(int timeout_sec);

/* ==================== 响应处理 ==================== */
bool start_with(const char *str, const char *pre);
bool respond_with_code(const char *response, int code);
bool respond_exists_code(const char *response, int code);

/* ==================== 端口解析 ==================== */
unsigned int cal_data_port(const char *recv_buffer);

/* ==================== Socket操作 ==================== */
bool is_connected(int socket_fd);
bool check_server_ip(const char *server_ip);
int connect_server(int socket, const char *server_ip, unsigned int server_port);
int connect_server_with_timeout(int socket, const char *server_ip, 
                                 unsigned int server_port, int timeout_sec);
int enter_passive_mode(void);  /* 修复拼写错误: passvie -> passive */
int get_server_connected_socket(const char *server_ip, 
                                 unsigned int client_port, 
                                 unsigned int server_port);

/* ==================== Socket选项 ==================== */
void set_flag(int fd, int flags);
void clr_flag(int fd, int flags);
int set_socket_timeout(int socket, int timeout_sec);
int set_keepalive(int socket);
int set_reuseaddr(int socket);

/* ==================== 连接状态 ==================== */
bool is_server_disconnected(void);
const char* get_server_ip(void);

/* ==================== 辅助函数 ==================== */
char* fgets_wrapper(char *buffer, size_t buflen, FILE *fp);

#endif /* _FTP_CLIENT_FUNCS_H */