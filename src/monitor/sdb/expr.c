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

/* We use the POSIX regex functions to process regular expressions.
 * Type 'man regex' for more information about POSIX regex functions.
 */
#include <regex.h>
#include "memory/vaddr.h"

typedef enum {
    // 多含义的token，将枚举名模糊化
    DUMMY_1 = '+', DUMMY_2 = '-', DUMMY_3 = '*', TK_DIV = '/',
    // 单一含义的单字符token，使用其ASCII作为枚举值
    TK_PAR_L = '(', TK_PAR_R = ')', TK_BOOL_LT = '<', TK_BOOL_GT = '>',
    // 我们将自定义token的token_type数值设置为256开始
    // 因为对于单字符的token，我们直接使用它的ASCII数值作为token_type
    TK_NOTYPE = 256, TK_BOOL_EQ, TK_INT, TK_REG, TK_BOOL_LE, TK_BOOL_GE,
    TK_ADD, TK_MINUS, TK_MUL,
    TK_POS, TK_NEG, TK_DEREF,
} token_enum_t;

/**
 * 获得token运算符的优先级
 * 一元运算 - 3
 * 乘除运算 - 2
 * 加减运算 - 1
 * 比较运算 - 0
 * @param op_type
 * @return 运算符优先级
 */
int get_priority(token_enum_t op_type) {
    if(op_type == TK_POS
    || op_type == TK_NEG
    || op_type == TK_DEREF) { return 3; }

    if(op_type == TK_MUL || op_type == TK_DIV) { return 2; }

    if(op_type == TK_ADD || op_type == TK_MINUS) { return 1; }

    if(op_type == TK_BOOL_EQ
    || op_type == TK_BOOL_LT
    || op_type == TK_BOOL_GT
    || op_type == TK_BOOL_LE
    || op_type == TK_BOOL_GE) { return 0; }
    // You should never get the priority of an operand
    assert(0);
}

bool is_operand(token_enum_t op_type) {
    if(op_type == TK_INT || op_type == TK_REG || op_type == ')') { return true; }
    return false;
}

// 匹配各种token的正则表达式
static struct rule {
    const char *regex;
    token_enum_t token_type;
} rules[] = {

        /* TODO: Add more rules.
         * Pay attention to the precedence level of different rules.
         */

        {" +",      TK_NOTYPE},           // 一个或多个空格
        // 由于加号在正则表达式中具有特殊含义，因此匹配加号时需要转义
        {"\\+",     '+'},                 // DUMMY （多种含义）
        {"-",       '-'},                 // DUMMY
        {"\\*",     '*'},                 // DUMMY
        {"/",       '/'},                 // 除号
        {"==",      TK_BOOL_EQ},          // 布尔等于
        {"(0x)?[0-9]+", TK_INT},          // 正整数
        {"\\(",     '('},                 // 左括号
        {"\\)",     ')'},                 // 右括号
        {"\\$\\w+", TK_REG},              // 寄存器
        {"<", '<'},                      // 小于
        {">", '>'},
        {"<=", TK_BOOL_LE},
        {">=", TK_BOOL_GE},
};

#define NR_REGEX ARRLEN(rules)

static regex_t re[NR_REGEX] = {};

/*
 * 将正则表达式的字符串编译为`regex_t`结构
 */
void init_regex() {
    int i;
    char error_msg[128];
    int ret;
    // 遍历正则表达式并编译
    for (i = 0; i < NR_REGEX; i++) {
        ret = regcomp(&re[i], rules[i].regex, REG_EXTENDED);
        if (ret != 0) {
            regerror(ret, &re[i], error_msg, 128);
            panic("regex compilation failed: %s\n%s", error_msg, rules[i].regex);
        }
    }
}

typedef struct token {
    int type;
    char str[32];
} Token;

// 存储解析出来的token
static Token tokens[32] __attribute__((used)) = {};
// 存储解析出来的token的数量
static int num_of_token __attribute__((used)) = 0;


/**
 * 词法分析 从用户输入的表达式中匹配出token，并作出处理
 * @param str_to_be_parsed 用户输入的表达式
 * @return
 *     - true:  用户的输入没有语法错误
 *     - false: 用户的输入存在语法错误
 */
static bool make_token(char *str_to_be_parsed) {
    int parse_position = 0;
    int i;

    num_of_token = 0;

    while (str_to_be_parsed[parse_position] != '\0') {
        // 对每一个规则进行尝试
        for (i = 0; i < NR_REGEX; i++) {
            regmatch_t parse_result; // 用于存储匹配到的子字符串的开始及结束下标
            // 对于当前规则，尝试进行匹配
            int res = regexec(&re[i], str_to_be_parsed + parse_position, 1, &parse_result, 0);
            // 能够匹配上，且匹配到的子字符串 从 字符串的起始字符开始
            if (res == 0 && parse_result.rm_so == 0) {
                char *substr_start = str_to_be_parsed + parse_position;
                int substr_len = parse_result.rm_eo;

                Log("match rules[%d] = \"%s\" at parse_position %d with len %d: %.*s",
                    i, rules[i].regex, parse_position, substr_len, substr_len, substr_start);

                parse_position += substr_len; // 修改下一轮匹配的开始坐标
                // --- 处理匹配到的token
                switch(rules[i].token_type) {
                    // 不记录空白字符到token数组
                    case TK_NOTYPE: break;
                    // 处理寄存器和数字类型
                    case TK_REG: case TK_INT:
                        memcpy(tokens[num_of_token].str, substr_start, substr_len);
                        (tokens[num_of_token].str)[substr_len] = '\0';
                        tokens[num_of_token].type = rules[i].token_type;
                        num_of_token++;
                        break;
                    // 处理多种含义的token
                    case '*': case '-': case '+':
                        if(num_of_token == 0                               // 符号是第一个运算符时，应被解析为一元运算符
                        || !is_operand(tokens[num_of_token-1].type) // 上一个符号不是整数、寄存器或者右括号
                        ) {
                            // 我们使用这个符号的一元运算符含义
                            switch(rules[i].token_type) {
                                case '+': tokens[num_of_token].type = TK_POS; break;
                                case '-': tokens[num_of_token].type = TK_NEG; break;
                                case '*': tokens[num_of_token].type = TK_DEREF; break;
                                default: assert(0); // Should never reach here
                            }
                        }
                        else {
                            // 我们使用这个符号的二元运算符含义
                            switch(rules[i].token_type) {
                                case '+': tokens[num_of_token].type = TK_ADD; break;
                                case '-': tokens[num_of_token].type = TK_MINUS; break;
                                case '*': tokens[num_of_token].type = TK_MUL; break;
                                default: assert(0);
                            }
                        }
                        num_of_token++;
                        break;
                    // 处理其他单一含义类型token
                    default:
                        tokens[num_of_token].type = rules[i].token_type;
                        num_of_token++;
                        break;
                }
                break;  // 一个正则表达式匹配成功后就可以结束了
            }
        }
        // 字符串没有能够匹配的规则，用户输入的表达式不正确，报错
        if (i == NR_REGEX) {
            printf("no match at parse_position %d\n%s\n%*.s^\n", parse_position, str_to_be_parsed, parse_position, "");
            return false;
        }
    }

    return true;
}

bool check_parentheses(int begin_index, int end_index) {
    int num_of_left_parentheses = 0;
    for (int i = begin_index; i <= end_index; i++) {
        if (tokens[i].type == '(') {
            num_of_left_parentheses++;
        }
        if (tokens[i].type == ')') {
            num_of_left_parentheses--;
            if (num_of_left_parentheses < 0) { return false; }
        }
    }
    return true;
}

/**
 * 寻找主运算符
 * @param begin_index
 * @param end_index
 * @return 主运算符的下标
 */
int find_major_operator(int begin_index, int end_index) {
    // --- 找到主运算符的位置
    /**
     * 主运算符应满足的条件
     * - 是一个运算符
     * - 出现在一对括号中的运算符不是主运算符
     * - 不被任何一对括号包围的、优先级最低的运算符是主运算符
     * - 如果存在多个 不被任何一对括号包围的、优先级最低的运算符，
     *   那么最后被结合的运算符是主运算符
     */
    int major_op_pos = -1;
    int num_of_left_parentheses = 0;
    // 规定乘除运算的优先级为2，加减运算的优先级为1，布尔运算的优先级为0
    int min_priority = 2;   // 数字越大，优先级越高，设定为2杜绝将单目运算符解析为主运算符
    for (int i = begin_index; i < end_index; i++) {
        // 跳过数字
        if (tokens[i].type == TK_INT) { continue; }
        // 跳过括号本身
        if (tokens[i].type == '(') { num_of_left_parentheses++;
            continue; }
        if (tokens[i].type == ')') { num_of_left_parentheses--;
            continue;  }
        // 跳过括号中的运算符
        if (num_of_left_parentheses > 0) { continue; }
        if (num_of_left_parentheses < 0) { assert(0); }    // Should never reach here
        // --- 从前往后寻找主运算符
        // 寻找优先级低的、结合顺序靠后的运算符
        if(get_priority(tokens[i].type) <= min_priority) {
            min_priority = get_priority(tokens[i].type);
            major_op_pos = i;
        }
    }
    return major_op_pos;
}

int64_t cal_expr(int begin_index, int end_index) {
    // 检查括号是否匹配
    if (!check_parentheses(begin_index, end_index)) {
        printf("Parentheses not match.\n");
        return 0;
    }
    // 去除包裹此表达式的括号
    if (tokens[begin_index].type == '(' && tokens[end_index].type == ')') {
        begin_index++;
        end_index--;
    }

    if (begin_index > end_index) {
        // 错误的起止下标
        printf("Expected expression at token index %d\n", begin_index);
    }
    else if (begin_index == end_index) {
        // 单个token的表达式
        token_enum_t op_type = tokens[begin_index].type;
        switch(op_type) {
            case TK_INT: return strtol(tokens[begin_index].str, NULL, 0);
            case TK_REG: {
                bool feedback;
                word_t result = isa_reg_str2val(tokens[begin_index].str, &feedback);
                if(feedback) { return (int64_t)result; }
                printf("Invalid register expression: %s", tokens[begin_index].str);
                return 0;
            }
            default:
                printf("Invalid expression: %s\n", tokens[begin_index].str);
                return 0;
        }
    }
    else if(end_index - begin_index == 1) {
        // 一元运算符
        switch(tokens[begin_index].type) {
            case TK_POS:   return +cal_expr(begin_index + 1, end_index);
            case TK_NEG:   return -cal_expr(begin_index + 1, end_index);
            case TK_DEREF: return vaddr_read(cal_expr(begin_index + 1, end_index), 8);

            default:
                printf("Expected operator at token index: %d", begin_index);
                break;
        }
    }
    else {
        // 寻找有效的主运算符
        int major_op_pos = find_major_operator(begin_index, end_index);
        assert(major_op_pos != -1);
        // --- 递归调用，计算子表达式
        int64_t val_1 = cal_expr(begin_index, major_op_pos - 1);
        int64_t val_2 = cal_expr(major_op_pos + 1, end_index);
        // --- 进行主运算符的计算
        token_enum_t op_type = tokens[major_op_pos].type;
        switch (op_type) {
            case TK_ADD:     return val_1 +  val_2;
            case TK_MINUS:   return val_1 -  val_2;
            case TK_MUL:     return val_1 *  val_2;
            case TK_DIV:     return val_1 /  val_2;
            case TK_BOOL_EQ: return val_1 == val_2;
            case TK_BOOL_GT: return val_1 >  val_2;
            case TK_BOOL_LT: return val_1 <  val_2;
            case TK_BOOL_GE: return val_1 >= val_2;
            case TK_BOOL_LE: return val_1 <= val_2;
            default: assert(0); // Should never reach here
        }
    }
    assert(0); // Should never reach here
}

word_t expr(char *e, bool *success) {
    // 判断解析token是否成功，如果不成功，则返回
    if (!make_token(e)) {
        *success = false;
        return 0;
    }

    // 进行表达式求值
    return cal_expr(0, num_of_token - 1);
}
