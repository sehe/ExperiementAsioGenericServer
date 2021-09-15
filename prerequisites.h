#pragma once
//#define VERBOSE_SERVER_DEBUG
// Windows stuff.
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
// boost asio

#include <algorithm>
#include <boost/asio.hpp>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <execution>
#include <functional>
#include <future>
#include <iostream>
#include <list>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
    #include <ShlObj.h>
    #include <Shlwapi.h>

#endif
#include <iostream>
#include <ostream>
#include <sstream>

#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <queue>
#include <random>
#include <stdexcept>
#include <utility>

#include "boost/date_time/posix_time/posix_time_types.hpp"
#include <boost/bind/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/function.hpp>
#include <boost/functional.hpp>
#include <boost/make_shared.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread/mutex.hpp>

#include <boost/signals2.hpp>
#include <boost/thread.hpp>

// Function pointer called CallbackType that takes a float
// and returns an int
// typedef int (*CallbackType)(float);
template <typename T, typename Executor> class Connection;
template <typename T> struct Message;

#include "MessageHeader.h"
#include "Message.h"
#include "ThreadSafeQueue.h"

using boost::asio::ip::tcp;
using boost::system::error_code;
using namespace std::chrono_literals;
using Clock = std::chrono::high_resolution_clock;

#include "Connection.h"
#include "Server.h"
