// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

// This is the implementation for node/router/pthread_create_interposer.h,
// but it must be included at the start of the main program file in order
// to ensure the interposing works...

#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <cstdlib> // std::_Exit

#include <node/router/pthread_create_interposer.h>

namespace arras4 {
namespace node {

__thread size_t thread_stacksize = 0;

void
set_thread_stacksize(size_t size)
{
    thread_stacksize = size;
}

size_t
get_thread_stacksize()
{
    return thread_stacksize;
}

typedef int (*pthread_createPtr)(pthread_t* thread, const pthread_attr_t* attr, void *(*start_routine)(void*), void* aArg);
pthread_createPtr realpthread_create = NULL;

} // end namespace node
} // end namespace arras4

extern "C" {

int
pthread_create(pthread_t *aThread, const pthread_attr_t *aAttr, void *(*aStart_routine) (void *), void *aArg)
{
    if (arras4::node::realpthread_create == NULL) {
// disable icc warning "assignment to statically allocated variable"
#if defined(__ICC)
        __pragma(warning(disable:1711)) 
#endif
        arras4::node::realpthread_create = (arras4::node::pthread_createPtr)dlsym(RTLD_NEXT,"pthread_create");
#if defined(__ICC)
        __pragma(warning(default:1711)) 
#endif
        if (arras4::node::realpthread_create == NULL) {
            fprintf(stderr,"Couldn't get the symbol for real pthread_create()\n");
            std::_Exit(1);
        }
    }

    int value;
    if ((aAttr == NULL) && (arras4::node::thread_stacksize > 0)) {
        pthread_attr_t localAttr;
        pthread_attr_init(&localAttr);
        pthread_attr_setstacksize(&localAttr, arras4::node::thread_stacksize);
        value = (*arras4::node::realpthread_create)(aThread, &localAttr, aStart_routine, aArg);
        pthread_attr_destroy(&localAttr);
    } else {
        value = (*arras4::node::realpthread_create)(aThread, aAttr, aStart_routine, aArg);
    }

    return value;
}

} // end extern "C"

