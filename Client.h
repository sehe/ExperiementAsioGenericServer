#pragma once
#include "prerequisites.h"

template <typename Message, typename Executor>
class Client
{
  protected:
    using base_type   = Client<Message, Executor>;
    using Strand      = boost::asio::strand<Executor>;
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
        tcp::resolver::results_type endpoints;
        try {
            // Resolve hostname/ip-address into tangiable physical address
			tcp::resolver resolver(_strand);
            // TODO FIXME why not async? Could this be kept out of the Client so
            // that there is no blocking resolve at the start of each
            // connection?
            endpoints = resolver.resolve(host, std::to_string(port));
        } catch (std::exception& e) {
            std::cerr << "Client Exception: " << e.what() << std::endl;
            return false;
        }

        post(_strand, [this, endpoints] {
            // Create connection
            using boost::placeholders::_1;
            using boost::placeholders::_2;
            _connection = conn_t::create( //
                _strand, 0,               //
                boost::bind(&Client::DoOnMessage, this, _1, _2),
                boost::bind(&Client::DoOnMessageSent, this, _1, _2),
                boost::bind(&Client::DoOnDisconnected, this, _1));

            async_connect( //
                _connection->socket(), endpoints,
                [this](std::error_code ec, tcp::endpoint) {
                    if (!ec) {
                        _connection->accepted();
                        OnConnect();
                    }
                });
        });
        return true;
    }

    void Disconnect()
    {
        post(_strand, [c = _connection] { //
            if (c) {
                c->Disconnect(true, true, true);
            }
        });
    }

    virtual ~Client() // NOTE the virtual again
    {
        Disconnect();
    }

    Client(Executor executor) : _strand(make_strand(executor)) {}

    // not safe outside strand
    bool IsConnected() { return _connection && _connection->socket().is_open(); }

    void Send(Message msg)
    {
        post(_strand, [c = _connection, msg = std::move(msg)]() mutable { //
            if (c) {
                c->Send(std::make_shared<Message>(std::move(msg)));
            }
        });
    }

    bool IsSending() { return _connection && _connection->IsSending(); }

    void DoOnDisconnected(ConnPtr const& client) { OnDisconnect(client); }
    void DoOnMessage(MsgPtr const& m, ConnPtr const& c) { OnMessage(m, c); }
    void DoOnMessageSent(MsgPtr const& m, ConnPtr const& c) { OnMessageSent(m, c); }

    void SetId(int id) { _connection->SetId(id); }

  protected:
    Strand   _strand;
    ConnPtr  _connection;
};
