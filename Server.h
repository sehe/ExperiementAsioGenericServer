#pragma once
#include "prerequisites.h"
template <typename MsgId, typename Executor> class Server {
    using acceptor_t = boost::asio::basic_socket_acceptor<tcp, Executor>;
    using socket_t   = boost::asio::basic_stream_socket<tcp, Executor>;

  protected:
    using conn_t       = Connection<MsgId, Executor>;
    using OwnedMessage = typename conn_t::OwnedMessage;
    using ConnPtr      = boost::shared_ptr<conn_t>;
    using base_type    = Server<MsgId, Executor>;

  public:
    Server(Executor executor, tcp::endpoint endpoint)
        : acceptor_(executor)
    {
        this->acceptor_.open(endpoint.protocol());
        this->acceptor_.set_option(tcp::acceptor::reuse_address(true));
        this->acceptor_.set_option(tcp::acceptor::do_not_route(true));
        this->acceptor_.set_option(tcp::acceptor::keep_alive(false));
        this->acceptor_.set_option(tcp::acceptor::enable_connection_aborted(false));
        this->acceptor_.set_option(tcp::acceptor::linger(false, 3));

        this->acceptor_.bind(endpoint);
        this->acceptor_.listen();
        this->shutdownBeginning = false;
        this->shutdownCompleted = false;

        this->start_accept();
    }

    void interrupt()
    {
        this->shutdownBeginning = true;
        // this->messageVar.notify_one();
        this->acceptor_.cancel();
        this->acceptor_.close();
        std::unique_lock<std::mutex> conMutex(this->connectionMutex);
        for (const auto& [key, value] : this->connections) {
            value->Disconnect(true, true, true);
        }
        this->connections.clear();
        conMutex.unlock();

        this->shutdownCompleted = true;
    }
    unsigned long long CalculateAverageBacklog()
    {

        size_t                       total = 0;
        size_t                       count = 0;
        std::unique_lock<std::mutex> conMutex(this->connectionMutex);
        for (const auto& [key, value] : this->connections) {
            if (!value->IsInvalid()) {
                size_t backlog = value->GetSendBacklog();
                total += backlog;
                count++;
            }
        }
        conMutex.unlock();
        if (count > 0) {
            auto average = total / count;
            return average;
        }
        return 0;
    }
    void BroadcastMessage(Message<MsgId>& msg)
    {
        std::unique_lock<std::mutex> conMutex(this->connectionMutex);

        for (const auto& [key, value] : this->connections) {
            if (!value->IsInvalid()) {
                value->Send(msg);
            }
        }

        //std::for_each(std::execution::par_unseq, this->connections.begin(),
                      //this->connections.end(),
                      //[msg](auto&& item) { item.second->Send(msg); });
        conMutex.unlock();

        // this->bcast(msg);
    }

    virtual ~Server() = default; // important for `delete` on derived classes!

    bool IsShutDownCompleted()
    {
        return this->shutdownCompleted;
    }

  protected:
    // This server class should override thse functions to implement
    // customised functionality

    // Called when a client connects, you can veto the connection by returning
    // false
    virtual bool OnClientConnect(ConnPtr const& /*client*/)
    {
        return false;
    }

    // Called when a client appears to have disconnected
    virtual void OnClientDisconnect(ConnPtr const& /*client*/)
    {
    }

    // Called when a message arrives
    virtual void OnMessage(OwnedMessage& /*message*/)
    {
    }
    virtual void OnMessageSent(OwnedMessage* /*message*/)
    {
    }

  private:
    void client_disconnected(ConnPtr const& connection)
    {
        OnClientDisconnect(connection);
        removeConnection(connection);
    }
    void client_message(OwnedMessage& message)
    {
        OnMessage(message);
    }
    void message_sent(OwnedMessage* message)
    {
        OnMessageSent(message);
    }
    void start_accept()
    {
        using boost::placeholders::_1;
        if (!this->shutdownBeginning && this->acceptor_.is_open()) {
            int  id             = this->connectionIds++;
            auto new_connection = conn_t::create(
                this->acceptor_.get_executor(), id,
                boost::bind(&Server::client_message, this, _1),
                boost::bind(&Server::message_sent, this, _1),
                boost::bind(&Server::client_disconnected, this, _1),
                qMessagesIn);

            this->acceptor_.async_accept(
                new_connection->socket(),
                boost::bind(&Server::handle_accept, this, new_connection,
                            boost::asio::placeholders::error));
        }
    }

#pragma region Handle the Connection Acceptance
    void handle_accept(ConnPtr new_connection, error_code error)
    {
        if (!error && !this->shutdownBeginning) {
            this->start_accept();
            using boost::placeholders::_1;
            if (OnClientConnect(new_connection)) {
                boost::signals2::connection c = this->bcast.connect(
                    boost::bind(&conn_t::Send, new_connection, _1));
                new_connection->accepted(c);
                this->addConnection(std::move(new_connection));

            } else {
#ifdef VERBOSE_SERVER_DEBUG

                std::cout << "[ Client " << new_connection->GetId()
                          << " ]  Connection Denied." << std::endl;
#endif
            }

        } else if (!this->shutdownBeginning) {
            this->start_accept();
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
        if (!this->shutdownBeginning) {
            std::unique_lock<std::mutex> conMutex(this->connectionMutex);
            this->connections.emplace(connection->GetId(),
                                      std::move(connection));
            conMutex.unlock();
        }
    }
#pragma endregion

#pragma region Remove Connection
    void removeConnectionById(int id)
    {
        if (!this->shutdownBeginning) {
            std::unique_lock<std::mutex> conMutex(this->connectionMutex);
            this->connections.erase(id);
            conMutex.unlock();
        }
    }
    
    void removeConnection(ConnPtr const& connection)
    {
        removeConnectionById(connection->GetId());
    }

#pragma endregion

    acceptor_t                    acceptor_;
    std::mutex                    connectionMutex;
    std::map<int, ConnPtr>        connections;
    std::vector<int>              connectionsToRemove;
    std::atomic_bool              shutdownBeginning{false};
    std::atomic_bool              shutdownCompleted{false};
    int                           connectionIds{10'000};
    ThreadSafeQueue<OwnedMessage> qMessagesIn;

    boost::signals2::signal<void(Message<MsgId>&)> bcast;
};
