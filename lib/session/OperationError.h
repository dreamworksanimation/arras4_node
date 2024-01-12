// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS4_OPERATION_ERROR_H__
#define __ARRAS4_OPERATION_ERROR_H__

#include <exception>
#include <stddef.h>
#include <string>

namespace arras4 {
namespace node {

class OperationError : public std::exception
{
public:
    OperationError(const std::string& aDetail,
                 int httpCode=500) : mDetail(aDetail), mHttpCode(httpCode) {}
    ~OperationError() noexcept override {}
    const char* what() const noexcept override { return mDetail.c_str(); }
    int httpCode() noexcept { return mHttpCode; }
    const std::string& text() { return mDetail; }
protected:
    std::string mDetail;
    int mHttpCode;
};

}
}
#endif
