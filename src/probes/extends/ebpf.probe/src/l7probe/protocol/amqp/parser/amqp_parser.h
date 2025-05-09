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
 * Description: AMQP protocol parser
 ******************************************************************************/
#ifndef __AMQP_PARSER_H__
#define __AMQP_PARSER_H__

#pragma once

#include "common/protocol_common.h"
#include "../model/amqp_msg_format.h"
#include "l7.h"

#define AMQP_PROTOCOL_HEADER_LEN 8 // "AMQP" + 0 + Major + Minor + Revision

/**
 * Find the AMQP frame boundary in raw data
 *
 * @param raw_data Raw data buffer
 * @return Size of the frame if found, 0 otherwise
 */
size_t amqp_find_frame_boundary(struct raw_data_s *raw_data);

/**
 * Parse an AMQP frame from raw data
 *
 * @param msg_type Message type (request/response)
 * @param raw_data Raw data buffer
 * @param frame_data Output frame data
 * @return Parse state (success/error)
 */
parse_state_t amqp_parse_frame(enum message_type_t msg_type, 
                               struct raw_data_s *raw_data,
                               struct frame_data_s **frame_data);

#endif // __AMQP_PARSER_H__ 