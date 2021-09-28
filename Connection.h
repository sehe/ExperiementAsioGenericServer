#pragma once
#include "prerequisites.h"
template <typename Message, typename Executor>
class Connection
    : public std::enable_shared_from_this<Connection<Message, Executor>> //
{
    using std::enable_shared_from_this<Connection>::shared_from_this;
    using socket_t = boost::asio::basic_stream_socket<tcp, Executor>;

  public:
    using ConnPtr = std::shared_ptr<Connection>;
    using MsgPtr  = std::shared_ptr<Message const>;

    using ClientMessageCallbackType     = std::function<void(MsgPtr const&, ConnPtr const&)>;
    using ClientMessageSentCallbackType = std::function<void(MsgPtr const&, ConnPtr const&)>;
    using ClientDisconnectCallbackType  = std::function<void(ConnPtr const&)>;

    enum class owner { server, client };

    static ConnPtr create(Executor executor, int id,
                          ClientMessageCallbackType     message_handle,
                          ClientMessageSentCallbackType messagesent_handle,
                          ClientDisconnectCallbackType  disconnect_handle)
    {
        // should be make_shared, but not posisble due to private constructor
        return ConnPtr(new Connection(
            executor, id, message_handle, messagesent_handle,
            disconnect_handle));
    }

    socket_t& socket() { return socket_; }
    int       GetId() { return connectionId; }
    void      SetId(int id) { connectionId = id; }

    void accepted()
    {
#ifdef VERBOSE_SERVER_DEBUG
        {
            std::stringstream ss;

            ss << "[ Client " << GetId() << " ] "
                << "Connected.";
            std::string output = ss.str();
            std::cout << output << std::endl;
            output = "";
            ss.str("");
        }
#endif
        tcp::no_delay option(true);
        socket_.set_option(option);

        ReadHeader();
    }

    void Disconnect(bool cancel, bool shutdown, bool close)
    {
        qMessagesOut.clear();
        if (socket_.is_open() && !alreadyDisconnected.exchange(true)) {
            error_code ec;

            if (cancel) {
                socket_.cancel(ec);
            }

            if (shutdown) {
                socket_.shutdown(socket_t::shutdown_both, ec);
            }

            if (close && !ec) {
                socket_.close(ec);
            }

#ifdef VERBOSE_SERVER_DEBUG
            std::stringstream ss;
            ss << "[ Client " << GetId() << " ] "
               << "Disconnected.";
            std::string output = ss.str();
            std::cout << output << std::endl;
            output = "";
            ss.str("");
#endif
            if (disconnect_handler) {
                disconnect_handler(shared_from_this());
            }
        }
        invalidState = true;
    }

    bool IsInvalid() const
    {
        return !socket_.is_open() || invalidState;
    }

    void Send(MsgPtr msg)
    {
        if (IsInvalid())
            return;
        qMessagesOut.push_back(std::move(msg));
        if (qMessagesOut.size() == 1) { // SEHE TODO FIXME Race condition?
            WriteMessage();
        }
    }
    size_t GetSendBacklog()
    {
        if (!IsInvalid()) {
            return qMessagesOut.count();
        }
        return 0;
    }
    ~Connection()
    {
        Disconnect(true, true, true);
    }

  private:
    Connection(Executor executor, int id,
               ClientMessageCallbackType      message_handle,
               ClientMessageSentCallbackType  messagesent_handle,
               ClientDisconnectCallbackType   disconnect_handle
            )
        : disconnect_handler(disconnect_handle)
        , message_handler(message_handle)
        , messagesent_handler(messagesent_handle)
        , socket_(executor)
        , connectionId(id)
    { }

    ClientDisconnectCallbackType  disconnect_handler;
    ClientMessageCallbackType     message_handler;
    ClientMessageSentCallbackType messagesent_handler;

    bool Report([[maybe_unused]] std::string_view caption, error_code ec,
                [[maybe_unused]] auto&&... what)
    {
        if (ec) {
#ifdef VERBOSE_SERVER_DEBUG
            std::cout << "[ Client " << GetId() << " ] " << caption
                      << " Failed"
                      << ec.value() << " - " << ec.message()
                      << std::endl;
#endif
            invalidState = true;
            Disconnect(true, true, true);
            return false;
        }

#ifdef VERBOSE_SERVER_DEBUG
        {
            [[maybe_unused]] auto print_arg = [](auto&& v) {
                std::cout << " " << v;
                return std::cout.good();
            };

            std::cout << "[ Client " << GetId() << " ] " << caption << " Success";
            std::cout << " " << ec.value() << " - " << ec.message();
            if ((true && ... && print_arg(what)))
                std::cout << std::endl;
        }
#endif
        return true;
    }

    void ReadHeader()
    {
        async_read( //
            socket_,
            boost::asio::buffer(&tempInMsg.message_header,
                                sizeof(tempInMsg.message_header)),
            [this, self = shared_from_this()](error_code ec, std::size_t) {
                if (Report("Read Header", ec)) {
                    tempInMsg.body.resize(tempInMsg.message_header.size);
                    ReadBody();
                }
            });
    }

    void ReadBody()
    {
        async_read( //
            socket_, boost::asio::buffer(tempInMsg.body),
            [this, self = shared_from_this()](error_code ec, std::size_t) {
                if (Report("Read Body", ec, tempInMsg.body.data())) {
                    CommitIncoming();

                    // Go Back to waiting for header
                    ReadHeader();
                }
            });
    }

    void WriteMessage()
    {
        // If this function is called, we know the outgoing message queue must
        // have at least one message to send. So allocate a transmission buffer
        // to hold the message, and issue the work - asio, send these bytes
        if (!qMessagesOut.empty()) {
            auto message = std::move(qMessagesOut.front());
            qMessagesOut.pop_front();

            std::vector bufs = {
                boost::asio::buffer(&message->message_header,
                                    sizeof(message->message_header)),
                boost::asio::buffer(message->body),
            };

            async_write( //
                socket_, bufs,
                [this, self = shared_from_this(),
                 message](error_code ec, std::size_t length) mutable {
                    if (Report("WriteMessage", ec, "Wrote", length, "bytes")) {
                        if (!qMessagesOut.empty()) {
                            // TODO FIXME Race condition?
                            WriteMessage();
                        }
                    }
                });
        }
    }

    void CommitIncoming()
    {
        if (message_handler) {
            message_handler(
                std::make_shared<Message>(std::move(tempInMsg)),
                shared_from_this());
        }

        tempInMsg.body.clear();
        tempInMsg.message_header.size = 0;
    }

    socket_t socket_;

    int connectionId;

    Message tempInMsg;
    Message tempOutMsg;

    std::deque<MsgPtr> qMessagesOut;

    std::atomic_bool invalidState        = false;
    std::atomic_bool alreadyDisconnected = false;
};
