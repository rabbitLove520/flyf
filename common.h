#ifndef _COMMON_H
#define _COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pwd.h>
#include <termios.h>
#include <unistd.h>

/* ==================== 命令枚举 ==================== */
enum COMMAND
{
    GET,
    PUT,
    MGET,
    MPUT,
    DELETE,
    CD,
    LCD,
    MGETWILD,
    MPUTWILD,
    DIR_,
    LDIR,
    LS,
    LLS,
    MKDIR,
    LMKDIR,
    RGET,
    RPUT,
    PWD,
    LPWD,
    ASCII,
    BINARY,
    OPEN,
    HELP,
    QUIT,
    EXIT
};

/* ==================== 常量定义 ==================== */
#define NP 0
#define HP 1
#define LENBUFFER 504
#define LENUSERINPUT 1024
#define NCOMMANDS 25
#define FTP_SERVER_PORT 21

/* 缓冲区大小优化 - 防止溢出 */
#define BUFFER_SIZE 4096              /* 网络缓冲区: 1024 -> 4096 */
#define FILE_READ_BUFFER_SIZE 8192    /* 文件传输缓冲区: 200 -> 8192 */
#define CMD_READ_BUFFER_SIZE 256      /* 命令输入缓冲区: 30 -> 256 */
#define FILE_NAME_MAX_SIZE 512
#define HOSTNAME_MAX_SIZE 256
#define PATH_MAX_SIZE 1024

/* ==================== 错误码定义 ==================== */
#define ERR_DISCONNECTED -503
#define ERR_CREATE_BINDED_SOCKET -523
#define ERR_CONNECT_SERVER_FAIL -500
#define ERR_READ_FAILED -454
#define ERR_INCORRECT_CODE -465
#define ERR_TIMEOUT -600
#define ERR_INVALID_PARAM -601
#define ERR_SOCKET_CREATE -602
#define ERR_SOCKET_BIND -603
#define ERR_SOCKET_CONNECT -604

/* ==================== 超时配置 ==================== */
#define CONNECT_TIMEOUT_SEC 10        /* 连接超时: 10秒 */
#define RECV_TIMEOUT_SEC 30           /* 接收超时: 30秒 */
#define SEND_TIMEOUT_SEC 30           /* 发送超时: 30秒 */

/* ==================== 随机端口配置 ==================== */
#define PORT_RANGE_MIN 10000          /* 随机端口最小值: 1025 -> 10000 */
#define PORT_RANGE_MAX 60000          /* 随机端口最大值: 1125 -> 60000 */

/* ==================== 数据结构 ==================== */
struct command
{
    short int id;
    int npaths;
    char** paths;
};

/* ==================== 全局变量声明 ==================== */
extern unsigned short login_time;
extern bool server_connected;
extern char cmd_read[CMD_READ_BUFFER_SIZE];

/* ==================== 函数声明 ==================== */
struct command* userinputtocommand(char s[LENUSERINPUT]);
void printcommand(struct command* c);
void freecommand(struct command* c);
void set0(char *p, size_t size);
unsigned short get_rand_port(void);

#endif /* _COMMON_H */