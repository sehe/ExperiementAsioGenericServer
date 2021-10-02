#pragma once
#include "prerequisites.h"

template <typename Session>
class Client : public std::enable_shared_from_this<Client<Session> >
{
  protected:
    using base_type = Client<Session>;
    using std::enable_shared_from_this<base_type>::shared_from_this;

    using SessPtr = std::shared_ptr<Session>;
    using MsgPtr  = typename Session::MsgPtr;

  protected:
    virtual void OnConnect() {}
    virtual void OnDisconnect(SessPtr const&) {}
    virtual void OnMessage(MsgPtr const&, SessPtr const&) {}
    virtual void OnMessageSent(MsgPtr const&, SessPtr const&) {}

  public:
	bool Connect(const std::string& host, const uint16_t port)
	{
        return Connect(
            tcp::resolver(_strand).resolve(host, std::to_string(port)));
    }

    bool Connect(tcp::resolver::results_type endpoints)
	{
        // Create connection
        using boost::placeholders::_1;
        using boost::placeholders::_2;
        _connection = std::make_shared<Session>(
            _strand, 0, //
            boost::bind(&Client::OnMessage, shared_from_this(), _1, _2),
            boost::bind(&Client::OnMessageSent, shared_from_this(), _1, _2),
            boost::bind(&Client::OnDisconnect, shared_from_this(), _1));

        async_connect( //
            _connection->socket(), endpoints,
            [this, self = shared_from_this()](std::error_code ec,
                                              tcp::endpoint) {
                if (!ec) {
                    _connection->run();
                    OnConnect();
                }
            });

        return true;
    }

    void Disconnect()
    {
        if (_connection) {
            _connection->Disconnect(true, true, true);
        }
    }

    virtual ~Client() // NOTE the virtual again
    {
        Disconnect();
    }

    Client(Executor executor) : _strand(make_strand(executor)) {}

    void Send(MsgPtr msg)
    {
        if (_connection) {
            _connection->Send(std::move(msg));
        }
    }

  protected:
    Strand  _strand;
    SessPtr _connection;

    // not safe outside strand
    bool IsConnected() { return _connection && _connection->socket().is_open(); }
};
