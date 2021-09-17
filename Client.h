#pragma once
#include "prerequisites.h"

template <typename Message, typename Executor>
class Client
{
  protected:
    using base_type   = Client<Message, Executor>;
    using Strand      = boost::asio::strand<Executor>;
    using socket_t    = boost::asio::basic_stream_socket<tcp, Strand>;
    using conn_t      = Connection<Message, Strand>;
    using MsgPtr      = typename conn_t::MsgPtr;
    using ConnPtr     = std::shared_ptr<conn_t>;

  protected:
    virtual void OnConnect() {}
    virtual void OnDisconnect(ConnPtr const&) {}
    virtual void OnMessage(MsgPtr const&, ConnPtr const&) {}
    virtual void OnMessageSent(MsgPtr const&, ConnPtr const&) {}

  public:
	bool Connect(const std::string& host, const uint16_t port)
	{
		try
		{
			// Resolve hostname/ip-address into tangiable physical address
			tcp::resolver resolver(_strand);
            tcp::resolver::results_type endpoints =
                resolver.resolve(host, std::to_string(port));

            // Create connection
            using boost::placeholders::_1;
            using boost::placeholders::_2;
            connection = conn_t::create( //
                _strand, 0,              //
                boost::bind(&Client::DoOnMessage, this, _1, _2),
                boost::bind(&Client::DoOnMessageSent, this, _1, _2),
                boost::bind(&Client::DoOnDisconnected, this, _1));

            // Tell the connection object to connect to server
            // connection->ConnectToServer(endpoints);
            async_connect( //
                connection->socket(), endpoints,
                [this](std::error_code ec, tcp::endpoint) {
                    if (!ec) {
                        connection->accepted();
                        OnConnect();
                    }
                });
            return true;
        } catch (std::exception& e) {
            std::cerr << "Client Exception: " << e.what() << std::endl;
			return false;
        }
    }

    void Disconnect()
    {
        if (IsConnected()) {
            connection->Disconnect(true, true, true);
        }
    }

    virtual ~Client() // NOTE the virtual again
    {
        Disconnect();
    }

    Client(Executor executor) : _strand(make_strand(executor)) {}

    bool IsConnected() { return connection && connection->socket().is_open(); }

    void Send(Message msg)
    {
        if (IsConnected()) {
            connection->Send(std::make_shared<Message>(std::move(msg)));
        }
    }

    bool IsSending() { return connection && connection->IsSending(); }

    void DoOnDisconnected(ConnPtr const& client) { OnDisconnect(client); }
    void DoOnMessage(MsgPtr const& m, ConnPtr const& c) { OnMessage(m, c); }
    void DoOnMessageSent(MsgPtr const& m, ConnPtr const& c) { OnMessageSent(m, c); }

    void SetId(int id) { connection->SetId(id); }

  private:
    Strand   _strand;
    ConnPtr  connection;
};
