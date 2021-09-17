// Server-Test.cpp : This file contains the 'main' function. Program execution
// begins and ends there.
#include "prerequisites.h"
#include <utility>

class MyServer : public Server<Message<MessageTypes>, Executor> {
  public:
    using Message = ::Message<MessageTypes>;

    MyServer(Executor executor, tcp::endpoint ep)
        : MyServer::base_type(executor, std::move(ep))
    {
    }

    void OnClientDisconnect(ConnPtr const& remote) override
    {
        std::cout << "[ Client  " << remote->GetId() << " ] Disconnected"
                  << std::endl;
    }

    bool OnClientConnect(ConnPtr const& remote) override
    {
        std::cout << "[ Client  " << remote->GetId() << " ] Connected" << std::endl;

        {
            auto msg = std::make_shared<Message>();
            msg->message_header.id = MessageTypes::ServerAccept;
            msg->put(remote->GetId());
            remote->Send(std::move(msg));
        }
        return true;
    }

    void OnMessage(MsgPtr const& msg, ConnPtr const& remote) override
    {
        std::cout << "[ Client " << remote->GetId() << " ] ";
        if (msg->message_header.id == MessageTypes::SendText) {
            auto message = msg->TextFragments().front();
            std::cout << "Received Message (lenth:" << message.length() << ")"
                      << std::endl;
            remote->Send(msg); // fire it back to the client
        }
    }

    void OnMessageSent(MsgPtr const&,
                       [[maybe_unused]] ConnPtr const& remote) override
    {
        // std::cout << "[ Client " << remote->GetId() << " ] ";
        // std::cout << " Sent Message" << std::endl;
    }
};

MyServer* srv;

int                      messageCount       = 0;
Clock::duration          previous_time      = 0s;
std::atomic_bool         stop               = false;
boost::asio::thread_pool context;
//Clock::duration          highest_time       = 0s;
//int                      total_thread_count = 0;
//size_t                   max_thread_count   = 1;

boost::asio::high_resolution_timer timer(context, 1s);

void timedBcast(error_code e)
{
    // std::cout << "Beginning BCAST..." << std::endl;
    Clock::time_point const tStart = Clock::now();
    Clock::time_point tPrepared    = tStart;

    if (!e && !stop) {

        if (srv != nullptr) {
            if (messageCount >= 1000) {
                messageCount = 0;
            }

            // std::cout << "SENDING BCAST" << std::endl;
            // std::string message = "HELLO WORLD TO ALL BROADCAST! ";
            // message += std::to_string(messageCount++);
            {
                auto msg = std::make_shared<Message<MessageTypes>>();

                msg->message_header.id = MessageTypes::MessageAll;
                auto space = msg->Alloc(rand() % 102400 + 81920);
                {
                    std::fill(begin(space), end(space), 'a');
                    auto countmsg = " " + std::to_string(messageCount++);
                    std::copy(begin(countmsg), end(countmsg),
                              end(space) - countmsg.length());
                }
                //msg.TransactionId = "Broadcast";
                srv->BroadcastMessage(std::move(msg));
            }

            auto const tDone = Clock::now();
            auto const time  = tDone - std::exchange(tPrepared, tDone);
            auto const time2 = tDone - tStart;

            if (time != previous_time && time > 2us) {
                // timer += time - 6;
                std::cout << "Broadcast took " << time / 1.0us << "μs | "
                          << time2 / 1us << "μs" << std::endl;
                previous_time = time;
            }
            auto time_expire = 99ms - time2;
            if (time_expire <= 50ms) {
                time_expire = 50ms;
            }
            // Reschedule the timer
            timer.expires_from_now(time_expire);
            timer.async_wait(timedBcast);
            std::cout << "BROADCAST" << std::endl;
        }
    }
    // std::cout << "Exited bcast" << std::endl;
    // timer.cancel();
}

int main()
{
    messageCount = 1;
    srand(time(nullptr));
    tcp::endpoint ep{{}, 40000};

    srv = new MyServer(context.get_executor(), ep);

    std::cout << "Hello World!\n";
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
