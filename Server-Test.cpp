// Server-Test.cpp : This file contains the 'main' function. Program execution
// begins and ends there.
//

#include "prerequisites.h"
#include <utility>

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

using Executor = boost::asio::thread_pool::executor_type;

class MyServer : public Server<Message<MessageTypes>, Executor> {
  public:
    using Message = ::Message<MessageTypes>;

    MyServer(Executor executor, tcp::endpoint ep)
        : MyServer::base_type(executor, std::move(ep))
    {
    }
    void OnClientDisconnect(ConnPtr const& client) override
    {
        std::cout << "[ Client  " << client->GetId() << " ] Disconnected"
                  << std::endl;
    }

    bool OnClientConnect(ConnPtr const& client) override
    {
        std::cout << "[ Client  " << client->GetId() << " ] Connected"
                  << std::endl;
        Message msg;
        msg.message_header.id = MessageTypes::ServerAccept;
        msg << client->GetId();
        client->Send(msg);
        return true;
    }

    void OnMessage(MsgPtr const& msg, ConnPtr const& remote) override
    {
        std::cout << "[ Client " << remote->GetId() << " ] ";
        if (msg->message_header.id == MessageTypes::SendText) {
            std::string message = msg->GetString(0);
            std::cout << " Received Message" << std::endl;
            message = "";
            remote->Send(*msg); // fire it back to the client
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

int                      messageCount;
Clock::duration          previous_time      = 0s;
// Clock::duration       highest_time       = 0s;
// int                   total_thread_count = 0;
// size_t                max_thread_count   = 1;
std::atomic_bool         stop               = false;
boost::asio::thread_pool context;

boost::asio::high_resolution_timer timer(context, 1s);

void timedBcast(const boost::system::error_code& e)
{
    // std::cout << "Beginning BCAST..." << std::endl;
    Clock::time_point const tStart = Clock::now();

    if (!e && !stop) {

        if (srv != nullptr) {

            if (messageCount >= 1000) {
                messageCount = 0;
            }

            // std::cout << "SENDING BCAST" << std::endl;
            // std::string message = "HELLO WORLD TO ALL BROADCAST! ";
            // message += std::to_string(messageCount++);

            Message<MessageTypes> msg;
            msg.message_header.id = MessageTypes::MessageAll;
            msg.Append(std::string(rand() % 102400 + 81920, 'a') + " " +
                       std::to_string(messageCount++));
            //msg.TransactionId = "Broadcast";
            Clock::time_point tPrepared = Clock::now();
            srv->BroadcastMessage(msg);

            auto const tDone = Clock::now();
            auto const time  = tDone - tPrepared;
            auto const time2 = tDone - tStart;

            if (time != previous_time && time > 6ms) {
                // timer += time - 6;
                std::cout << "Broadcast took " << time / 1ms << "ms | "
                          << time2 / 1ms << "ms" << std::endl;
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
    timer.expires_from_now(5s);
    timer.async_wait(timedBcast);

    std::string str;
    std::getline(std::cin, str);

    srv->interrupt();
    // t1.interrupt();
    stop = true;
    timer.cancel();

    context.stop();

    context.join();
    str = "";
    std::getline(std::cin, str);
    delete srv;
    return 0;
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started:
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add
//   Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project
//   and select the .sln file
