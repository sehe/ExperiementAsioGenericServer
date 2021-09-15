#pragma once
#include "prerequisites.h"
template <typename Message, typename Executor> class Server {
  protected:
    using base_type  = Server<Message, Executor>;
    using acceptor_t = boost::asio::basic_socket_acceptor<tcp, Executor>;
    using Strand     = boost::asio::strand<Executor>;
    using conn_t     = Connection<Message, Strand>;
    using MsgPtr     = typename conn_t::MsgPtr;
    using ConnPtr    = std::shared_ptr<conn_t>;

  public:
    Server(Executor executor, tcp::endpoint endpoint)
        : acceptor_(executor)
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
        // messageVar.notify_one();
        acceptor_.cancel();
        acceptor_.close();
        {
            std::lock_guard lk(connectionMutex);
            for (const auto& [key, value] : connections) {
                value->Disconnect(true, true, true);
            }
            connections.clear();
        }

        shutdownCompleted = true;
    }

    unsigned long long CalculateAverageBacklog()
    {
        size_t total = 0;
        size_t count = 0;
        {
            std::lock_guard lk(connectionMutex);
            for (const auto& [key, value] : connections) {
                if (!value->IsInvalid()) {
                    size_t backlog = value->GetSendBacklog();
                    total += backlog;
                    count++;
                }
            }
        }
        if (count > 0) {
            auto average = total / count;
            return average;
        }
        return 0;
    }

    void BroadcastMessage(MsgPtr msg)
    {
        std::lock_guard lk(connectionMutex);

        for (const auto& [key, value] : connections) {
            if (!value->IsInvalid()) {
                value->Send(msg);
            }
        }
    }

    virtual ~Server() = default; // important for `delete` on derived classes!

    bool IsShutDownCompleted()
    {
        return shutdownCompleted;
    }

  protected:
    // This server class should override thse functions to implement
    // customised functionality

    // Called when a client connects, you can veto the connection by returning
    // false
    virtual bool OnClientConnect(ConnPtr const& /*remote*/) { return false; }

    // Called when a client appears to have disconnected
    virtual void OnClientDisconnect(ConnPtr const& /*remote*/) { }

    // Called when a message arrives
    virtual void OnMessage(MsgPtr const& /*message*/, ConnPtr const&) { }
    virtual void OnMessageSent(MsgPtr const& /*message*/, ConnPtr const&) { }

  private:
    void client_disconnected(ConnPtr const& connection)
    {
        OnClientDisconnect(connection);
        removeConnection(connection);
    }

    void client_message(MsgPtr const& message, ConnPtr const& conn)
    {
        OnMessage(message, conn);
    }

    void message_sent(MsgPtr const& message, ConnPtr const& conn)
    {
        OnMessageSent(message, conn);
    }

    void start_accept()
    {
        using boost::placeholders::_1;
        using boost::placeholders::_2;
        if (!shutdownBegan && acceptor_.is_open()) {

            auto new_connection = conn_t::create(
                make_strand(acceptor_.get_executor()),
                connectionIds++,
                boost::bind(&Server::client_message, this, _1, _2),
                boost::bind(&Server::message_sent, this, _1, _2),
                boost::bind(&Server::client_disconnected, this, _1));

            acceptor_.async_accept(
                new_connection->socket(),
                boost::bind(&Server::handle_accept, this, new_connection,
                            boost::asio::placeholders::error));
        }
    }

#pragma region Handle the Connection Acceptance
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
#pragma endregion

#pragma region Add Connection
    void addConnection(ConnPtr connection)
    {
        if (!shutdownBegan) {
            std::lock_guard lk(connectionMutex);
            connections.emplace(connection->GetId(),
                                      std::move(connection));
        }
    }
#pragma endregion

#pragma region Remove Connection
    void removeConnectionById(int id)
    {
        if (!shutdownBegan) {
            std::lock_guard lk(connectionMutex);
            connections.erase(id);
        }
    }
    
    void removeConnection(ConnPtr const& connection)
    {
        removeConnectionById(connection->GetId());
    }

#pragma endregion

    acceptor_t             acceptor_;
    std::mutex             connectionMutex;
    std::map<int, ConnPtr> connections;
    std::vector<int>       connectionsToRemove;
    std::atomic_bool       shutdownBegan{false};
    std::atomic_bool       shutdownCompleted{false};
    int                    connectionIds{10'000};

    struct OwnedMessage {
        ConnPtr remote = nullptr;
        Message msg;
    };
    ThreadSafeQueue<OwnedMessage> qMessagesIn; // why is this here?
};
