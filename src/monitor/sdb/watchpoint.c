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

#include "sdb.h"

#define NR_WP 32

typedef struct watchpoint {
    int id;
    struct watchpoint *next, *prev;

    /* TODO: Add more members if necessary */
    word_t last_val;   // 上一次check时，表达式的数值
    char expr[100];    // 表达式
    int counter;       // 当前watchpoint被check过几次
} watchpoint_t;

// 可用的watchpoint_t列表
static watchpoint_t wp_pool[NR_WP] = {};

static watchpoint_t *head = NULL;   // 表示已经分配的watchpoint_t链表的头节点
static watchpoint_t *free_ = NULL;  // 表示空余的watchpoint_t链表的头节点

void init_wp_pool() {
//  int i;
//  for (i = 0; i < NR_WP; i ++) {
//    wp_pool[i].NO = i;
//    wp_pool[i].next = (i == NR_WP - 1 ? NULL : &wp_pool[i + 1]);
//  }
//
//  head = NULL;
//  free_ = wp_pool;

    static watchpoint_t wp_dummy[2];    // 为两个链表的头节点开辟的空间
    head = &wp_dummy[0];
    free_ = &wp_dummy[1];
    // 初始化watchpoint_t的池，将它们串成链表
    for (int i = 0; i < NR_WP; i++) {
        wp_pool[i].id = i;
        wp_pool[i].next = &wp_pool[i + 1];
        wp_pool[i].prev = &wp_pool[i - 1];
    }
    // head的id为-1 free_的id为-2
    for (int i = 0; i < 2; i++) {
        wp_dummy[i].id = -i - 1;
    }
    // 将链表初始化为双向环形链表，free_作为头节点
    wp_pool[NR_WP - 1].next = free_;
    wp_pool[0].prev = free_;
    free_->next = &wp_pool[0];
    free_->prev = &wp_pool[NR_WP - 1];

    head->next = head;
    head->prev = head;
}

/* TODO: Implement the functionality of watchpoint */

/**
 * 从wp_pool中申请一个watchpoint_t
 * @return 申请到的watchpoint_t的指针
 */
watchpoint_t *new_wp() {
    if (free_->next->id == -2) {
        // 头节点的下一个节点是它本身，表示池子里没有可用的watchpoint_t了
        assert(0);
    } else {
        // 从wp_pool链表中拿出一个节点
        watchpoint_t *res = free_->next;

        free_->next = res->next;
        free_->next->prev = free_;

        head->next->prev = res;
        res->prev = head;
        res->next = head->next;
        head->next = res;

        res->counter = 0;
        return res;
    }
}

/**
 * 将watchpoint_t放回free链表
 * @param n 要放回的链表的id
 * @return
 *        - -1: 不合法的下标
 *        -  0: 释放成功
 */
int free_wp(int n) {
    if (n < 0 || n >= NR_WP) {
        return -1;
    }
    watchpoint_t *wp = &wp_pool[n];

    wp->prev->next = wp->next;
    wp->next->prev = wp->prev;

    free_->next->prev = wp;
    wp->prev = free_;
    wp->next = free_->next;
    free_->next = wp;
    return 0;
}

/**
 * 检查watchpoint中的表达式数值是否改变
 * @return
 *          - 0: 没有表达式的值发生变化
 *          - 1: 有表达式的数值发生变化
 */
int check_wp() {
    watchpoint_t *cur = head;
    int res = 0;
    // 遍历当前已经分配的watchpoint_t
    while (cur->next->id != -1) {
        // ？？？
        cur = cur->next;
        if (cur->counter == 0) {
            cur->counter++;
            continue;
        }
        cur->counter++;
        // 重新计算当前watchpoint的表达式的数值
        bool success;
        word_t val = expr(cur->expr, &success);
        if (success == false) {
            assert(0);
        } else {
            // printf_debug("watchpoint #%d:\t%s = %d -> %d\n", cur->NO, cur->expr, cur->last_val, val);
            if (val != cur->last_val) {
                printf("watchpoint #%d\t%s:\t%d\t->\t%d\n", cur->id,
                       cur->expr, cur->last_val, val);
                res = 1;
            }
            cur->last_val = val;
        }
    }

    return res;
}

/**
 * 打印所有的watchpoint
 */
void print_wp() {
    watchpoint_t *cur = head;
    printf("Watchpoint #WP_ID\tWP_EXPR:\tWP_VAL\n");
    while (cur->prev->id != -1) {
        cur = cur->prev;
        printf("Watchpoint #%d\t%s:\t%lu\n", cur->id, cur->expr, cur->last_val);
    }
    cur = head;
}

void wp_watch(char *expr, word_t res) {
    watchpoint_t* wp = new_wp();
    strcpy(wp->expr, expr);
    wp->last_val = res;
    printf("Watchpoint %d: %s\n", wp->id, expr);
}