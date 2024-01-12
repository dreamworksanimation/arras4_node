// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS4_SESSION_ERROR_H__
#define __ARRAS4_SESSION_ERROR_H__

#include "OperationError.h"

namespace arras4 {
namespace node {

class SessionError : public OperationError
{
public:
    SessionError(const std::string& aDetail,int httpCode=500) : OperationError(aDetail,httpCode) {}
};

}
}
#endif
