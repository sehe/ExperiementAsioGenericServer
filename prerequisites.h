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
#include <boost/bind/bind.hpp>
using boost::asio::ip::tcp;
using boost::system::error_code;

template <typename T> class Session;

#include "MessageHeader.h"
#include "Message.h"

using namespace std::chrono_literals;
using Clock    = std::chrono::high_resolution_clock;
using Executor = boost::asio::thread_pool::executor_type;
using Strand   = boost::asio::strand<Executor>;
using Timer    = boost::asio::basic_waitable_timer<Clock>;

#ifdef VERBOSE_SERVER_DEBUG
    static inline std::ostream debug{std::cerr.rdbuf()};
#else
    static inline std::ostream debug{nullptr};
#endif
using SessionA = Session<protocol::MyMessage>;

#include "Session.h"
/*
 *struct SessionA : Session<MyMessage> {
 *    using Session<MyMessage>::Session; // inherit constructors
 *
 *    // IDEA: make your factor method here?
 *    std::string additional, state;
 *};
 *
 */
#include "Server.h"
#include "Client.h"
