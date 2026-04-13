/**
 * @file cmds.h
 * @brief FTP客户端命令声明
 */

#ifndef _CMDS_H
#define _CMDS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <dirent.h>
#include <termios.h>
#include <time.h>

#include "common.h"
#include "ftp_client_funcs.h"
#include "encode.h"

/* ==================== 函数声明 ==================== */

/* 文件传输 */
void get(struct command* cmd);
void put(struct command* cmd);

/* 目录操作 */
void ls(void);
void lls(void);
void cd(struct command* cmd);
void lcd(struct command* cmd);
void pwd(struct command* cmd);
void lpwd(struct command* cmd);
void create_dir(struct command* cmd);

/* 删除 */
void delete_cmd(struct command* cmd);

/* 传输模式 */
void ascii(void);
void binary(void);

/* 连接管理 */
void open_cmd(struct command* cmd);
void exit_cmd(void);

/* 帮助 */
void help(void);

/* 登录 */
int user_login(void);

#endif /* _CMDS_H */