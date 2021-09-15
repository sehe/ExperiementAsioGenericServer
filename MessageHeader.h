#pragma once
#include "prerequisites.h"

template <typename MsgId> struct MessageHeader {
    MsgId    id{};
    uint32_t size = 0;
};
