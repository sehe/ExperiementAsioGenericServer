#pragma once
//#define VERBOSE_SERVER_DEBUG
#include <chrono>
#include <deque>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio.hpp>

#ifdef _WIN32
    // Windows stuff.
    #define _CRT_SECURE_NO_WARNINGS
    #define NOMINMAX
    #include <ShlObj.h>
    #include <Shlwapi.h>
#endif

#include <boost/bind/bind.hpp>
#include <boost/signals2.hpp>

// Function pointer called CallbackType that takes a float
// and returns an int
// typedef int (*CallbackType)(float);
template <typename T, typename Executor> class Connection;
template <typename T> struct Message;

#include "MessageHeader.h"
#include "Message.h"

using boost::asio::ip::tcp;
using boost::system::error_code;
using namespace std::chrono_literals;
using Clock    = std::chrono::high_resolution_clock;
using Executor = boost::asio::thread_pool::executor_type;
using Timer    = boost::asio::basic_waitable_timer<Clock>;

enum class MessageTypes : uint32_t {
    ServerAccept,
    ServerDeny,
    ServerPing,
    MessageAll,
    SendText,
    ServerMessage,
    ServerMessage1,
    ServerMessage2,
    ServerMessage3,
    ServerMessage4,
    ServerMessage5,
    ServerMessage6,
    ServerMessage7,
    ServerMessage8,
    ServerMessage9,
};

#include "Connection.h"
#include "Server.h"
#include "Client.h"
