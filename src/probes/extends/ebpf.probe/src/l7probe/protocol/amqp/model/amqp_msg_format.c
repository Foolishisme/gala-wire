/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 * gala-gopher licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: sonic
 * Create: 2023-08-08
 * Description: AMQP protocol message format implementation
 ******************************************************************************/

#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "amqp_msg_format.h"

amqp_message *init_amqp_msg(void)
{
    amqp_message *msg = (amqp_message *)malloc(sizeof(amqp_message));
    if (msg == NULL) {
        ERROR("[AMQP] Failed to allocate memory for message.\n");
        return NULL;
    }
    memset(msg, 0, sizeof(amqp_message));
    msg->type = MESSAGE_UNKNOW;
    msg->version = AMQP_0_9_1;  // 默认为 AMQP 0-9-1 版本
    
    return msg;
}

void free_amqp_msg(amqp_message *msg)
{
    if (msg == NULL) {
        return;
    }
    
    // 释放 connection 字段
    if (msg->connection.vhost != NULL) {
        free(msg->connection.vhost);
    }
    
    // 释放 basic 字段
    if (msg->basic.exchange != NULL) {
        free(msg->basic.exchange);
    }
    if (msg->basic.routing_key != NULL) {
        free(msg->basic.routing_key);
    }
    
    // 释放 content 字段
    if (msg->content.content_type != NULL) {
        free(msg->content.content_type);
    }
    if (msg->content.content_encoding != NULL) {
        free(msg->content.content_encoding);
    }
    
    free(msg);
}

amqp_record *init_amqp_record(void)
{
    amqp_record *record = (amqp_record *)malloc(sizeof(amqp_record));
    if (record == NULL) {
        ERROR("[AMQP] Failed to allocate memory for record.\n");
        return NULL;
    }
    memset(record, 0, sizeof(amqp_record));
    return record;
}

void free_amqp_record(amqp_record *record)
{
    if (record == NULL) {
        return;
    }
    
    // NOTE: 记录中的 req/resp 指针复用了 frame_buf 中的 req/resp 指针，因此我们不在这里释放
    free(record);
} 