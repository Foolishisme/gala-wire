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
 * Description: AMQP protocol message format
 ******************************************************************************/
#ifndef __AMQP_MSG_FORMAT_H__
#define __AMQP_MSG_FORMAT_H__

#pragma once

#include "data_stream.h"

/* AMQP 0-9-1 version defines */
#define AMQP_0_9_1           2

/* AMQP 0-9-1 Frame types */
#define AMQP_0_9_FRAME_TYPE_METHOD                1
#define AMQP_0_9_FRAME_TYPE_CONTENT_HEADER        2
#define AMQP_0_9_FRAME_TYPE_CONTENT_BODY          3
#define AMQP_0_9_FRAME_TYPE_HEARTBEAT             8

/* AMQP 0-9-1 Classes */
#define AMQP_0_9_CLASS_CONNECTION                10
#define AMQP_0_9_CLASS_CHANNEL                   20
#define AMQP_0_9_CLASS_EXCHANGE                  40
#define AMQP_0_9_CLASS_QUEUE                     50
#define AMQP_0_9_CLASS_BASIC                     60
#define AMQP_0_9_CLASS_TX                        90
#define AMQP_0_9_CLASS_CONFIRM                   85

/* AMQP 0-9-1 Connection methods */
#define AMQP_0_9_METHOD_CONNECTION_START          10
#define AMQP_0_9_METHOD_CONNECTION_START_OK       11
#define AMQP_0_9_METHOD_CONNECTION_SECURE         20
#define AMQP_0_9_METHOD_CONNECTION_SECURE_OK      21
#define AMQP_0_9_METHOD_CONNECTION_TUNE           30
#define AMQP_0_9_METHOD_CONNECTION_TUNE_OK        31
#define AMQP_0_9_METHOD_CONNECTION_OPEN           40
#define AMQP_0_9_METHOD_CONNECTION_OPEN_OK        41
#define AMQP_0_9_METHOD_CONNECTION_CLOSE          50
#define AMQP_0_9_METHOD_CONNECTION_CLOSE_OK       51

/* AMQP 0-9-1 Basic methods */
#define AMQP_0_9_METHOD_BASIC_QOS                 10
#define AMQP_0_9_METHOD_BASIC_QOS_OK              11
#define AMQP_0_9_METHOD_BASIC_CONSUME             20
#define AMQP_0_9_METHOD_BASIC_CONSUME_OK          21
#define AMQP_0_9_METHOD_BASIC_CANCEL              30
#define AMQP_0_9_METHOD_BASIC_CANCEL_OK           31
#define AMQP_0_9_METHOD_BASIC_PUBLISH             40
#define AMQP_0_9_METHOD_BASIC_RETURN              50
#define AMQP_0_9_METHOD_BASIC_DELIVER             60
#define AMQP_0_9_METHOD_BASIC_GET                 70
#define AMQP_0_9_METHOD_BASIC_GET_OK              71
#define AMQP_0_9_METHOD_BASIC_GET_EMPTY           72
#define AMQP_0_9_METHOD_BASIC_ACK                 80
#define AMQP_0_9_METHOD_BASIC_REJECT              90
#define AMQP_0_9_METHOD_BASIC_RECOVER_ASYNC      100
#define AMQP_0_9_METHOD_BASIC_RECOVER            110
#define AMQP_0_9_METHOD_BASIC_RECOVER_OK         111
#define AMQP_0_9_METHOD_BASIC_NACK               120

/**
 * AMQP message structure
 */
typedef struct amqp_message {
    enum message_type_t type;      // 消息类型：请求或响应
    u64 timestamp_ns;              // 消息时间戳
    
    // AMQP 版本
    u8 version;                    // AMQP_0_9_1
    
    // AMQP 帧信息
    u8 frame_type;                 // 帧类型
    u16 channel;                   // 通道号
    u32 frame_size;                // 帧大小
    
    // 方法帧信息
    u16 class_id;                  // 类别ID
    u16 method_id;                 // 方法ID
    
    // Connection 方法关键字段
    struct {
        u16 channel_max;           // 最大通道数量
        u32 frame_max;             // 最大帧大小
        u16 heartbeat;             // 心跳间隔
        char *vhost;               // 虚拟主机名
    } connection;
    
    // Basic 方法关键字段
    struct {
        char *exchange;            // 交换机名称
        char *routing_key;         // 路由键
        u64 delivery_tag;          // 传递标签
        bool mandatory;            // 强制标志
        bool immediate;            // 立即标志
    } basic;
    
    // 内容帧信息
    struct {
        char *content_type;        // 内容类型
        char *content_encoding;    // 内容编码
        u64 body_size;             // 消息体大小
    } content;
    
    // 用于匹配请求和响应
    u16 delivery_channel;          // 传递通道
    u64 delivery_tag;              // 传递标签
} amqp_message;

amqp_message *init_amqp_msg(void);
void free_amqp_msg(amqp_message *msg);

/**
 * AMQP record structure, contains request and response
 */
typedef struct amqp_record {
    amqp_message *req;             // 请求消息
    amqp_message *resp;            // 响应消息
} amqp_record;

amqp_record *init_amqp_record(void);
void free_amqp_record(amqp_record *record);

#endif // __AMQP_MSG_FORMAT_H__ 