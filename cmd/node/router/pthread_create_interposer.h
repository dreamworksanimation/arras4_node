// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef ARRAS_PTHREAD_CREATE_INTERPOSER_H_
#define ARRAS_PTHREAD_CREATE_INTERPOSER_H_

// std::thread doesn't provide a mechanism for controlling the stack size of
// thread. Too much virtual memory gets used with the default stack size. The
// pthread_create interposer allows the stack size to be set by calling
// set_thread_stacksize() before creating the child thread. The stack size
// setting is stored in thread local memory so set_thread_stacksize() needs
// to be called in the same thread. Setting it to zero will use the default
// stack size.

#include <pthread.h>

namespace arras4 {
namespace node {

int constexpr KB_256 = 256 * 1024;

void set_thread_stacksize(size_t size);
size_t get_thread_stacksize();

} // end namespace service
} // end namespace arras

#endif // ARRAS_PTHREAD_CREATE_INTERPOSER_H_

