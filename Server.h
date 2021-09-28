#pragma once
#include "prerequisites.h"
#include <future>

template <typename Derived, typename Connection> class Server {
  protected:
    using base_type   = Server<Derived, Connection>;
    using acceptor_t  = boost::asio::basic_socket_acceptor<tcp, Strand>;
    using ConnPtr     = std::shared_ptr<Connection>;
    using WeakConnPtr = std::weak_ptr<Connection>;

  public:
    Server(Executor executor, tcp::endpoint endpoint)
        : executor_(executor)
    {
        acceptor_.open(endpoint.protocol());
        acceptor_.set_option(tcp::acceptor::reuse_address(true));
        acceptor_.set_option(tcp::acceptor::do_not_route(true));
        acceptor_.set_option(tcp::acceptor::keep_alive(false));
        acceptor_.set_option(tcp::acceptor::enable_connection_aborted(false));
        acceptor_.set_option(tcp::acceptor::linger(false, 3));

        acceptor_.bind(endpoint);
        acceptor_.listen();

        start_accept();
    }

    void interrupt()
    {
        shutdownBegan = true;

        post(strand_, [this] {
            acceptor_.cancel();
            acceptor_.close();
            for (const auto& [id, handle] : connections) {
                if (auto conn = handle.lock())
                    conn->Disconnect(true, true, true);
            }
            connections.clear();

            shutdownCompleted = true;
        });
    }

    std::future<size_t> CalculateAverageBacklog()
    {
        std::packaged_task<size_t()> task([this]() -> size_t {
            size_t total = 0;
            size_t count = 0;
            for (const auto& [id, handle] : connections) {
                if (auto conn = handle.lock()) {
                    if (!conn->IsInvalid()) {
                        size_t backlog = conn->GetSendBacklog();
                        total += backlog;
                        count++;
                    }
                }
            }

            if (count) {
                auto average = 1.0 * total / count;
                return average;
            }
            return 0;
        });

        auto fut = task.get_future();
        post(strand_, std::move(task));
        return fut;
    }

    template <typename ConstMessage>
    void BroadcastMessage(std::shared_ptr<ConstMessage> msg)
    {
        post(strand_, [this, msg = std::move(msg)] {
            for (const auto& kvp : connections) {
                post(executor_, [this, handle = kvp.second, msg] {
                    if (auto conn = handle.lock()) {
                        if (!conn->IsInvalid()) {
                            conn->Send(std::move(msg));
                        }
                    }
                });
            }
        });
    }

    virtual ~Server() = default; // important for `delete` on derived classes!

  protected:
    // This server class should override thse functions to implement
    // customised functionality

    // Called when a client connects, you can veto the connection by returning
    // false
    virtual bool OnClientConnect(ConnPtr const& /*remote*/) { return false; }

    // Called when a client appears to have disconnected
    virtual void OnClientDisconnect(ConnPtr const& /*remote*/) { }

    /* // Static polymorphism:
     *template <typename Message>
     *void OnMessage(std::shared_ptr<Message const> const& message,
     *               ConnPtr const&);
     *template <typename Message>
     *void OnMessageSent(std::shared_ptr<Message const> const& message,
     *                   ConnPtr const&);
     */

  private:
    Derived&       derived()       { return *static_cast<Derived*>(this);       } 
    Derived const& derived() const { return *static_cast<Derived const*>(this); } 

    void start_accept()
    {
        using boost::placeholders::_1;
        using boost::placeholders::_2;
        if (!shutdownBegan && acceptor_.is_open()) {

            auto new_connection = Connection::create(
                make_strand(executor_), //
                connectionIds++,
                [this](auto const&... args) { derived().OnMessage(args...); },
                [this](auto const&... args) { derived().OnMessageSent(args...); },
                [this](auto const&... args) { this->OnClientDisconnect(args...); });

            acceptor_.async_accept(
                new_connection->socket(),
                boost::bind(&Server::handle_accept, this, new_connection,
                            boost::asio::placeholders::error));
        }
    }

    void handle_accept(ConnPtr new_connection, error_code error)
    {
        if (!error && !shutdownBegan) {
            start_accept();
            using boost::placeholders::_1;
            if (OnClientConnect(new_connection)) {
                new_connection->accepted();
                addConnection(std::move(new_connection));
            } else {
#ifdef VERBOSE_SERVER_DEBUG
                std::cout << "[ Client " << new_connection->GetId()
                          << " ]  Connection Denied." << std::endl;
#endif
            }

        } else if (!shutdownBegan) {
            start_accept();
#ifdef VERBOSE_SERVER_DEBUG

            std::cout << "[ SERVER ] New connection error: " << error.message()
                      << std::endl;
#endif
        }
    }

    void addConnection(ConnPtr connection)
    {
        if (!shutdownBegan) {
            post(strand_, [this, conn = std::move(connection)]() mutable {

                connections.emplace(conn->GetId(), std::move(conn));
                // garbage collect connections
                // c++20, otherwise clumsy iterator loop
                std::erase_if(connections,
                              [](auto& kvp) { return kvp.second.expired(); });
            });
        }
    }

    Executor                   executor_;
    Strand                     strand_ = make_strand(executor_);
    acceptor_t                 acceptor_{strand_};
    std::map<int, WeakConnPtr> connections;
    std::atomic_bool           shutdownBegan{false};
    std::atomic_bool           shutdownCompleted{false};
    int                        connectionIds{10'000};
};
