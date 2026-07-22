/*
* Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
* kraio is licensed under the Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*     http://license.coscl.org.cn/MulanPSL2
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
* PURPOSE.
* See the Mulan PSL v2 for more details.
*
*/

#ifndef KRAIO_H
#define KRAIO_H
#include <stddef.h>
#include <stdarg.h>
#include <sys/uio.h>

size_t write_async(int fd, const void *buf, size_t count, void *para1, void *para2, void *para3);
size_t writev_async(int fd, const void *buf, size_t count, void *para1, void *para2, void *para3);
typedef void (*write_callback_t)(int ret, void *buf, int len, void *para1, void *para2, void *para3);
typedef void (*writev_callback_t)(int ret, struct iovec *iov, int iovcnt, void *para1, void *para2, void *para3);
void register_write_callback(write_callback_t cb);
void register_writev_callback(writev_callback_t cb);

typedef void (*redis_log_t)(int level, const char *fmt, va_list ap);
void register_logfunc(redis_log_t fn);

#endif // KRAIO_H