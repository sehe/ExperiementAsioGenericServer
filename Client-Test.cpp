// Client-Test.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "prerequisites.h"

class MyClient : public Client<Message<MessageTypes>, Executor> {
  public:
    using Message = ::Message<MessageTypes>;
    MyClient(Executor ex) : MyClient::base_type(ex), timer_(ex) {}

    virtual void OnDisconnect(ConnPtr const&)
    {
        std::cout << "Disconnected." << std::endl;
        timer_.cancel();
        isexiting_ = true;
    }
    virtual void OnMessage(MsgPtr const& msg, ConnPtr const&)
    {
        switch (msg->message_header.id) {
        case MessageTypes::ServerAccept: {
            auto id = msg->get<int>();
            SetId(id);
            std::cout << "[ SERVER ] SENT ID: " << id << std::endl;
            StartSendMessages();
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
            auto time_taken = now - std::exchange(start_, now);
            auto s          = msg->TextFragments().front();

            if (!isfirst_.exchange(false)) {
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

    ~MyClient()
    {
        isexiting_ = true;
        // Disconnect(); // happens in baseclass destructor
    }

  private:
    void StartSendMessages()
    {
        if (num_msgs_ || !IsConnected())
            return; // already running

        std::cout << "[ SERVER ] StartSendMessages" << std::endl;
        /* generate secret number between 1 and 10: */
        num_msgs_ = Dist{1, 10}(prng_);

        post(_strand, [this] { TimedSendLoop(); });
    }

    void TimedSendLoop()
    {
        std::cout << "TimedSendLoop #" << num_msgs_ + 1 << std::endl;
        {
            auto msg_size = Dist{409'600, 921'600}(prng_);

            Message msg;
            msg.message_header.id = MessageTypes::SendText;

            auto payload = msg.Alloc(msg_size);
            std::fill(begin(payload), end(payload), 'a' + num_msgs_ % 27);

            Send(std::move(msg));
        }

        auto delay = 1ms * Dist{1'000, 10'000}(prng_);
        std::cout << "Sleeping for " << delay / 1.0s << std::endl;

        timer_.expires_from_now(delay);
        timer_.async_wait([this](error_code ec) {
            if (isexiting_ || ec == boost::asio::error::operation_aborted) {
                std::cout << "TimedSendLoop: " << ec.message() << std::endl;
                return;
            }
            if (num_msgs_--) {
                TimedSendLoop();
            }
        });
    }

    Clock::time_point start_;
    std::atomic_bool isfirst_{true};
    std::atomic_bool isexiting_{false};

    // SendMessages state
    using Dist = std::uniform_int_distribution<>;
    std::mt19937 prng_{std::random_device{}()};
    Timer        timer_;
    int          num_msgs_ = 0;
};

int main()
{
    boost::asio::thread_pool io;

    std::string host = "localhost";
    uint16_t    port = 40'000;

    {
        std::deque<std::unique_ptr<MyClient> > clients;

        std::generate_n( //
            back_inserter(clients), 200, [&] {
                auto c = std::make_unique<MyClient>(io.get_executor());
                std::cout << "Connect" << std::endl;
                c->Connect(host, port);
                return c;
            });

        std::this_thread::sleep_for(10s);
    } // destructors call Disconnect()

    std::cout << "DONE" << std::endl;
    io.join();
}
