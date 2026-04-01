/**
 * @file cmds.c
 * @brief FTP客户端命令实现
 * @author Optimized Version
 * @date 2026
 */

#include "cmds.h"

/* ==================== 全局变量 ==================== */
extern char recv_buffer[BUFFER_SIZE];
extern bool server_connected;
extern char cmd_read[CMD_READ_BUFFER_SIZE];

/* ==================== 辅助函数 ==================== */

/**
 * @brief 获取本地当前工作目录
 * @param buffer 缓冲区
 * @param size 缓冲区大小
 * @return 成功返回0，失败返回-1
 */
static int get_local_cwd(char *buffer, size_t size)
{
    if (buffer == NULL || size == 0) {
        return -1;
    }
    
    if (getcwd(buffer, size) == NULL) {
        perror("getcwd");
        return -1;
    }
    return 0;
}

/**
 * @brief 检查文件是否存在
 * @param filename 文件名
 * @return 存在返回true
 */
static bool file_exists(const char *filename)
{
    if (filename == NULL) {
        return false;
    }
    struct stat st;
    return (stat(filename, &st) == 0);
}

/**
 * @brief 获取文件大小
 * @param filename 文件名
 * @return 文件大小，失败返回-1
 */
static long get_file_size(const char *filename)
{
    if (filename == NULL) {
        return -1;
    }
    struct stat st;
    if (stat(filename, &st) != 0) {
        return -1;
    }
    return (long)st.st_size;
}

/* ==================== 文件传输命令 ==================== */

/**
 * @brief 下载文件 (get命令)
 * @param cmd 命令结构
 */
void get(struct command* cmd)
{
    if (cmd == NULL || cmd->paths == NULL || cmd->npaths == 0) {
        printf("Usage: get <filename> [dest]\n");
        return;
    }
    
    char *filename = cmd->paths[0];
    char *destname = (cmd->npaths >= 2) ? cmd->paths[1] : filename;
    
    printf("Downloading: %s -> %s\n", filename, destname);
    
    int client_data_socket = enter_passive_mode();
    if (client_data_socket < 0) {
        if (client_data_socket == ERR_DISCONNECTED) {
            close_cmd_socket();
            server_connected = false;
        }
        return;
    }
    
    /* 查询文件大小 */
    if (send_cmd("SIZE %s\r\n", filename) <= 0) {
        close(client_data_socket);
        fprintf(stderr, "send [SIZE] command failed\n");
        return;
    }
    
    if (get_response() <= 0) {
        close(client_data_socket);
        return;
    }
    
    long file_size = -1;
    if (respond_with_code(recv_buffer, 213)) {
        char *p = recv_buffer + 4;
        while (*p == ' ') p++;
        file_size = atol(p);
        printf("File size: %ld bytes\n", file_size);
    }

    /* 请求文件 */
    if (send_cmd("RETR %s\r\n", filename) <= 0) {
        close(client_data_socket);
        fprintf(stderr, "send [RETR] command failed\n");
        return;
    }
    
    if (get_response() <= 0) {
        close(client_data_socket);
        return;
    }
    
    if (!respond_with_code(recv_buffer, 125) && !respond_with_code(recv_buffer, 150)) {
        close(client_data_socket);
        printf("%s", recv_buffer);
        return;
    }

    set_flag(client_data_socket, O_NONBLOCK);
    
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        close(client_data_socket);
        return;
    } else if (pid == 0) {
        FILE *fp = fopen(destname, "wb");
        if (fp == NULL) {
            perror("fopen");
            close(client_data_socket);
            _exit(EXIT_FAILURE);
        }
        
        char data_buffer[BUFFER_SIZE];
        size_t total_received = 0;
        time_t start_time = time(NULL);
        
        for (;;) {
            memset(data_buffer, 0, BUFFER_SIZE);
            ssize_t length = recv(client_data_socket, data_buffer, BUFFER_SIZE, 0);
            if (length == 0) {
                break;
            } else if (length < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                    continue;
                }
                perror("recv");
                close(client_data_socket);
                fclose(fp);
                _exit(EXIT_FAILURE);
            }
            
            if (fwrite(data_buffer, 1, (size_t)length, fp) != (size_t)length) {
                perror("fwrite");
                close(client_data_socket);
                fclose(fp);
                _exit(EXIT_FAILURE);
            }
            total_received += (size_t)length;
            
            /* 显示进度 */
            if (file_size > 0) {
                int percent = (int)(total_received * 100 / (size_t)file_size);
                printf("\rProgress: %d%% (%zu/%ld bytes)", percent, total_received, file_size);
                fflush(stdout);
            }
        }
        
        close(client_data_socket);
        fclose(fp);
        
        time_t end_time = time(NULL);
        double duration = difftime(end_time, start_time);
        if (duration > 0) {
            printf("\nDownloaded %zu bytes in %.2f seconds (%.2f KB/s)\n", 
                   total_received, duration, total_received / duration / 1024.0);
        } else {
            printf("\nDownloaded %zu bytes\n", total_received);
        }
        _exit(EXIT_SUCCESS);
    } else {
        if (get_response() <= 0) {
            fprintf(stderr, "receive transfer complete response failed\n");
            waitpid(pid, NULL, 0);
            return;
        }
        if (!respond_with_code(recv_buffer, 226)) {
            fprintf(stderr, "transfer failed\n");
            waitpid(pid, NULL, 0);
            return;
        }
        printf("%s", recv_buffer);
        
        int status = 0;
        waitpid(pid, &status, 0);
        
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            printf("Download completed successfully.\n");
        } else {
            printf("Download failed.\n");
        }
    }
}

/**
 * @brief 上传文件 (put命令)
 * @param cmd 命令结构
 */
void put(struct command* cmd)
{
    if (cmd == NULL || cmd->paths == NULL || cmd->npaths == 0) {
        printf("Usage: put <filename> [dest]\n");
        return;
    }
    
    char *filename = cmd->paths[0];
    char *destname = (cmd->npaths >= 2) ? cmd->paths[1] : filename;
    
    /* 检查文件是否存在 */
    if (!file_exists(filename)) {
        printf("Error: File '%s' does not exist\n", filename);
        return;
    }
    
    long file_size = get_file_size(filename);
    printf("Uploading: %s -> %s (%ld bytes)\n", filename, destname, file_size);
    
    int client_data_socket = enter_passive_mode();
    if (client_data_socket < 0) {
        if (client_data_socket == ERR_DISCONNECTED) {
            close_cmd_socket();
            server_connected = false;
        }
        return;
    }

    if (send_cmd("STOR %s\r\n", destname) <= 0) {
        close(client_data_socket);
        fprintf(stderr, "send [STOR] command failed\n");
        return;
    }
    
    if (get_response() <= 0) {
        close(client_data_socket);
        return;
    }
    
    if (!respond_with_code(recv_buffer, 125) && !respond_with_code(recv_buffer, 150)) {
        close(client_data_socket);
        printf("%s", recv_buffer);
        return;
    }

    set_flag(client_data_socket, O_NONBLOCK);
    
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        close(client_data_socket);
        return;
    } else if (pid == 0) {
        FILE *fp = fopen(filename, "rb");
        if (fp == NULL) {
            perror("fopen");
            close(client_data_socket);
            _exit(EXIT_FAILURE);
        }
        
        char data_buffer[FILE_READ_BUFFER_SIZE];
        size_t total_sent = 0;
        ssize_t numread;
        time_t start_time = time(NULL);
        
        for (;;) {
            memset(data_buffer, 0, FILE_READ_BUFFER_SIZE);
            numread = fread(data_buffer, 1, FILE_READ_BUFFER_SIZE, fp);
            if (numread <= 0) {
                break;
            }
            
            ssize_t length = send(client_data_socket, data_buffer, (size_t)numread, 0);
            if (length <= 0) {
                if (length < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                    continue;
                }
                perror("send");
                fclose(fp);
                close(client_data_socket);
                _exit(EXIT_FAILURE);
            }
            total_sent += (size_t)length;
            
            /* 显示进度 */
            if (file_size > 0) {
                int percent = (int)(total_sent * 100 / (size_t)file_size);
                printf("\rProgress: %d%% (%zu/%ld bytes)", percent, total_sent, file_size);
                fflush(stdout);
            }
        }
        
        /* 优雅关闭发送端 */
        shutdown(client_data_socket, SHUT_WR);
        fclose(fp);
        close(client_data_socket);
        
        time_t end_time = time(NULL);
        double duration = difftime(end_time, start_time);
        if (duration > 0) {
            printf("\nUploaded %zu bytes in %.2f seconds (%.2f KB/s)\n", 
                   total_sent, duration, total_sent / duration / 1024.0);
        } else {
            printf("\nUploaded %zu bytes\n", total_sent);
        }
        _exit(EXIT_SUCCESS);
    } else {
        close(client_data_socket);
        
        int status = 0;
        waitpid(pid, &status, 0);
        
        if (status != 0) {
            fprintf(stderr, "upload failed\n");
            return;
        }
        
        if (get_response() <= 0) {
            close_cmd_socket();
            server_connected = false;
            return;
        }
        printf("%s", recv_buffer);
        
        if (respond_with_code(recv_buffer, 226)) {
            printf("Upload completed successfully.\n");
        } else {
            printf("Upload failed.\n");
        }
    }
}

/* ==================== 目录操作命令 ==================== */

/**
 * @brief 列出远程目录 (ls命令)
 */
void ls(void)
{
    printf("Listing remote directory...\n");
    
    int client_data_socket = enter_passive_mode();
    if (client_data_socket < 0) {
        if (client_data_socket == ERR_DISCONNECTED) {
            close_cmd_socket();
            server_connected = false;
        }
        return;
    }
    
    if (send_cmd("LIST\r\n") <= 0) {
        close(client_data_socket);
        fprintf(stderr, "send [LIST] command failed\n");
        return;
    }

    if (get_response() <= 0) {
        close(client_data_socket);
        fprintf(stderr, "receive [LIST] response failed\n");
        return;
    }
    
    if (!respond_with_code(recv_buffer, 125) && !respond_with_code(recv_buffer, 150)) {
        close(client_data_socket);
        printf("%s", recv_buffer);
        return;
    }
    
    /* 设置为非阻塞模式 */
    set_flag(client_data_socket, O_NONBLOCK);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        close(client_data_socket);
        return;
    } else if (pid == 0) {
        char data_buffer[BUFFER_SIZE];
        char *ptr = NULL;
        size_t data_len = 0;
        size_t pre_len = 0;
        
        for (;;) {
            memset(data_buffer, 0, BUFFER_SIZE);
            ssize_t length = recv(client_data_socket, data_buffer, BUFFER_SIZE - 1, 0);
            if (length == 0) {
                break;
            } else if (length < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                    continue;
                }
                perror("recv");
                close(client_data_socket);
                _exit(EXIT_FAILURE);
            }
            
            pre_len = data_len;
            data_len += (size_t)length;
            
            char *tmp_ptr = (char*)realloc(ptr, data_len + 1);
            if (tmp_ptr == NULL) {
                free(ptr);
                close(client_data_socket);
                _exit(EXIT_FAILURE);
            }
            ptr = tmp_ptr;
            
            memcpy(ptr + pre_len, data_buffer, (size_t)length);
            ptr[data_len] = '\0';
        }
        
        close(client_data_socket);
        
        /* 编码转换并输出 */
        if (data_len > 0) {
            char *tmp_ptr = (char*)calloc(data_len * 2 + 1, sizeof(char));
            if (tmp_ptr != NULL) {
                g2u(ptr, data_len, tmp_ptr, data_len * 2);
                printf("%s", tmp_ptr);
                free(tmp_ptr);
            } else {
                printf("%s", ptr);
            }
            free(ptr);
        }
        _exit(EXIT_SUCCESS);
    } else {
        if (!respond_exists_code(recv_buffer, 226)) {
            if (get_response() <= 0) {
                fprintf(stderr, "receive transfer complete response failed\n");
                waitpid(pid, NULL, 0);
                return;
            }
            if (!respond_with_code(recv_buffer, 226)) {
                fprintf(stderr, "transfer failed\n");
                waitpid(pid, NULL, 0);
                return;
            }
        }
        
        int status = 0;
        waitpid(pid, &status, 0);
        
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            printf("Directory listing completed.\n");
        }
    }
}

/**
 * @brief 列出本地目录 (lls命令)
 */
void lls(void)
{
    char cwd[PATH_MAX_SIZE];
    if (get_local_cwd(cwd, sizeof(cwd)) < 0) {
        return;
    }
    
    printf("Local directory: %s\n", cwd);
    
    DIR *dir = opendir(".");
    if (dir == NULL) {
        perror("opendir");
        return;
    }
    
    struct dirent *entry;
    struct stat st;
    
    printf("%-50s %-10s %-20s\n", "NAME", "SIZE", "TYPE");
    printf("------------------------------------------------------------\n");
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        if (stat(entry->d_name, &st) == 0) {
            const char *type = S_ISDIR(st.st_mode) ? "DIRECTORY" : "FILE";
            printf("%-50s %-10ld %-20s\n", entry->d_name, (long)st.st_size, type);
        }
    }
    
    closedir(dir);
}

/**
 * @brief 切换远程目录 (cd命令)
 * @param cmd 命令结构
 */
void cd(struct command* cmd)
{
    if (cmd == NULL || cmd->paths == NULL || cmd->npaths == 0) {
        printf("Usage: cd <directory>\n");
        return;
    }
    
    char *dirname = cmd->paths[0];
    
    if (send_cmd("CWD %s\r\n", dirname) <= 0) {
        fprintf(stderr, "send [CWD] command failed\n");
        return;
    }
    
    if (get_response() <= 0) {
        return;
    }
    
    printf("%s", recv_buffer);
    
    if (respond_with_code(recv_buffer, 250)) {
        printf("Changed directory to: %s\n", dirname);
    }
}

/**
 * @brief 切换本地目录 (lcd命令)
 * @param cmd 命令结构
 */
void lcd(struct command* cmd)
{
    if (cmd == NULL || cmd->paths == NULL || cmd->npaths == 0) {
        printf("Usage: lcd <directory>\n");
        return;
    }
    
    char *dirname = cmd->paths[0];
    
    if (chdir(dirname) < 0) {
        perror("chdir");
        return;
    }
    
    char cwd[PATH_MAX_SIZE];
    if (get_local_cwd(cwd, sizeof(cwd)) == 0) {
        printf("Local directory changed to: %s\n", cwd);
    }
}

/**
 * @brief 显示远程当前目录 (pwd命令)
 * @param cmd 命令结构 (未使用)
 */
void pwd(struct command* cmd)
{
    (void)cmd;  /* 避免未使用参数警告 */
    
    if (send_cmd("PWD\r\n") <= 0) {
        fprintf(stderr, "send [PWD] command failed\n");
        return;
    }
    
    if (get_response() <= 0) {
        return;
    }
    
    printf("%s", recv_buffer);
}

/**
 * @brief 显示本地当前目录 (lpwd命令)
 * @param cmd 命令结构 (未使用)
 */
void lpwd(struct command* cmd)
{
    (void)cmd;  /* 避免未使用参数警告 */
    
    char cwd[PATH_MAX_SIZE];
    if (get_local_cwd(cwd, sizeof(cwd)) == 0) {
        printf("Local working directory: %s\n", cwd);
    }
}

/**
 * @brief 创建远程目录 (mkdir命令)
 * @param cmd 命令结构
 */
void create_dir(struct command* cmd)
{
    if (cmd == NULL || cmd->paths == NULL || cmd->npaths == 0) {
        printf("Usage: mkdir <directory>\n");
        return;
    }
    
    char *dirname = cmd->paths[0];
    
    if (send_cmd("MKD %s\r\n", dirname) <= 0) {
        fprintf(stderr, "send [MKD] command failed\n");
        return;
    }
    
    if (get_response() <= 0) {
        return;
    }
    
    printf("%s", recv_buffer);
}

/* ==================== 删除命令 ==================== */

/**
 * @brief 删除远程文件 (delete命令)
 * @param cmd 命令结构
 */
void delete_cmd(struct command* cmd)
{
    if (cmd == NULL || cmd->paths == NULL || cmd->npaths == 0) {
        printf("Usage: delete <filename>\n");
        return;
    }
    
    char *filename = cmd->paths[0];
    
    if (send_cmd("DELE %s\r\n", filename) <= 0) {
        fprintf(stderr, "send [DELE] command failed\n");
        return;
    }
    
    if (get_response() <= 0) {
        return;
    }
    
    printf("%s", recv_buffer);
}

/* ==================== 传输模式命令 ==================== */

/**
 * @brief 切换到ASCII模式
 */
void ascii(void)
{
    if (send_cmd("TYPE A\r\n") <= 0) {
        fprintf(stderr, "send [TYPE A] command failed\n");
        return;
    }
    
    if (get_response() <= 0) {
        return;
    }
    
    printf("%s", recv_buffer);
    
    if (respond_with_code(recv_buffer, 200)) {
        printf("Transfer mode set to ASCII.\n");
    }
}

/**
 * @brief 切换到二进制模式
 */
void binary(void)
{
    if (send_cmd("TYPE I\r\n") <= 0) {
        fprintf(stderr, "send [TYPE I] command failed\n");
        return;
    }
    
    if (get_response() <= 0) {
        return;
    }
    
    printf("%s", recv_buffer);
    
    if (respond_with_code(recv_buffer, 200)) {
        printf("Transfer mode set to Binary.\n");
    }
}

/* ==================== 连接管理命令 ==================== */

/**
 * @brief 打开新连接 (open命令)
 * @param cmd 命令结构
 */
void open_cmd(struct command* cmd)
{
    if (cmd == NULL || cmd->paths == NULL || cmd->npaths == 0) {
        printf("Usage: open <server_ip> [port]\n");
        return;
    }
    
    char *hostname = cmd->paths[0];
    unsigned int port = FTP_SERVER_PORT;
    
    if (cmd->npaths >= 2) {
        char *endptr = NULL;
        long p = strtol(cmd->paths[1], &endptr, 10);
        if (*endptr == '\0' && p > 0 && p <= 65535) {
            port = (unsigned int)p;
        }
    }
    
    /* 关闭现有连接 */
    if (server_connected) {
        close_cmd_socket();
        server_connected = false;
    }
    
    /* 解析主机名 */
    const char *server_ip = NULL;
    if (check_server_ip(hostname)) {
        server_ip = hostname;
    } else {
        struct hostent *hptr = gethostbyname(hostname);
        if (hptr == NULL) {
            fprintf(stderr, "Failed to resolve hostname: %s\n", hostname);
            return;
        }
        static char ip_str[INET_ADDRSTRLEN];
        if (inet_ntop(hptr->h_addrtype, hptr->h_addr_list[0], 
                      ip_str, sizeof(ip_str)) == NULL) {
            fprintf(stderr, "Failed to convert IP address\n");
            return;
        }
        server_ip = ip_str;
    }
    
    printf("Connecting to %s:%u...\n", server_ip, port);
    
    if (get_server_connected_socket(server_ip, get_rand_port(), port) < 0) {
        fprintf(stderr, "Failed to connect to server\n");
        return;
    }
    
    server_connected = true;
    printf("Connected to %s\n", server_ip);
    
    /* 用户登录 */
    int res = user_login();
    if (res == ERR_DISCONNECTED) {
        server_connected = false;
        close_cmd_socket();
    }
}

/**
 * @brief 退出程序 (exit/quit命令)
 */
void exit_cmd(void)
{
    printf("\nClosing connection...\n");
    
    if (server_connected) {
        if (send_cmd("QUIT\r\n") > 0) {
            get_response();
            printf("%s", recv_buffer);
        }
        close_cmd_socket();
        server_connected = false;
    }
    
    printf("Goodbye!\n");
    exit(EXIT_SUCCESS);
}

/* ==================== 帮助命令 ==================== */

/**
 * @brief 显示帮助信息
 */
void help(void)
{
    printf("\n");
    printf("=== FTP Client Commands ===\n\n");
    printf("  Connection:\n");
    printf("    open <host> [port]  - Connect to FTP server\n");
    printf("    quit/exit           - Close connection and exit\n\n");
    printf("  File Transfer:\n");
    printf("    get <file> [dest]   - Download file from server\n");
    printf("    put <file> [dest]   - Upload file to server\n");
    printf("    ascii               - Set ASCII transfer mode\n");
    printf("    binary              - Set Binary transfer mode\n\n");
    printf("  Directory:\n");
    printf("    ls                  - List remote directory\n");
    printf("    lls                 - List local directory\n");
    printf("    cd <dir>            - Change remote directory\n");
    printf("    lcd <dir>           - Change local directory\n");
    printf("    pwd                 - Print remote working directory\n");
    printf("    lpwd                - Print local working directory\n");
    printf("    mkdir <dir>         - Create remote directory\n");
    printf("    delete <file>       - Delete remote file\n\n");
    printf("  Other:\n");
    printf("    help                - Show this help message\n\n");
    printf("=== End of Help ===\n\n");
}

/* ==================== 用户登录 ==================== */

/**
 * @brief 用户登录流程
 * @return 成功返回0，失败返回错误码
 */
int user_login(void)
{
    if (get_response() <= 0) {
        fprintf(stderr, "Failed to receive server greeting\n");
        return ERR_DISCONNECTED;
    }
    
    printf("%s", recv_buffer);
    
    /* 输入用户名 */
    printf("Username: ");
    fflush(stdout);
    
    if (fgets_wrapper(cmd_read, CMD_READ_BUFFER_SIZE, stdin) == NULL) {
        return ERR_DISCONNECTED;
    }
    
    /* 移除换行符 */
    size_t len = strlen(cmd_read);
    if (len > 0 && cmd_read[len - 1] == '\n') {
        cmd_read[len - 1] = '\0';
    }
    
    if (send_cmd("USER %s\r\n", cmd_read) <= 0) {
        fprintf(stderr, "send [USER] command failed\n");
        return ERR_DISCONNECTED;
    }
    
    if (get_response() <= 0) {
        return ERR_DISCONNECTED;
    }
    
    printf("%s", recv_buffer);
    
    /* 检查是否需要密码 */
    if (respond_with_code(recv_buffer, 331)) {
        /* 输入密码 */
        printf("Password: ");
        fflush(stdout);
        
        /* 隐藏密码输入 */
        struct termios oldt, newt;
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~ECHO;
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        
        if (fgets_wrapper(cmd_read, CMD_READ_BUFFER_SIZE, stdin) == NULL) {
            tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
            return ERR_DISCONNECTED;
        }
        
        /* 恢复终端设置 */
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        printf("\n");
        
        /* 移除换行符 */
        len = strlen(cmd_read);
        if (len > 0 && cmd_read[len - 1] == '\n') {
            cmd_read[len - 1] = '\0';
        }
        
        if (send_cmd("PASS %s\r\n", cmd_read) <= 0) {
            fprintf(stderr, "send [PASS] command failed\n");
            return ERR_DISCONNECTED;
        }
        
        if (get_response() <= 0) {
            return ERR_DISCONNECTED;
        }
        
        printf("%s", recv_buffer);
        
        if (!respond_with_code(recv_buffer, 230)) {
            fprintf(stderr, "Login failed\n");
            return ERR_INCORRECT_CODE;
        }
    } else if (!respond_with_code(recv_buffer, 230)) {
        fprintf(stderr, "Login failed\n");
        return ERR_INCORRECT_CODE;
    }
    
    printf("Login successful!\n");
    return 0;
}