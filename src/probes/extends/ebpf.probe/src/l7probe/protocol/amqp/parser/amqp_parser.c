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
 * Description: AMQP protocol parser implementation
 ******************************************************************************/
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include "common.h"
#include "amqp_parser.h"

// AMQP 0-9-1 帧结束标记
#define AMQP_FRAME_END 0xCE

// AMQP 最小帧大小
#define AMQP_MIN_FRAME_SIZE 8  // 1 (type) + 2 (channel) + 4 (size) + 1 (end)

// AMQP 协议头: "AMQP"
static const char AMQP_PROTOCOL_HEADER[] = {'A', 'M', 'Q', 'P'};

/**
 * 检查是否为 AMQP 协议头
 */
static bool is_amqp_protocol_header(const char *data, size_t len)
{
    if (len < AMQP_PROTOCOL_HEADER_LEN) {
        return false;
    }
    
    return (memcmp(data, AMQP_PROTOCOL_HEADER, 4) == 0);
}

/**
 * 获取 AMQP 0-9-1 帧长度
 */
static size_t get_amqp_091_frame_len(const char *data, size_t len)
{
    if (len < 7) {
        return 0;
    }
    
    // 提取帧大小 (4字节, 大端)
    uint32_t frame_size = ntohl(*(uint32_t *)(data + 3));
    
    // 计算总帧大小: 帧头(7) + 内容(frame_size) + 帧结束标记(1)
    return 7 + frame_size + 1;
}

/**
 * 解析 AMQP 0-9-1 协议头
 */
static bool parse_amqp_protocol_header(const char *data, size_t len, amqp_message *msg)
{
    if (len < AMQP_PROTOCOL_HEADER_LEN) {
        return false;
    }
    
    // 检查协议签名 "AMQP"
    if (!is_amqp_protocol_header(data, len)) {
        return false;
    }
    
    // 提取协议版本信息
    msg->version = AMQP_0_9_1;  // 我们只支持 0-9-1 版本
    
    return true;
}

/**
 * 解析 AMQP 0-9-1 方法帧
 */
static bool parse_amqp_091_method_frame(const char *data, size_t len, amqp_message *msg)
{
    if (len < 12) {  // 帧头(7) + 类ID(2) + 方法ID(2) + 帧结束标记(1)
        return false;
    }
    
    // 跳过帧头(7字节)
    const char *payload = data + 7;
    
    // 提取类ID和方法ID
    msg->class_id = ntohs(*(uint16_t *)payload);
    msg->method_id = ntohs(*(uint16_t *)(payload + 2));
    
    // 处理特定方法的参数
    switch (msg->class_id) {
        case AMQP_0_9_CLASS_CONNECTION:
            switch (msg->method_id) {
                case AMQP_0_9_METHOD_CONNECTION_TUNE:
                    if (len < 19) {  // 帧头(7) + 类ID(2) + 方法ID(2) + channel_max(2) + frame_max(4) + heartbeat(2) + 帧结束标记(1)
                        return false;
                    }
                    // 提取 tune 参数
                    msg->connection.channel_max = ntohs(*(uint16_t *)(payload + 4));
                    msg->connection.frame_max = ntohl(*(uint32_t *)(payload + 6));
                    msg->connection.heartbeat = ntohs(*(uint16_t *)(payload + 10));
                    break;
                
                case AMQP_0_9_METHOD_CONNECTION_OPEN:
                    if (len < 13) {  // 帧头(7) + 类ID(2) + 方法ID(2) + vhost_len(1) + 帧结束标记(1)
                        return false;
                    }
                    // 提取 virtual host (短字符串: 1字节长度 + 内容)
                    uint8_t vhost_len = *(uint8_t *)(payload + 4);
                    if (len < 13 + vhost_len) {
                        return false;
                    }
                    msg->connection.vhost = (char *)malloc(vhost_len + 1);
                    if (msg->connection.vhost != NULL) {
                        memcpy(msg->connection.vhost, payload + 5, vhost_len);
                        msg->connection.vhost[vhost_len] = '\0';
                    }
                    break;
            }
            break;
            
        case AMQP_0_9_CLASS_BASIC:
            switch (msg->method_id) {
                case AMQP_0_9_METHOD_BASIC_PUBLISH:
                    if (len < 13) {  // 基本开销 + 交换机长度(至少1)
                        return false;
                    }
                    
                    // 获取交换机名称长度
                    uint8_t exchange_len = *(uint8_t *)(payload + 6);
                    if (len < 14 + exchange_len) {
                        return false;
                    }
                    
                    // 提取交换机名称
                    msg->basic.exchange = (char *)malloc(exchange_len + 1);
                    if (msg->basic.exchange != NULL) {
                        memcpy(msg->basic.exchange, payload + 7, exchange_len);
                        msg->basic.exchange[exchange_len] = '\0';
                    }
                    
                    // 获取路由键长度
                    uint8_t routing_key_len = *(uint8_t *)(payload + 7 + exchange_len);
                    if (len < 15 + exchange_len + routing_key_len) {
                        return false;
                    }
                    
                    // 提取路由键
                    msg->basic.routing_key = (char *)malloc(routing_key_len + 1);
                    if (msg->basic.routing_key != NULL) {
                        memcpy(msg->basic.routing_key, payload + 8 + exchange_len, routing_key_len);
                        msg->basic.routing_key[routing_key_len] = '\0';
                    }
                    
                    // 提取标志位
                    uint8_t flags = *(uint8_t *)(payload + 8 + exchange_len + routing_key_len);
                    msg->basic.mandatory = (flags & 0x01) != 0;
                    msg->basic.immediate = (flags & 0x02) != 0;
                    
                    // 设置传递信息用于后续匹配
                    msg->delivery_channel = msg->channel;
                    break;
                    
                case AMQP_0_9_METHOD_BASIC_DELIVER:
                    if (len < 21) {  // 基本开销 + 其他字段
                        return false;
                    }
                    
                    // 设置传递标签
                    msg->basic.delivery_tag = ntohll(*(uint64_t *)(payload + 8));
                    
                    // 设置传递信息用于后续匹配
                    msg->delivery_channel = msg->channel;
                    msg->delivery_tag = msg->basic.delivery_tag;
                    break;
                    
                case AMQP_0_9_METHOD_BASIC_ACK:
                    if (len < 17) {  // 帧头(7) + 类ID(2) + 方法ID(2) + delivery_tag(8) + 帧结束标记(1)
                        return false;
                    }
                    
                    // 设置传递标签
                    msg->basic.delivery_tag = ntohll(*(uint64_t *)(payload + 4));
                    
                    // 设置传递信息用于后续匹配
                    msg->delivery_channel = msg->channel;
                    msg->delivery_tag = msg->basic.delivery_tag;
                    break;
            }
            break;
    }
    
    return true;
}

/**
 * 解析 AMQP 0-9-1 内容头帧
 */
static bool parse_amqp_091_content_header_frame(const char *data, size_t len, amqp_message *msg)
{
    if (len < 14) {  // 帧头(7) + 类ID(2) + 权重(2) + body_size(8) + 帧结束标记(1)
        return false;
    }
    
    // 跳过帧头(7字节)
    const char *payload = data + 7;
    
    // 提取类ID
    msg->class_id = ntohs(*(uint16_t *)payload);
    
    // 提取消息体大小
    msg->content.body_size = ntohll(*(uint64_t *)(payload + 4));
    
    // TODO: 解析内容头属性标志和字段
    
    return true;
}

/**
 * 解析 AMQP 0-9-1 内容体帧
 */
static bool parse_amqp_091_content_body_frame(const char *data, size_t len, amqp_message *msg)
{
    // 内容体帧不需要额外解析，因为我们不关心具体内容
    // 只需验证帧格式正确性
    
    if (len < 8) {  // 帧头(7) + 帧结束标记(1)，内容可以为空
        return false;
    }
    
    return true;
}

/**
 * 解析 AMQP 0-9-1 心跳帧
 */
static bool parse_amqp_091_heartbeat_frame(const char *data, size_t len, amqp_message *msg)
{
    // 心跳帧没有内容，长度固定为 8 字节
    if (len != 8) {  // 帧头(7) + 帧结束标记(1)
        return false;
    }
    
    return true;
}

/**
 * 解析 AMQP 0-9-1 帧
 */
static bool parse_amqp_091_frame(const char *data, size_t len, amqp_message *msg)
{
    if (len < AMQP_MIN_FRAME_SIZE) {
        return false;
    }
    
    // 检查帧结束标记
    if (data[len - 1] != AMQP_FRAME_END) {
        return false;
    }
    
    // 提取帧类型、通道号和帧大小
    msg->frame_type = *(uint8_t *)data;
    msg->channel = ntohs(*(uint16_t *)(data + 1));
    msg->frame_size = ntohl(*(uint32_t *)(data + 3));
    
    // 验证帧大小
    if (7 + msg->frame_size + 1 != len) {
        return false;
    }
    
    // 根据帧类型进行解析
    switch (msg->frame_type) {
        case AMQP_0_9_FRAME_TYPE_METHOD:
            return parse_amqp_091_method_frame(data, len, msg);
            
        case AMQP_0_9_FRAME_TYPE_CONTENT_HEADER:
            return parse_amqp_091_content_header_frame(data, len, msg);
            
        case AMQP_0_9_FRAME_TYPE_CONTENT_BODY:
            return parse_amqp_091_content_body_frame(data, len, msg);
            
        case AMQP_0_9_FRAME_TYPE_HEARTBEAT:
            return parse_amqp_091_heartbeat_frame(data, len, msg);
            
        default:
            // 未知帧类型
            return false;
    }
}

/**
 * 解析 AMQP 帧
 */
static bool parse_amqp_frame(const char *data, size_t len, amqp_message *msg)
{
    // 检查是否为协议头
    if (is_amqp_protocol_header(data, len)) {
        return parse_amqp_protocol_header(data, len, msg);
    }
    
    // 目前只支持 AMQP 0-9-1 版本
    return parse_amqp_091_frame(data, len, msg);
}

size_t amqp_find_frame_boundary(struct raw_data_s *raw_data)
{
    if (raw_data == NULL || raw_data->data == NULL || raw_data->data_len == 0) {
        return 0;
    }

    const char *data = raw_data->data + raw_data->pos;
    size_t remaining = raw_data->data_len - raw_data->pos;

    // 检查是否为 AMQP 协议头
    if (is_amqp_protocol_header(data, remaining)) {
        // 协议头长度固定为 8 字节
        return (remaining >= AMQP_PROTOCOL_HEADER_LEN) ? AMQP_PROTOCOL_HEADER_LEN : 0;
    }

    // 检查是否为 AMQP 0-9-1 帧
    if (remaining >= 7) {  // 至少需要帧头(7字节)
        size_t frame_len = get_amqp_091_frame_len(data, remaining);
        
        // 检查边界并验证帧结束标记
        if (frame_len > 0 && remaining >= frame_len && data[frame_len - 1] == AMQP_FRAME_END) {
            return frame_len;
        }
    }

    return 0;  // 未找到有效的帧边界
}

parse_state_t amqp_parse_frame(enum message_type_t msg_type, struct raw_data_s *raw_data, struct frame_data_s **frame_data)
{
    if (raw_data == NULL || raw_data->data == NULL || frame_data == NULL) {
        return STATE_ERROR;
    }

    // 查找帧边界
    size_t frame_len = amqp_find_frame_boundary(raw_data);
    if (frame_len == 0) {
        return STATE_CONTINUE;
    }

    // 分配帧数据结构
    *frame_data = (struct frame_data_s *)malloc(sizeof(struct frame_data_s));
    if (*frame_data == NULL) {
        ERROR("[AMQP] Failed to malloc frame_data.\n");
        return STATE_ERROR;
    }
    memset(*frame_data, 0, sizeof(struct frame_data_s));

    // 创建并初始化消息结构
    amqp_message *msg = init_amqp_msg();
    if (msg == NULL) {
        free(*frame_data);
        *frame_data = NULL;
        return STATE_ERROR;
    }

    // 设置消息类型和时间戳
    msg->type = msg_type;
    msg->timestamp_ns = raw_data->timestamp_ns;

    // 解析帧内容
    const char *data = raw_data->data + raw_data->pos;
    if (!parse_amqp_frame(data, frame_len, msg)) {
        DEBUG("[AMQP] Failed to parse AMQP frame.\n");
        free_amqp_msg(msg);
        free(*frame_data);
        *frame_data = NULL;
        return STATE_ERROR;
    }

    // 更新 raw_data 位置
    raw_data->pos += frame_len;

    // 设置返回值
    (*frame_data)->frame = msg;
    (*frame_data)->timestamp = msg->timestamp_ns;

    return STATE_SUCCESS;
} 