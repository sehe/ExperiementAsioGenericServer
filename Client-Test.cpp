// Client-Test.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "prerequisites.h"

class MyClient : public Client<Message<MessageTypes>, Executor> {
  public:
    using Message = ::Message<MessageTypes>;
    MyClient(Executor ex) : MyClient::base_type(ex) {}
    MyClient(MyClient const&) = default;

    virtual void OnDisconnect(ConnPtr const&)
    {
        std::cout << "Disconnected." << std::endl;
        isExiting = true;
    }
    virtual void OnMessage(MsgPtr const& msg, ConnPtr const&)
    {
        switch (msg->message_header.id) {
        case MessageTypes::ServerAccept: {
            auto id = msg->get<int>();
            SetId(id);
            std::cout << "[ SERVER ] SENT ID: " << id << std::endl;
            // SEHE TODO FIXME
            std::thread(&MyClient::SendMessages, this).detach();
            break;
        }
        case MessageTypes::SendText: {
            std::cout << "[ SERVER ] SENT MESSAGE " << msg->size()
                      << " bytes long." << std::endl;
            break;
        }
        case MessageTypes::MessageAll: {
            auto now      = Clock::now();
            // atomic laptime
            auto time_taken = now - std::exchange(time_point, now);
            auto s          = msg->TextFragments().front();

            if (!std::exchange(isFirst, false)) {
                if (time_taken > 55ms) {
                    std::cout << "MESSAGE WAS DELAYED (" << (time_taken / 1ms)
                              << "ms, length: " << s.length() << ")"
                              << std::endl;
                }
            }
            break;
        }
        default: throw std::runtime_error("Message type not implemented");
        }
    }

    virtual void OnConnect()
    {
        std::cout << "[ DEBUG ] Thread Id: " << std::this_thread::get_id() << std::endl;
    }
    virtual void OnMessageSent(MsgPtr const&)
    {
        std::cout << "Message sent" << std::endl;
    }

    Clock::time_point time_point;
    bool              isFirst{true};
    bool              isExiting{false};

    ~MyClient()
    {
        isExiting = true;
        Disconnect();
    }

  private:
    void SendMessage(char ch)
    {
        auto msg_size = rand() % 512000 + 409600;

        Message msg;
        msg.message_header.id = MessageTypes::SendText;

        auto payload = msg.Alloc(msg_size);
        std::fill(begin(payload), end(payload), ch);

        Send(std::move(msg));
    }

    void SendMessages()
    {
        /* initialize random seed: */
        srand(time(NULL));

        /* generate secret number between 1 and 10: */
        auto num_msgs  = rand() % 10 + 1;

        for (int i = 0; i < num_msgs; i++) {
            if (IsConnected())
            {
                std::cout << "Sending " << i + 1 << " of " << num_msgs << std::endl;
                SendMessage('a' + i % 26);
                std::cout << "Sent " << i + 1 << " of " << num_msgs << std::endl;

                auto delay = 1s + 1ms * (rand() % 10'000);
                std::cout << "Sleeping for " << delay / 1.0s << std::endl;
                std::this_thread::sleep_for(delay);
            }

            if (!IsConnected()) {
                std::cout << "Not connected. Exiting thread." << std::endl;
                return;
            }

        }
    }
};

int main()
{
    boost::asio::thread_pool io;

    std::string host = "localhost";
    uint16_t    port = 40'000;

    {
        std::list<MyClient> clients(200, io.get_executor());

        for (auto& c : clients) {
            std::cout << "Connect" << std::endl;
            c.Connect(host, port);
        }

        std::this_thread::sleep_for(10s);
    } // destructors call Disconnect()

    std::cout << "DONE" << std::endl;
    io.join();
}
