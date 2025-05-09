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
 * Description: AMQP protocol matcher
 ******************************************************************************/
#ifndef __AMQP_MATCHER_H__
#define __AMQP_MATCHER_H__

#include "../model/amqp_msg_format.h"
#include "data_stream.h"

/**
 * Match AMQP frames to create complete request-response records
 *
 * @param req_frames Request frames buffer
 * @param resp_frames Response frames buffer
 * @param record_buf Output record buffer
 */
void amqp_match_frames(struct frame_buf_s *req_frames, struct frame_buf_s *resp_frames, struct record_buf_s *record_buf);

#endif // __AMQP_MATCHER_H__ 