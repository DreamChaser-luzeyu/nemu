/***************************************************************************************
* Copyright (c) 2014-2022 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include <isa.h>
#include <cpu/cpu.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "sdb.h"
#include "memory/paddr.h"

static int is_batch_mode = false;

extern void init_regex();

extern void init_wp_pool();
extern void wp_watch(char *expr, word_t res);
extern int free_wp(int n);
extern void print_wp();

/* NOTE: We use the `readline` library to provide more flexibility to read from stdin. */
static char* rl_gets() {
    static char *line_read = NULL;

    if (line_read) {
        free(line_read);
        line_read = NULL;
    }
    // NOTE: We use the `readline` library to provide more flexibility to read from stdin.
    line_read = readline("(nemu) ");

    if (line_read && *line_read) {
        add_history(line_read);
    }

    return line_read;
}

// ----- CMD HANDLERS DECLARATION
static int cmd_c(char *args) {
    cpu_exec(-1);
    return 0;
}
static int cmd_q(char* args) {
    return -1;
}
static int cmd_x(char* args);
static int cmd_help(char* args);
static int cmd_echo(char* args);
static int cmd_n(char* args) {
    cpu_exec(1);
    return 0;
}
static int cmd_info(char* args);
static int cmd_p(char* args);
static int cmd_w(char* args);
static int cmd_d(char* args);

static struct {
    const char *name;
    const char *description;

    int (*handler)(char *);
} cmd_table[] = {
        {"help", "Display information about all supported commands", cmd_help},
        {"c",    "Continue the execution of the program",            cmd_c},
        {"q",    "Exit NEMU",                                        cmd_q},
        // TODO: 在这里添加更多命令
        {"echo", "Output your input after the echo command",         cmd_echo},
        {"n", "Execute a single instruction",                        cmd_n},
        {"info", "Show info of registers, ...",                      cmd_info},
        {"x", "Show memory, usage: x <count> <base_addr>",           cmd_x},
        {"p", "Calculate expression, usage: p <expr>",               cmd_p},
        { "w", "Usage: w EXPR. Watch for the variation of the result of EXPR, pause at variation point", cmd_w },
        { "d", "Usage: d N. Delete watchpoint of wp.NO=N", cmd_d },
};

#define NR_CMD ARRLEN(cmd_table)

// ----- CMD HANDLERS
// 当匹配到命令后，调用cmd_table中注册的回调函数
// args                          - 用户输入的整行字符串中，从 cmd结束后的第一个非空格字符 开始的字符串
// return 0                      - 正常返回
// return <Integer less than 0>  - 异常返回，程序将终止


static int cmd_help(char *args) {
    /* extract the first argument */
    char *arg = strtok(NULL, " ");
    int i;

    if (arg == NULL) {
        /* no argument given */
        for (i = 0; i < NR_CMD; i++) {
            printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
        }
    } else {
        for (i = 0; i < NR_CMD; i++) {
            if (strcmp(arg, cmd_table[i].name) == 0) {
                printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
                return 0;
            }
        }
        printf("Unknown command '%s'\n", arg);
    }
    return 0;
}

static int cmd_echo(char *args) {
    printf("echo: %s", args);
    printf("\n");
    return 0;
}

static int cmd_info(char* args) {
    char* cmd_arg = strtok(NULL, " ");
    printf("param: %s\n", cmd_arg);
    // 打印核心寄存器
    if((strcmp(cmd_arg, "r") == 0) || (strcmp(cmd_arg, "reg") == 0) || (strcmp(cmd_arg, "register") == 0)) {
        isa_reg_display();
    }
    // 打印观察点
    if(strcmp(cmd_arg, "w") == 0) {
        print_wp();
    }
    return 0;
}

static int cmd_x(char* args) {
    char* count_str = strtok(NULL, " ");
    char* base_addr_str = strtok(NULL, " ");
    if(!count_str || !base_addr_str) {
        printf("Invalid parameter.\n");
        return 0;
    }
    // 进制数填写为0，表示让strtoll()自动推断进制
    // 对于16进制，应以0x开始，对于8进制，应以0开始
    // 对于非法的输入，将返回0
    word_t count = strtoll(count_str, NULL, 0);
    word_t base_addr = strtoll(base_addr_str, NULL, 0);
    printf("param: %lu %lu\n", count, base_addr);
    printf("Addr\tData\n");
    for(int i=0; i<count; i++) {
        uint8_t data = paddr_read(base_addr + i, 1);
        printf("%016lx\t%04x\n", base_addr + i, data);
    }

    return 0;
}

static int cmd_p(char* args) {
    bool result;
    word_t expr_cal_result = expr(args, &result);
    if(result) {
        printf("Result in %%lu format: %lu\n", expr_cal_result);
        printf("Result in %%ld format: %ld\n", expr_cal_result);
        printf("Result in %%08lx format: %08lx\n", expr_cal_result);
        printf("Result in %%016lx format: %016lx\n", expr_cal_result);
    } else {
        printf("Invalid expression. \n");
    }
    return 0;
}

static int cmd_w(char* args) {
    if (!args) {
        printf("Usage: w EXPR\n");
        return 0;
    }
    bool success;
    word_t res = expr(args, &success);
    if (!success) {
        puts("invalid expression");
    } else {
        wp_watch(args, res);
    }
    return 0;
}

static int cmd_d(char* args) {
    char *arg = strtok(NULL, "");
    if (!arg) {
        printf("Usage: d N\n");
        return 0;
    }
    int no = strtol(arg, NULL, 10);
    free_wp(no);
    return 0;
}

// -----

/**
 * 配置为不使用sdb，直接运行
 */
void sdb_set_batch_mode() {
    is_batch_mode = true;
}

void sdb_mainloop() {
    if (is_batch_mode) {
        cmd_c(NULL);
        return;
    }

    // --- 循环解析用户输入的命令
    for (char *str; (str = rl_gets()) != NULL;) {
        char *str_end = str + strlen(str);
        // 第一个空格之前的文本被作为命令
        /* extract the first token as the command */
        char *cmd = strtok(str, " ");
        if (cmd == NULL) { continue; }
        // 将命令和空格后续的字符串当作参数，传入命令对应的handler
        /* treat the remaining string as the arguments,
         * which may need further parsing
         */
        char *args = cmd + strlen(cmd) + 1;
        if (args >= str_end) {
            args = NULL;
        }

#ifdef CONFIG_DEVICE
        extern void sdl_clear_event_queue();
        sdl_clear_event_queue();
#endif
        // --- 匹配命令，并调用命令对应的handler
        int i;
        for (i = 0; i < NR_CMD; i++) {
            if (strcmp(cmd, cmd_table[i].name) == 0) {
                // 在此处执行命令的handler
                int ret = cmd_table[i].handler(args);
                if (ret < 0) {
                    // 命令执行异常，打印错误并退出
                    printf("Command \"%s\" returned %d, abort.", cmd_table[i].name, ret);
                    return;
                }
                break;
            }
        }
        // 未匹配任何命令，打印错误
        if (i == NR_CMD) { printf("Unknown command '%s'\n", cmd); }
    }
}

void init_sdb() {
    /* Compile the regular expressions. */
    init_regex();

    /* Initialize the watchpoint pool. */
    init_wp_pool();
}
