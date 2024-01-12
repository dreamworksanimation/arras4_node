// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS4_SERVICE_ERROR_H__
#define __ARRAS4_SERVICE_ERROR_H__

#include <exception>
#include <stddef.h>
#include <string>

namespace arras4 {
namespace node {

class ServiceError : public std::exception
{
public:
    ServiceError(const std::string& aDetail) : mDetail(aDetail) {}
    ~ServiceError() noexcept override {}
    const char* what() const noexcept override { return mDetail.c_str(); }
protected:
    std::string mDetail;
};

}
}
#endif
