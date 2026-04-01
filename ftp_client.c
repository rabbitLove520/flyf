#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "cmds.h"
#include "common.h"
#include "ftp_client_funcs.h"

/**
 * @brief 通过主机名获取IP地址
 * @param name 主机名
 * @return IP地址字符串，失败返回NULL
 */
static const char* getServerIpByHostname(const char* name)
{
    if (name == NULL) {
        return NULL;
    }
    
    /* 先尝试解析为IP地址 */
    if (check_server_ip(name)) {
        return name;
    }
    
    /* 尝试DNS解析 */
    struct hostent *hptr = gethostbyname(name);
    if (hptr == NULL) {
        return NULL;
    }
    
    static char ip_str[INET_ADDRSTRLEN];
    if (inet_ntop(hptr->h_addrtype, hptr->h_addr_list[0], 
                  ip_str, sizeof(ip_str)) == NULL) {
        return NULL;
    }
    
    return ip_str;
}

/**
 * @brief 打印使用说明
 * @param prog_name 程序名
 */
static void print_usage(const char *prog_name)
{
    printf("Usage: %s <server_ip|hostname> [port]\n", prog_name);
    printf("  server_ip: FTP服务器IP地址或主机名\n");
    printf("  port: FTP服务器端口（默认21）\n");
    printf("\nExample:\n");
    printf("  %s 192.168.1.100\n", prog_name);
    printf("  %s ftp.example.com 2121\n", prog_name);
}

/**
 * @brief 主函数
 */
int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }
    
    char *hostname = argv[1];
    const char *server_ip = NULL;
    unsigned int server_port = FTP_SERVER_PORT;

    /* 解析服务器地址 */
    if ((server_ip = getServerIpByHostname(hostname)) == NULL) {
        fprintf(stderr, "Failed to resolve hostname: %s\n", hostname);
        exit(EXIT_FAILURE);
    }
    
    /* 解析端口 */
    if (argc >= 3) {
        char *endptr = NULL;
        long port = strtol(argv[2], &endptr, 10);
        if (*endptr != '\0' || port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port: %s\n", argv[2]);
            exit(EXIT_FAILURE);
        }
        server_port = (unsigned int)port;
    }

    printf("Connecting to %s:%u...\n", server_ip, server_port);
    
    /* 建立控制连接 */
    if (get_server_connected_socket(server_ip, get_rand_port(), server_port) < 0) {
        fprintf(stderr, "Failed to connect to server\n");
        exit(EXIT_FAILURE);
    }
    
    /* 设置信号处理 */
    server_connected = true;
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, exit_cmd);
    
    /* 用户登录 */
    int res = user_login();
    if (res == ERR_DISCONNECTED) {
        server_connected = false;
        close_cmd_socket();
        exit(EXIT_FAILURE);
    }
    
    printf("\n=== FTP Client Ready ===\n");
    printf("Type 'help' for available commands.\n\n");

    /* 命令循环 */
    struct command* cmd = NULL;
    for (;;) {
        printf("ftp> ");
        fflush(stdout);
        
        if (fgets_wrapper(cmd_read, CMD_READ_BUFFER_SIZE, stdin) == NULL) {
            /* EOF或错误 */
            if (feof(stdin)) {
                printf("\n");
                exit_cmd();
            }
            continue;
        }
        
        /* 解析命令 */
        cmd = userinputtocommand(cmd_read);
        if (cmd == NULL) {
            continue;
        }
        
        /* 检查连接状态 */
        if (!server_connected && cmd->id != OPEN && cmd->id != EXIT && cmd->id != QUIT) {
            printf("Not connected. Use 'open' command first.\n");
            freecommand(cmd);
            continue;
        }
        
        if (server_connected && is_server_disconnected()) {
            close_cmd_socket();
            server_connected = false;
            printf("Connection lost.\n");
        }
        
        /* 执行命令 */
        switch(cmd->id) {
            case LS:      ls(); break;
            case LLS:     lls(); break;
            case GET:     get(cmd); break;
            case PUT:     put(cmd); break;
            case CD:      cd(cmd); break;
            case LCD:     lcd(cmd); break;
            case PWD:     pwd(cmd); break;
            case LPWD:    lpwd(cmd); break;
            case ASCII:   ascii(); break;
            case BINARY:  binary(); break;
            case DELETE:  delete_cmd(cmd); break;
            case MKDIR:   create_dir(cmd); break;
            case OPEN:    open_cmd(cmd); break;
            case HELP:    help(); break;
            case QUIT:
            case EXIT:    exit_cmd(); break;
            default:      printf("Unknown command.\n"); break;
        }
        
        freecommand(cmd);
        cmd = NULL;
    }
    
    /* 清理资源（理论上不会执行到这里） */
    close_cmd_socket();
    return EXIT_SUCCESS;
}