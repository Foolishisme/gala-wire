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
 * Description: AMQP protocol matcher implementation
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "data_stream.h"
#include "../model/amqp_msg_format.h"
#include "amqp_matcher.h"

// 前向声明
static void calc_l7_api_statistic(struct record_buf_s *record_buf, struct record_data_s *record_data, amqp_record *rcd_cp);

/**
 * 添加 AMQP 记录到记录缓冲区
 */
static void add_amqp_record_into_buf(amqp_record *record, struct record_buf_s *record_buf)
{
    // 副本记录
    amqp_record *rcd_cp;

    if (record_buf->record_buf_size >= RECORD_BUF_SIZE) {
        WARN("[AMQP MATCHER] The record buffer is full.\n");
        return;
    }

    rcd_cp = (amqp_record *)malloc(sizeof(amqp_record));
    if (rcd_cp == NULL) {
        ERROR("[AMQP MATCHER] Failed to malloc amqp_record.\n");
        return;
    }
    rcd_cp->req = record->req;
    rcd_cp->resp = record->resp;

    struct record_data_s *record_data = (struct record_data_s *)malloc(sizeof(struct record_data_s));
    if (record_data == NULL) {
        ERROR("[AMQP MATCHER] Failed to malloc record_data.\n");
        free(rcd_cp);
        return;
    }
    record_data->record = rcd_cp;
    record_data->latency = rcd_cp->resp->timestamp_ns - rcd_cp->req->timestamp_ns;

    // TODO: 更详细的错误处理逻辑
    record_buf->records[record_buf->record_buf_size] = record_data;
    ++record_buf->record_buf_size;

    // 计算API级别指标
    calc_l7_api_statistic(record_buf, record_data, rcd_cp);
}

/**
 * 为 AMQP 计算 API 统计信息
 */
static void calc_l7_api_statistic(struct record_buf_s *record_buf, struct record_data_s *record_data, amqp_record *rcd_cp)
{
    if (record_buf == NULL || record_data == NULL || rcd_cp == NULL) {
        return;
    }
    
    amqp_message *req = rcd_cp->req;
    
    struct api_stats_id stat_id = {0};
    char api_name[MAX_API_LEN] = {0};
    
    // 根据类ID和方法ID构建 API 名称
    if (req->frame_type == AMQP_0_9_FRAME_TYPE_METHOD) {
        switch (req->class_id) {
            case AMQP_0_9_CLASS_CONNECTION:
                switch (req->method_id) {
                    case AMQP_0_9_METHOD_CONNECTION_START:
                        snprintf(api_name, MAX_API_LEN, "Connection.Start");
                        break;
                    case AMQP_0_9_METHOD_CONNECTION_START_OK:
                        snprintf(api_name, MAX_API_LEN, "Connection.StartOk");
                        break;
                    case AMQP_0_9_METHOD_CONNECTION_TUNE:
                        snprintf(api_name, MAX_API_LEN, "Connection.Tune");
                        break;
                    case AMQP_0_9_METHOD_CONNECTION_TUNE_OK:
                        snprintf(api_name, MAX_API_LEN, "Connection.TuneOk");
                        break;
                    case AMQP_0_9_METHOD_CONNECTION_OPEN:
                        snprintf(api_name, MAX_API_LEN, "Connection.Open");
                        break;
                    case AMQP_0_9_METHOD_CONNECTION_OPEN_OK:
                        snprintf(api_name, MAX_API_LEN, "Connection.OpenOk");
                        break;
                    case AMQP_0_9_METHOD_CONNECTION_CLOSE:
                        snprintf(api_name, MAX_API_LEN, "Connection.Close");
                        break;
                    case AMQP_0_9_METHOD_CONNECTION_CLOSE_OK:
                        snprintf(api_name, MAX_API_LEN, "Connection.CloseOk");
                        break;
                    default:
                        snprintf(api_name, MAX_API_LEN, "Connection.Method_%d", req->method_id);
                        break;
                }
                break;
                
            case AMQP_0_9_CLASS_BASIC:
                switch (req->method_id) {
                    case AMQP_0_9_METHOD_BASIC_PUBLISH:
                        // 使用交换机和路由键构建更详细的 API 名称
                        if (req->basic.exchange != NULL && req->basic.routing_key != NULL) {
                            snprintf(api_name, MAX_API_LEN, "Basic.Publish [%s -> %s]", 
                                     req->basic.exchange, req->basic.routing_key);
                        } else {
                            snprintf(api_name, MAX_API_LEN, "Basic.Publish");
                        }
                        break;
                    case AMQP_0_9_METHOD_BASIC_DELIVER:
                        snprintf(api_name, MAX_API_LEN, "Basic.Deliver");
                        break;
                    case AMQP_0_9_METHOD_BASIC_GET:
                        snprintf(api_name, MAX_API_LEN, "Basic.Get");
                        break;
                    case AMQP_0_9_METHOD_BASIC_GET_OK:
                        snprintf(api_name, MAX_API_LEN, "Basic.GetOk");
                        break;
                    case AMQP_0_9_METHOD_BASIC_ACK:
                        snprintf(api_name, MAX_API_LEN, "Basic.Ack");
                        break;
                    default:
                        snprintf(api_name, MAX_API_LEN, "Basic.Method_%d", req->method_id);
                        break;
                }
                break;
                
            default:
                snprintf(api_name, MAX_API_LEN, "Class_%d.Method_%d", req->class_id, req->method_id);
                break;
        }
    } else if (req->frame_type == AMQP_0_9_FRAME_TYPE_CONTENT_HEADER) {
        snprintf(api_name, MAX_API_LEN, "ContentHeader.Class_%d", req->class_id);
    } else if (req->frame_type == AMQP_0_9_FRAME_TYPE_CONTENT_BODY) {
        snprintf(api_name, MAX_API_LEN, "ContentBody");
    } else if (req->frame_type == AMQP_0_9_FRAME_TYPE_HEARTBEAT) {
        snprintf(api_name, MAX_API_LEN, "Heartbeat");
    } else {
        snprintf(api_name, MAX_API_LEN, "Unknown_%d", req->frame_type);
    }
    
    snprintf(stat_id.api, MAX_API_LEN, "%s", api_name);
    
    struct api_stats* api_stats;
    H_FIND(record_buf->api_stats, &stat_id, sizeof(struct api_stats_id), api_stats);
    if (api_stats == NULL) {
        api_stats = create_api_stats(stat_id.api);
        if (api_stats == NULL) {
            return;
        }
        H_ADD_KEYPTR(record_buf->api_stats, &(api_stats->id), sizeof(struct api_stats_id), api_stats);
    }
    
    api_stats->records[api_stats->record_buf_size] = record_data;
    ++api_stats->record_buf_size;
    ++api_stats->req_count;
    ++api_stats->resp_count;
    
    // TODO: 定义 AMQP 错误状态识别逻辑
    // 对于 AMQP 的 Connection.Close 响应，可以根据关闭代码判断是否为错误
    if (rcd_cp->resp->frame_type == AMQP_0_9_FRAME_TYPE_METHOD &&
        rcd_cp->resp->class_id == AMQP_0_9_CLASS_CONNECTION &&
        rcd_cp->resp->method_id == AMQP_0_9_METHOD_CONNECTION_CLOSE) {
        // 这里只是一个简单示例，实际逻辑可能需要进一步完善
        ++api_stats->err_count;
        ++record_buf->err_count;
        
        // 这里假设是服务器错误，也可以根据具体的关闭代码进行判断
        ++api_stats->server_err_count;
        
        api_stats->err_records[api_stats->err_count - 1] = record_data;
    }
}

void amqp_match_frames(struct frame_buf_s *req_frames, struct frame_buf_s *resp_frames, struct record_buf_s *record_buf)
{
    DEBUG("[AMQP MATCHER] Start to match AMQP request and response frames.\n");
    amqp_record record = {0};
    
    // 定义消息占位符，设置时间戳为最大值
    amqp_message placeholder_msg = {0};
    placeholder_msg.timestamp_ns = UINT64_MAX;
    
    // 循环处理，当响应帧缓冲区中有帧时继续匹配
    while (resp_frames->current_pos < resp_frames->frame_buf_size) {
        amqp_message *req_msg = (req_frames->current_pos == req_frames->frame_buf_size) ? &placeholder_msg
                                                                      : (amqp_message *) ((req_frames->frames)[req_frames->current_pos]->frame);
        amqp_message *resp_msg = (resp_frames->current_pos == resp_frames->frame_buf_size) ? &placeholder_msg
                                                                         : (amqp_message *) ((resp_frames->frames)[resp_frames->current_pos]->frame);
        
        // 将请求添加到记录中
        if (req_msg->timestamp_ns < resp_msg->timestamp_ns) {
            DEBUG("[AMQP MATCHER] Add request to record, req.timestamp: %lu, resp.timestamp: %lu\n",
                  req_msg->timestamp_ns, resp_msg->timestamp_ns);
            memset(&record, 0, sizeof(amqp_record));
            record.req = req_msg;
            ++req_frames->current_pos;
            continue;
        }
        
        // 如果记录中没有请求则中断循环
        if (record.req == NULL) {
            DEBUG("[AMQP MATCHER] No request in record, break the cycle.\n");
            break;
        }
        
        // 如果记录中的请求是有效请求，则匹配成功
        if (record.req->timestamp_ns != 0) {
            DEBUG("[AMQP MATCHER] Record->req->timestamp: %lu\n", record.req->timestamp_ns);
            record.resp = resp_msg;
            ++resp_frames->current_pos;
            add_amqp_record_into_buf(&record, record_buf);
            memset(&record, 0, sizeof(amqp_record));
            continue;
        }
        
        // 如果记录中的请求是占位符，则继续循环
        ++resp_frames->current_pos;
    }
    
    record_buf->req_count = req_frames->current_pos;
    record_buf->resp_count = resp_frames->current_pos;
    DEBUG("[AMQP MATCHER] Match finished.\n");
} 