#pragma once
#include "prerequisites.h"
template <typename T>
class Connection;

template <typename T>
struct OwnedMessage
{

	boost::shared_ptr<Connection<T>> remote = nullptr;
	Message<T> msg;

	// Again, a friendly string maker
	friend std::ostream& operator<<(std::ostream& os, const OwnedMessage<T>& msg)
	{
		os << msg.msg;
		return os;
	}
};