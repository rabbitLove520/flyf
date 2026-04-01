#include "common.h"

/* ==================== 命令列表 ==================== */
static const char commandlist[NCOMMANDS][10] =
{
    "get", "put", "mget", "mput", "delete",
    "cd", "lcd", "mgetwild", "mputwild",
    "dir", "ldir", "ls", "lls",
    "mkdir", "lmkdir", "rget", "rput",
    "pwd", "lpwd", "ascii", "binary",
    "open", "help", "quit", "exit"
};

/* ==================== 全局变量定义 ==================== */
unsigned short login_time = 0;
bool server_connected = false;
char cmd_read[CMD_READ_BUFFER_SIZE];

/* ==================== 工具函数 ==================== */

/**
 * @brief 将内存区域设置为0
 * @param p 目标内存指针
 * @param size 内存大小
 */
void set0(char *p, size_t size)
{
    if (p != NULL && size > 0) {
        memset(p, 0, size);
    }
}

/**
 * @brief 追加路径到命令结构
 * @param c 命令结构指针
 * @param s 路径字符串
 * @return 成功返回0，失败返回-1
 */
static int append_path(struct command* c, const char* s)
{
    if (c == NULL || s == NULL) {
        return -1;
    }
    
    c->npaths++;
    
    /* 重新分配路径数组 */
    char** temppaths = (char**)realloc(c->paths, c->npaths * sizeof(char*));
    if (temppaths == NULL) {
        c->npaths--;
        return -1;
    }
    c->paths = temppaths;

    /* 分配并复制路径字符串 */
    size_t len = strlen(s) + 1;
    char* temps = (char*)malloc(len * sizeof(char));
    if (temps == NULL) {
        c->npaths--;
        return -1;
    }
    
    /* 复制字符串，将冒号替换为空格 */
    size_t i;
    for (i = 0; i < len - 1; i++) {
        temps[i] = (s[i] == ':') ? ' ' : s[i];
    }
    temps[len - 1] = '\0';

    c->paths[c->npaths - 1] = temps;
    return 0;
}

/**
 * @brief 将用户输入解析为命令结构
 * @param s 用户输入字符串
 * @return 命令结构指针，失败返回NULL
 */
struct command* userinputtocommand(char s[LENUSERINPUT])
{
    if (s == NULL) {
        return NULL;
    }
    
    struct command* cmd = (struct command*)malloc(sizeof(struct command));
    if (cmd == NULL) {
        return NULL;
    }
    
    cmd->id = -1;
    cmd->npaths = 0;
    cmd->paths = NULL;
    
    char* savestate = NULL;
    char* token = NULL;
    char* input_copy = strdup(s);  /* 复制输入，避免修改原字符串 */
    
    if (input_copy == NULL) {
        free(cmd);
        return NULL;
    }
    
    int i, j;
    for (i = 0; ; i++, input_copy = NULL)
    {
        token = strtok_r(input_copy, " \t\n", &savestate);
        if (token == NULL)
            break;
            
        if (cmd->id == -1) {
            for (j = 0; j < NCOMMANDS; j++) {
                if (strcmp(token, commandlist[j]) == 0) {
                    cmd->id = j;
                    break;
                }
            }
        } else {
            if (append_path(cmd, token) < 0) {
                free(input_copy);
                freecommand(cmd);
                return NULL;
            }
        }
    }
    
    free(input_copy);
    
    /* 处理通配符命令 */
    if (cmd->id == MGET && cmd->npaths > 0 && strcmp(cmd->paths[0], "*") == 0)
        cmd->id = MGETWILD;
    else if (cmd->id == MPUT && cmd->npaths > 0 && strcmp(cmd->paths[0], "*") == 0)
        cmd->id = MPUTWILD;
    
    if (cmd->id != -1)
        return cmd;
    else {
        freecommand(cmd);
        return NULL;
    }
}

/**
 * @brief 打印命令结构内容（调试用）
 * @param c 命令结构指针
 */
void printcommand(struct command* c)
{
    if (c == NULL) return;
    
    printf("\tPrinting contents of the above command...\n");
    printf("\tid = %d\n", c->id);
    printf("\tnpaths = %d\n", c->npaths);
    printf("\tpaths =\n");
    for (int i = 0; i < c->npaths; i++) {
        if (c->paths[i] != NULL)
            printf("\t\t%s\n", c->paths[i]);
    }
    printf("\n");
}

/**
 * @brief 释放命令结构内存
 * @param c 命令结构指针
 */
void freecommand(struct command* c)
{
    if (c == NULL) return;
    
    if (c->npaths > 0 && c->paths != NULL) {
        for (int i = 0; i < c->npaths; i++) {
            if (c->paths[i] != NULL) {
                free(c->paths[i]);
                c->paths[i] = NULL;
            }
        }
        free(c->paths);
        c->paths = NULL;
    }
    free(c);
}

/**
 * @brief 获取随机端口（用于数据连接）
 * @return 随机端口号 (10000-60000)
 */
unsigned short get_rand_port(void)
{
    static bool seeded = false;
    if (!seeded) {
        srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
        seeded = true;
    }
    
    unsigned int range = PORT_RANGE_MAX - PORT_RANGE_MIN + 1;
    unsigned int number = rand() % range;
    return (unsigned short)(number + PORT_RANGE_MIN);
}