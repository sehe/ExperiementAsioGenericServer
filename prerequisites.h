#pragma once
// Windows stuff.
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
//boost asio

#include <boost/asio.hpp>
#include <ctime>
#include <cstring>
#include <chrono>
#include <time.h>
#include <sstream>
#include <string>
#include <vector>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include <iostream>
#include <thread>
#include <future>
#include <algorithm>
#include <cstdlib>
#include <list>
#include <set>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <execution>
#ifdef _WIN32
#include <ShlObj.h>
#include <Shlwapi.h>

#endif
#include <iostream>
#include <ostream>
#include <sstream>


#include <errno.h>
#include <stdint.h>
#include <fstream>
#include <stdexcept>
#include <math.h>
#include <random>
#include <climits>
#include <memory>
#include <utility>
#include <queue>
#include <optional>

#include <boost/bind/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/make_shared.hpp>
#include "boost/date_time/posix_time/posix_time_types.hpp"
#include <boost/thread/mutex.hpp>
#include <boost/functional.hpp>
#include <boost/function.hpp>

#include <boost/thread.hpp>
#include <boost/signals2.hpp>


//Function pointer called CallbackType that takes a float
//and returns an int
//typedef int (*CallbackType)(float);
template<typename T>
class Connection;

template<typename T>
struct Message;

template<typename T>
struct OwnedMessage;



//template<typename T>
//using ClientDisconnectCallbackType = void (*)(boost::shared_ptr<Connection<T>>);
template<typename T>
using ClientDisconnectCallbackType = boost::function<void(boost::shared_ptr<Connection<T>>)>;

template<typename T>
using ClientMessageCallbackType = boost::function<void(OwnedMessage<T>&)>;

template<typename T>
using ClientMessageSentCallbackType = boost::function<void(OwnedMessage<T>*)>;

#include "MessageHeader.h"
#include "Message.h"
#include "OwnedMessage.h"
#include "ThreadSafeQueue.h"
#include "Connection.h"
#include "Server.h"


