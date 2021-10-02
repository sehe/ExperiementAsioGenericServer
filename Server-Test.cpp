// Server-Test.cpp : This file contains the 'main' function. Program execution
// begins and ends there.
#include "prerequisites.h"
#include <utility>
#include <list>

#include "Statistics.h"
static statistics::Accum g_latencies{"Latency", 100};

namespace networking {
    template<typename Factory>
    struct Server {
        using socket_t = listener::socket_t;

        Server(Executor ex, Factory f, std::initializer_list<tcp::endpoint> eps)
            : ex_(ex)
            , sessmgr_(strand_, std::move(f))
        {
            for (auto ep : eps)
                add_endpoint(ep);
        }

        void add_endpoint(tcp::endpoint ep) {
            listeners_
                .emplace_back(sessmgr_.strand_, ep, sessmgr_.enter()) //
                .start();
        }

        void stop() {
            for(auto& l : listeners_)
                l.stop();
            sessmgr_.shutdown();
        }

        void for_each_handle(auto f) { sessmgr_.for_each_handle(std::move(f)); }
        void for_each_session(auto f) { sessmgr_.for_each_session(std::move(f)); }
        size_t Count() { return sessmgr_.Count(); }

      private:
        Executor                 ex_;
        Strand                   strand_{make_strand(ex_)};
        session_manager<Factory> sessmgr_;
        std::list<listener>      listeners_;
    };
}

struct MyServer {
    using SessPtr = std::shared_ptr<SessionA>;
    using Factory = std::function<SessPtr(networking::listener::socket_t&&, int id)>;
    using Server  = networking::Server<Factory>;
    using MsgPtr  = SessionA::MsgPtr;

    Factory factory_ = [this](Server::socket_t&& s, int id) {
        using boost::placeholders::_1;
        using boost::placeholders::_2;
        auto product = std::make_shared<SessionA>(
            std::move(s), id,
            boost::bind(&MyServer::OnMessage, this, _1, _2), //
            [](auto&&...) {},                                //
            [](auto&&...) {});
        product->run();
        OnClientConnect(product);
        return product;
    };

    MyServer(Executor executor, tcp::endpoint ep)
        : ex_(executor)
        , server_(executor, factory_, {ep})
    {
    }

    void interrupt() {
        debug << "Server interrupted" << std::endl;
        server_.stop();
    }

    Executor ex_;
    Server server_;

    size_t Count() { return server_.Count(); }

    void BroadcastMessage(MsgPtr msg)
    {
        server_.for_each_handle(
            [this, msg = std::move(msg)](auto const& handle) {
                if (auto sess = handle.lock())
                    post(ex_, [=] { sess->Send(msg); });
            });
    }

#if 1
    void OnClientDisconnect([[maybe_unused]] SessPtr const& remote)
    {
        //std::cout << "[ Client  " << remote->GetId() << " ] Disconnected" << std::endl;
    }

    bool OnClientConnect(SessPtr const& remote)
    {
        //std::cout << "[ Client  " << remote->GetId() << " ] Connected" << std::endl;

        {
            using namespace protocol;
            auto msg = std::make_shared<MyMessage>();
            msg->message_header.id = MessageTypes::ServerAccept;
            msg->put(remote->GetId());
            remote->Send(std::move(msg));
        }
        return true;
    }

    void OnMessage(MsgPtr const& msg, SessPtr const& remote)
    {
        //std::cout << "[ Client " << remote->GetId() << " ] ";
        if (msg->message_header.id == protocol::MessageTypes::SendText) {
            g_latencies.sample(epoch_nanos() - msg->message_header.timestamp);

            [[maybe_unused]] auto message = msg->TextFragments().front();
            // std::cout << "Received Message (lenth:" << message.length() <<
            // ")" << std::endl;
            remote->Send(msg); // fire it back to the client
        }
    }

    void OnMessageSent(MsgPtr const&, [[maybe_unused]] SessPtr const& remote)
    {
        // std::cout << "[ Client " << remote->GetId() << " ] ";
        // std::cout << " Sent Message" << std::endl;
    }
#endif
};

MyServer* srv;

int                      messageCount  = 0;
Clock::duration          previous_time = 0s;
std::atomic_bool         stop          = false;
boost::asio::thread_pool context;
// Clock::duration          highest_time       = 0s;
// int                      total_thread_count = 0;
// size_t                   max_thread_count   = 1;

Timer timer(context, 1s);

void timedBcast(error_code e)
{
    timer.expires_from_now(99ms);

    if (e || stop || !srv)
        return;

    if (messageCount >= 1000) {
        messageCount = 0;
    }

    {
        using namespace protocol;
        auto msg = std::make_shared<MyMessage>();

        msg->message_header.id = MessageTypes::MessageAll;
        auto space = msg->Alloc(rand() % 102400 + 81920);
        {
            std::fill(begin(space), end(space), 'a');
            auto countmsg = " " + std::to_string(messageCount++);
            std::copy(begin(countmsg), end(countmsg),
                    end(space) - countmsg.length());
        }
        srv->BroadcastMessage(std::move(msg));
        // std::cout << "Sessions: " << srv->Count() << std::endl;
    }

    // Reschedule the timer
    timer.async_wait(timedBcast);
    std::cout << "BROADCAST (sleep " << (timer.expiry() - Clock::now()) / 1.0ms << "ms)" << std::endl;
}

int main()
{
    messageCount = 1;
    srand(time(nullptr));
    tcp::endpoint ep{{}, 40000};

    srv = new MyServer(context.get_executor(), ep);

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Hello!\n";
    timer.expires_from_now(1s);
    timer.async_wait(timedBcast);

    std::string str;
    std::getline(std::cin, str);

    srv->interrupt();
    // t1.interrupt();
    stop = true;
    timer.cancel();

    context.stop();

    context.join();
    std::getline(std::cin, str);
    delete srv;
    return 0;
}
