#include "ftp_client_funcs.h"

/* ==================== 全局变量 ==================== */
char recv_buffer[BUFFER_SIZE];
static char send_buffer[BUFFER_SIZE];
static int client_cmd_socket = -1;
static unsigned int client_cmd_port = 0;
static const char *server_ip = NULL;

/* ==================== 缓冲区管理 ==================== */

/**
 * @brief 清空发送和接收缓冲区
 */
void empty_buffer(void)
{
    memset(recv_buffer, 0, BUFFER_SIZE);
    memset(send_buffer, 0, BUFFER_SIZE);
}

/**
 * @brief 关闭控制连接socket
 */
void close_cmd_socket(void)
{
    if (client_cmd_socket >= 0) {
        shutdown(client_cmd_socket, SHUT_RDWR);
        close(client_cmd_socket);
        client_cmd_socket = -1;
    }
    server_ip = NULL;
    client_cmd_port = 0;
}

/**
 * @brief 获取服务器IP地址
 * @return 服务器IP字符串
 */
const char* get_server_ip(void)
{
    return server_ip;
}

/* ==================== Socket选项设置 ==================== */

/**
 * @brief 设置Socket保持活跃
 * @param socket Socket文件描述符
 * @return 成功返回0，失败返回-1
 */
int set_keepalive(int socket)
{
    if (socket < 0) return -1;
    
    int optval = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval)) < 0) {
        perror("setsockopt(SO_KEEPALIVE)");
        return -1;
    }
    
    /* TCP Keepalive 参数优化 */
#ifdef TCP_KEEPIDLE
    optval = 60;  /* 60秒后开始探测 */
    setsockopt(socket, IPPROTO_TCP, TCP_KEEPIDLE, &optval, sizeof(optval));
#endif

#ifdef TCP_KEEPINTVL
    optval = 10;  /* 探测间隔10秒 */
    setsockopt(socket, IPPROTO_TCP, TCP_KEEPINTVL, &optval, sizeof(optval));
#endif

#ifdef TCP_KEEPCNT
    optval = 3;   /* 探测3次 */
    setsockopt(socket, IPPROTO_TCP, TCP_KEEPCNT, &optval, sizeof(optval));
#endif

    return 0;
}

/**
 * @brief 设置Socket地址复用
 * @param socket Socket文件描述符
 * @return 成功返回0，失败返回-1
 */
int set_reuseaddr(int socket)
{
    if (socket < 0) return -1;
    
    int opt = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt(SO_REUSEADDR)");
        return -1;
    }
    
#ifdef SO_REUSEPORT
    if (setsockopt(socket, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        perror("setsockopt(SO_REUSEPORT)");
        /* SO_REUSEPORT不是必须的，失败不影响功能 */
    }
#endif
    
    return 0;
}

/**
 * @brief 设置Socket超时
 * @param socket Socket文件描述符
 * @param timeout_sec 超时时间（秒）
 * @return 成功返回0，失败返回-1
 */
int set_socket_timeout(int socket, int timeout_sec)
{
    if (socket < 0 || timeout_sec < 0) return -1;
    
    struct timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;
    
    if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("setsockopt(SO_RCVTIMEO)");
        return -1;
    }
    
    if (setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        perror("setsockopt(SO_SNDTIMEO)");
        return -1;
    }
    
    return 0;
}

/**
 * @brief 设置Socket为非阻塞模式
 * @param fd Socket文件描述符
 * @param flags 要设置的标志
 */
void set_flag(int fd, int flags)
{
    if (fd < 0) return;
    
    int val = fcntl(fd, F_GETFL, 0);
    if (val == -1) {
        perror("fcntl(F_GETFL)");
        return;
    }
    val |= flags;
    if (fcntl(fd, F_SETFL, val) < 0) {
        perror("fcntl(F_SETFL)");
    }
}

/**
 * @brief 清除Socket标志
 * @param fd Socket文件描述符
 * @param flags 要清除的标志
 */
void clr_flag(int fd, int flags)
{
    if (fd < 0) return;
    
    int val = fcntl(fd, F_GETFL, 0);
    if (val == -1) {
        perror("fcntl(F_GETFL)");
        return;
    }
    val &= ~flags;
    if (fcntl(fd, F_SETFL, val) < 0) {
        perror("fcntl(F_SETFL)");
    }
}

/* ==================== 命令发送/接收 ==================== */

/**
 * @brief 发送FTP命令
 * @param format 格式化字符串
 * @param ... 参数
 * @return 发送字节数，失败返回-1
 */
int send_cmd(const char *format, ...)
{
    if (format == NULL || client_cmd_socket < 0) {
        return -1;
    }
    
    va_list argp;
    va_start(argp, format);
    int len = vsnprintf(send_buffer, BUFFER_SIZE, format, argp);
    va_end(argp);
    
    if (len < 0 || len >= BUFFER_SIZE) {
        fprintf(stderr, "send_cmd: buffer overflow prevented\n");
        return -1;
    }
    
    ssize_t sent = send(client_cmd_socket, send_buffer, (size_t)len, 0);
    memset(send_buffer, 0, BUFFER_SIZE);
    
    if (sent <= 0) {
        perror("send");
        close_cmd_socket();
        return -1;
    }
    
    return (int)sent;
}

/**
 * @brief 使用select实现带超时的接收
 * @param socket Socket文件描述符
 * @param buffer 接收缓冲区
 * @param buflen 缓冲区长度
 * @param timeout_sec 超时时间（秒）
 * @return 接收字节数，超时返回0，错误返回-1
 */
static ssize_t recv_with_timeout(int socket, char *buffer, size_t buflen, int timeout_sec)
{
    if (socket < 0 || buffer == NULL || buflen == 0) {
        return -1;
    }
    
    fd_set readfds;
    struct timeval tv;
    
    FD_ZERO(&readfds);
    FD_SET(socket, &readfds);
    
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;
    
    int ret = select(socket + 1, &readfds, NULL, NULL, &tv);
    if (ret < 0) {
        if (errno == EINTR) {
            return recv_with_timeout(socket, buffer, buflen, timeout_sec);
        }
        perror("select");
        return -1;
    }
    
    if (ret == 0) {
        /* 超时 */
        return 0;
    }
    
    return recv(socket, buffer, buflen, 0);
}

/**
 * @brief 检查是否为多行响应（响应码后跟'-'）
 * @param str 响应字符串
 * @return 是多行响应返回true
 */
static bool is_multi_response(const char *str)
{
    if (str == NULL || strlen(str) < 4) {
        return false;
    }
    /* 格式: XXX-... 表示多行 */
    return (str[0] >= '0' && str[0] <= '9' &&
            str[1] >= '0' && str[1] <= '9' &&
            str[2] >= '0' && str[2] <= '9' &&
            str[3] == '-');
}

/**
 * @brief 检查是否为多行响应结束（响应码后跟' '）
 * @param buffer 响应缓冲区
 * @param code 响应码
 * @return 是结束行返回true
 */
static bool is_multi_response_end(const char *buffer, const char *code)
{
    if (buffer == NULL || code == NULL) {
        return false;
    }
    /* 格式: XXX ... 表示结束 */
    return (strncmp(buffer, code, 3) == 0 && buffer[3] == ' ');
}

/**
 * @brief 接收FTP服务器响应（支持多行）
 * @return 接收字节数，失败返回-1
 */
int get_response(void)
{
    return get_response_with_timeout(RECV_TIMEOUT_SEC);
}

/**
 * @brief 接收FTP服务器响应（可配置超时）
 * @param timeout_sec 超时时间（秒）
 * @return 接收字节数，失败返回-1
 */
int get_response_with_timeout(int timeout_sec)
{
    memset(recv_buffer, 0, BUFFER_SIZE);
    
    /* 第一次接收 */
    ssize_t length = recv_with_timeout(client_cmd_socket, recv_buffer, 
                                        BUFFER_SIZE - 1, timeout_sec);
    if (length <= 0) {
        if (length == 0) {
            fprintf(stderr, "recv timeout\n");
        } else {
            perror("recv");
        }
        return -1;
    }
    
    recv_buffer[length] = '\0';
    
    /* 处理多行响应 */
    if (is_multi_response(recv_buffer)) {
        char code[4];
        memcpy(code, recv_buffer, 3);
        code[3] = '\0';
        
        size_t total_len = (size_t)length;
        while (!is_multi_response_end(recv_buffer + total_len - 
                       (total_len > 3 ? 3 : 0), code)) {
            char anotherBuff[BUFFER_SIZE];
            ssize_t len = recv_with_timeout(client_cmd_socket, anotherBuff, 
                                             BUFFER_SIZE - 1, timeout_sec);
            if (len <= 0) {
                return -1;
            }
            
            if (total_len + (size_t)len >= BUFFER_SIZE - 1) {
                fprintf(stderr, "response buffer overflow prevented\n");
                return -1;
            }
            
            memcpy(recv_buffer + total_len, anotherBuff, (size_t)len);
            total_len += (size_t)len;
            recv_buffer[total_len] = '\0';
        }
    }
    
    return (int)length;
}

/**
 * @brief 安全读取一行输入
 * @param buffer 缓冲区
 * @param buflen 缓冲区长度
 * @param fp 文件指针
 * @return 成功返回buffer，失败返回NULL
 */
char* fgets_wrapper(char *buffer, size_t buflen, FILE *fp)
{
    if (buffer == NULL || buflen == 0 || fp == NULL) {
        return NULL;
    }
    
    if (fgets(buffer, (int)buflen, fp) != NULL) {
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
        }
        /* 移除可能的回车符 */
        len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\r') {
            buffer[len - 1] = '\0';
        }
        return buffer;
    }
    return NULL;
}

/* ==================== 字符串处理 ==================== */

/**
 * @brief 检查字符串是否以指定前缀开头
 * @param str 目标字符串
 * @param pre 前缀字符串
 * @return 是返回true
 */
bool start_with(const char *str, const char *pre)
{
    if (str == NULL || pre == NULL) {
        return false;
    }
    size_t lenpre = strlen(pre);
    size_t lenstr = strlen(str);
    return (lenstr >= lenpre) && (strncmp(pre, str, lenpre) == 0);
}

/**
 * @brief 检查响应是否以指定代码开头
 * @param response 响应字符串
 * @param code 响应代码
 * @return 匹配返回true
 */
bool respond_with_code(const char *response, int code)
{
    if (response == NULL) {
        return false;
    }
    char code_str[4];
    snprintf(code_str, sizeof(code_str), "%d", code);
    return start_with(response, code_str);
}

/**
 * @brief 检查多行响应中是否存在指定代码
 * @param response 响应字符串
 * @param code 响应代码
 * @return 存在返回true
 */
bool respond_exists_code(const char *response, int code)
{
    if (response == NULL) {
        return false;
    }
    char pattern[16];
    snprintf(pattern, sizeof(pattern), "\r\n%d", code);
    return (strstr(response, pattern) != NULL);
}

/* ==================== 端口解析 ==================== */

/**
 * @brief 从PASV响应中解析数据端口
 * @param recv_buffer PASV响应字符串
 * @return 数据端口号，失败返回0
 */
unsigned int cal_data_port(const char *recv_buffer)
{
    if (recv_buffer == NULL) {
        return 0;
    }
    
    const char *pos1 = strchr(recv_buffer, '(');
    const char *pos2 = strchr(recv_buffer, ')');
    
    if (pos1 == NULL || pos2 == NULL || pos2 <= pos1) {
        return 0;
    }
    
    char passive_res[64];
    size_t len = (size_t)(pos2 - pos1 - 1);
    if (len >= sizeof(passive_res)) {
        len = sizeof(passive_res) - 1;
    }
    strncpy(passive_res, pos1 + 1, len);
    passive_res[len] = '\0';
    
    int p[6];
    char *token;
    const char delim[] = ",";
    char *saveptr = NULL;
    char *temp = strdup(passive_res);
    
    if (temp == NULL) {
        return 0;
    }
    
    int idx = 0;
    token = strtok_r(temp, delim, &saveptr);
    while (token != NULL && idx < 6) {
        p[idx++] = atoi(token);
        token = strtok_r(NULL, delim, &saveptr);
    }
    free(temp);
    
    if (idx < 6) {
        return 0;
    }
    
    return (unsigned int)(p[4] * 256 + p[5]);
}

/* ==================== IP地址验证 ==================== */

/**
 * @brief 检查IP地址是否有效
 * @param ip_addr IP地址字符串
 * @return 有效返回true
 */
bool check_server_ip(const char *ip_addr)
{
    if (ip_addr == NULL) {
        return false;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    return inet_pton(AF_INET, ip_addr, &(addr.sin_addr)) == 1;
}

/* ==================== Socket创建与连接 ==================== */

/**
 * @brief 创建并绑定本地端口的Socket
 * @param local_port 本地端口
 * @return Socket文件描述符，失败返回-1
 */
int get_binded_socket(unsigned int local_port)
{
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    client_addr.sin_port = htons((in_port_t)local_port);
    
    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        perror("socket()");
        return ERR_SOCKET_CREATE;
    }
    
    /* 设置地址复用 */
    if (set_reuseaddr(client_socket) < 0) {
        close(client_socket);
        return ERR_SOCKET_BIND;
    }
    
    /* 绑定端口 */
    if (bind(client_socket, (struct sockaddr*)&client_addr, sizeof(client_addr)) < 0) {
        perror("bind()");
        close(client_socket);
        return ERR_SOCKET_BIND;
    }
    
    return client_socket;
}

/**
 * @brief 连接服务器（带超时）
 * @param socket Socket文件描述符
 * @param server_ip 服务器IP
 * @param server_port 服务器端口
 * @param timeout_sec 超时时间（秒）
 * @return 成功返回0，失败返回-1
 */
int connect_server_with_timeout(int socket, const char *server_ip, 
                                 unsigned int server_port, int timeout_sec)
{
    if (socket < 0 || server_ip == NULL) {
        return -1;
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((in_port_t)server_port);
    
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid IP address: %s\n", server_ip);
        return -1;
    }
    
    /* 设置为非阻塞模式 */
    int flags = fcntl(socket, F_GETFL, 0);
    fcntl(socket, F_SETFL, flags | O_NONBLOCK);
    
    /* 发起连接 */
    int ret = connect(socket, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (ret < 0) {
        if (errno != EINPROGRESS) {
            perror("connect()");
            fcntl(socket, F_SETFL, flags);  /* 恢复原标志 */
            return -1;
        }
        
        /* 等待连接完成 */
        fd_set writefds;
        struct timeval tv;
        
        FD_ZERO(&writefds);
        FD_SET(socket, &writefds);
        
        tv.tv_sec = timeout_sec;
        tv.tv_usec = 0;
        
        ret = select(socket + 1, NULL, &writefds, NULL, &tv);
        if (ret <= 0) {
            if (ret == 0) {
                fprintf(stderr, "connect timeout\n");
            } else {
                perror("select()");
            }
            fcntl(socket, F_SETFL, flags);
            return -1;
        }
        
        /* 检查连接结果 */
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(socket, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
            fprintf(stderr, "connect failed: %s\n", strerror(error));
            fcntl(socket, F_SETFL, flags);
            return -1;
        }
    }
    
    /* 恢复原标志 */
    fcntl(socket, F_SETFL, flags);
    return 0;
}

/**
 * @brief 连接服务器（默认超时）
 * @param socket Socket文件描述符
 * @param server_ip 服务器IP
 * @param server_port 服务器端口
 * @return 成功返回0，失败返回-1
 */
int connect_server(int socket, const char *server_ip, unsigned int server_port)
{
    return connect_server_with_timeout(socket, server_ip, server_port, CONNECT_TIMEOUT_SEC);
}

/**
 * @brief 进入被动模式（修复拼写错误）
 * @return 数据连接Socket，失败返回错误码
 */
int enter_passive_mode(void)
{
    /* 获取随机端口用于数据连接 */
    unsigned int local_port = get_rand_port();
    int client_data_socket = get_binded_socket(local_port);
    if (client_data_socket < 0) {
        return ERR_CREATE_BINDED_SOCKET;
    }
    
    /* 发送PASV命令 */
    if (send_cmd("PASV\r\n") <= 0) {
        close(client_data_socket);
        fprintf(stderr, "send [PASV] command failed\n");
        return ERR_DISCONNECTED;
    }
    
    /* 接收响应 */
    if (get_response() <= 0) {
        close(client_data_socket);
        fprintf(stderr, "receive [PASV] response from server %s failed\n", 
                server_ip ? server_ip : "unknown");
        return ERR_DISCONNECTED;
    }
    
    /* 检查响应码 */
    if (!respond_with_code(recv_buffer, 227)) {
        close(client_data_socket);
        fprintf(stderr, "%s\n", recv_buffer);
        return ERR_INCORRECT_CODE;
    }
    
    /* 解析数据端口 */
    unsigned int server_data_port = cal_data_port(recv_buffer);
    if (server_data_port == 0) {
        close(client_data_socket);
        fprintf(stderr, "parse data port failed\n");
        return ERR_INVALID_PARAM;
    }
    
    /* 连接数据端口 */
    if (connect_server(client_data_socket, server_ip, server_data_port) < 0) {
        close(client_data_socket);
        return ERR_CONNECT_SERVER_FAIL;
    }
    
    /* 设置数据Socket选项 */
    set_socket_timeout(client_data_socket, RECV_TIMEOUT_SEC);
    
    return client_data_socket;
}

/**
 * @brief 创建控制连接并保存状态
 * @param ip 服务器IP
 * @param client_port 客户端端口
 * @param server_port 服务器端口
 * @return 成功返回1，失败返回-1
 */
int get_server_connected_socket(const char *ip, unsigned int client_port, 
                                 unsigned int server_port)
{
    if (ip == NULL) {
        return -1;
    }
    
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    client_addr.sin_port = htons((in_port_t)client_port);
    
    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        perror("socket()");
        return -1;
    }
    
    /* 设置Socket选项 */
    set_reuseaddr(client_socket);
    set_keepalive(client_socket);
    
    /* 绑定端口 */
    if (bind(client_socket, (struct sockaddr*)&client_addr, sizeof(client_addr)) < 0) {
        perror("bind()");
        close(client_socket);
        return -1;
    }
    
    /* 连接服务器 */
    if (connect_server(client_socket, ip, server_port) < 0) {
        close(client_socket);
        return -1;
    }
    
    /* 保存连接状态 */
    server_ip = ip;
    client_cmd_port = client_port;
    client_cmd_socket = client_socket;
    
    return 1;
}

/* ==================== 连接状态检查 ==================== */

/**
 * @brief 检查Socket是否已连接
 * @param socket_fd Socket文件描述符
 * @return 已连接返回true
 */
bool is_connected(int socket_fd)
{
    if (socket_fd < 0) {
        return false;
    }
    
    int error = 0;
    socklen_t len = sizeof(error);
    if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
        return false;
    }
    return (error == 0);
}

/**
 * @brief 检查服务器是否断开连接
 * @return 已断开返回true
 */
bool is_server_disconnected(void)
{
    if (client_cmd_socket < 0) {
        return true;
    }
    
    /* 临时设置为非阻塞 */
    int flags = fcntl(client_cmd_socket, F_GETFL, 0);
    fcntl(client_cmd_socket, F_SETFL, flags | O_NONBLOCK);
    
    char buffer[1];
    ssize_t length = recv(client_cmd_socket, buffer, sizeof(buffer), MSG_PEEK);
    
    /* 恢复原标志 */
    fcntl(client_cmd_socket, F_SETFL, flags);
    
    /* length == 0 表示对端关闭连接 */
    return (length == 0);
}