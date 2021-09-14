#pragma once
#include "prerequisites.h"
template<typename T>
struct MessageHeader
{
	T id{};
	uint32_t size = 0;
};