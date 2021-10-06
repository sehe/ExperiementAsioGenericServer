// Server-Test.cpp : This file contains the 'main' function. Program execution
// begins and ends there.
#include "prerequisites.h"
#include <utility>

#include "Statistics.h"
static statistics::Accum g_latencies{"Latency", 100};

struct MyServer {
    using SessPtr = std::shared_ptr<SessionA>;
    using Factory = std::function<SessPtr(networking::listener::socket_t&&, int id)>;
    using Server  = networking::basic_server<Factory>;
    using Message = SessionA::Message;
    using MsgPtr  = SessionA::MsgPtr;

    Factory sessionFactory = [this](Server::socket_t&& s, int id) {
        using boost::placeholders::_1;
        using boost::placeholders::_2;
        auto product = std::make_shared<SessionA>(
            std::move(s), //
            id,           //
            [this](Message const& m, SessPtr const& s) { Handle(m, s); });

        product->run();

        SendSeverAccept(product);
        return product;
    };

    MyServer(Executor executor, tcp::endpoint ep)
        : ex_(executor)
        , server_(executor, sessionFactory, {ep})
    {
    }

    void Interrupt() {
        debug << "Server interrupted" << std::endl;
        server_.stop();
    }

    Executor ex_;
    Server server_;

    size_t Count() { return server_.Count(); }

    void SendSeverAccept(SessPtr const& remote)
    {
        using namespace protocol;
        auto msg = std::make_shared<MyMessage>();
        msg->message_header.id = MessageTypes::ServerAccept;
        msg->put(remote->GetId());
        remote->Send(std::move(msg));
    }

    void Handle(Message const& msg, SessPtr const& remote)
    {
        if (msg.message_header.id == protocol::MessageTypes::SendText) {
            g_latencies.sample(epoch_nanos() - msg.message_header.timestamp);

            [[maybe_unused]] auto message = msg.TextFragments().front();
            // std::cout << "Received Message (lenth:" << message.length() << ")" << std::endl;
            remote->Send(std::make_shared<Message>(
                std::move(msg))); // fire it back to the client
        }
    }

    void Broadcast(MsgPtr msg)
    {
        server_.for_each_handle(
            [this, msg = std::move(msg)](auto const& handle) {
                if (auto sess = handle.lock())
                    post(ex_, [=] { sess->Send(msg); });
            });
    }
};

struct TimedStressTest {
    TimedStressTest(MyServer& srv, Executor executor)
        : _srv(srv)
        , timer(executor, 1s)
    { }

    MyServer& _srv;
    Timer     timer;

    int                      messageId     = 1;
    std::atomic_bool         stop          = false;
    boost::asio::thread_pool context;

    using Dist = std::uniform_int_distribution<>;
    std::mt19937 prng_{std::random_device{}()};

    void loop(error_code ec = {})
    {
        timer.expires_from_now(99ms);

        if (ec)
            return;

        if (messageId >= 1000) {
            messageId = 1;
        }

        {
            using namespace protocol;
            auto msg = std::make_shared<MyMessage>();

            msg->message_header.id = MessageTypes::MessageAll;
            auto space = msg->Alloc(Dist{81'920, 102'400 }(prng_));
            {
                std::fill(begin(space), end(space), 'a');
                auto countmsg = " " + std::to_string(messageId++);
                std::copy(begin(countmsg), end(countmsg),
                          end(space) - countmsg.length());
            }
            _srv.Broadcast(std::move(msg));
            // std::cout << "Sessions: " << srv->Count() << std::endl;
        }

        // Reschedule the timer
        timer.async_wait([this](error_code ec) { loop(ec); });
        std::cout << "BROADCAST (sleep "
                  << (timer.expiry() - Clock::now()) / 1.0ms << "ms)"
                  << std::endl;
    }
};

int main() {
    boost::asio::thread_pool context;
    MyServer srv(context.get_executor(), tcp::endpoint{{}, 40000});

    std::this_thread::sleep_for(1s);
    TimedStressTest test(srv, context.get_executor());

    std::cin.ignore(1024'000, '\n');

    context.stop();
    context.join();
}
