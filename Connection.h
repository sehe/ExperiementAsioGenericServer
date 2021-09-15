#pragma once
#include "prerequisites.h"
template <typename Message, typename Executor>
class Connection
    : public boost::enable_shared_from_this<Connection<Message, Executor>> //
{
    using boost::enable_shared_from_this<Connection>::shared_from_this;
    using socket_t = boost::asio::basic_stream_socket<tcp, Executor>;

  public:
    using ConnPtr = boost::shared_ptr<Connection>;
    using MsgPtr  = boost::shared_ptr<Message>;

    using ClientMessageCallbackType     = boost::function<void(MsgPtr const&, ConnPtr const&)>;
    using ClientMessageSentCallbackType = boost::function<void(MsgPtr const&, ConnPtr const&)>;
    using ClientDisconnectCallbackType  = boost::function<void(ConnPtr const&)>;

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
    // void ModelItem::SubscribeItemCreated(const ModelItemSlot& slot)
    //{
    //    _itemCreated.connect(slot);
    //}

    socket_t& socket() { return socket_; }
    int GetId() { return connectionId; }

    void accepted(boost::signals2::connection bcast_connection)
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
        c = bcast_connection;
        tcp::no_delay option(true);
        socket_.set_option(option);

        ReadHeader();
    }

    void Disconnect(bool cancel, bool shutdown, bool close)
    {
        qMessagesOut.clear();
        if (socket_.is_open() && !alreadyDisconnected.exchange(true)) {
            boost::system::error_code ec;

            if (cancel) {
                socket_.cancel(ec);
            }

            if (shutdown) {
                socket_.shutdown(socket_t::shutdown_both, ec);
            }

            if (close && !ec) {
                socket_.close(ec);
            }

            // broadcastSignal.disconnect(boost::bind(&Connection::Send,
            // this); UnRegisterBroadcastSignal(broadcastSignal, this);
            c.disconnect();

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
    void Send(const Message& msg)
    {
        qMessagesOut.push_back(msg);
        if (qMessagesOut.count() == 1) { // SEHE TODO FIXME Race condition
            WriteHeader();
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

    void ReadHeader()
    {
        auto self = shared_from_this();

        async_read(socket_,
                   boost::asio::buffer(&tempInMsg.message_header,
                                       sizeof(tempInMsg.message_header)),
                   [this, self](std::error_code ec, std::size_t /*length*/) {
                       if (!ec) {
                           // A complete message header has been read, check if
                           // this message has a body to follow...
                           if (tempInMsg.message_header.size > 0) {
                               // ...it does, so allocate enough space in the
                               // messages' body vector, and issue asio with the
                               // task to read the body.
                               tempInMsg.body.resize(
                                   tempInMsg.message_header.size);
                               ReadBody();
                           } else {
                               // it doesn't, so add this bodyless message to
                               // the connections incoming message queue
                               AddToIncomingMessageQueue();
                               // Go back to waiting for header
                               ReadHeader();
                           }
                       } else {
                // Reading form the client went wrong, most likely a disconnect
                // has occurred. Close the socket and let the system tidy it up
                // later.
#ifdef VERBOSE_SERVER_DEBUG

                           std::stringstream ss;

                           ss << "[ Client " << GetId() << " ] "
                              << "Read Header Failed."
                              << " " << ec.value() << " - " << ec.message();
                           std::string output = ss.str();
                           std::cout << output << std::endl;
                           output = "";
                           ss.str("");
#endif
                           invalidState = true;
                           Disconnect(false, true, true);
                       }
                   });
    }

    void ReadBody()
    {
        auto self = shared_from_this();
        async_read(socket_, boost::asio::buffer(tempInMsg.body),
                   [this, self](std::error_code ec, std::size_t /*length*/) {
                       if (!ec) {
                           // ...and they have! The message is now complete, so
                           // add the whole message to incoming queue
                           AddToIncomingMessageQueue();

#ifdef VERBOSE_SERVER_DEBUG

                           std::stringstream ss;

                           ss << "[ Client " << GetId()
                              << " ] SENT: " << tempInMsg.body.data();

                           std::string output = ss.str();
                           std::cout << output << std::endl;
                           output = "";
                           ss.str("");
#endif
                           // Go Back to waiting for header
                           ReadHeader();
                       } else {
                // As above!
#ifdef VERBOSE_SERVER_DEBUG
                           std::stringstream ss;
                           ss << "[ Client " << GetId() << " ] "
                              << "Read Body Failed."
                              << " " << ec.value() << " - " << ec.message();
                           std::string output = ss.str();
                           std::cout << output << std::endl;
                           output = "";
                           ss.str("");
#endif
                           invalidState = true;
                           Disconnect(false, true, true);
                       }
                   });
    }

    void WriteHeader()
    {
        auto self = shared_from_this();
        // If this function is called, we know the outgoing message queue must
        // have at least one message to send. So allocate a transmission buffer
        // to hold the message, and issue the work - asio, send these bytes
        if (!qMessagesOut.empty()) {
            auto message =
                boost::make_shared<Message>(qMessagesOut.pop_front());
            async_write(
                socket_,
                boost::asio::buffer(&message->message_header,
                                    sizeof(message->message_header)),
                [this, self,
                 message](std::error_code              ec,
                          [[maybe_unused]] std::size_t length) mutable {
                    // asio has now sent the bytes - if there was a problem
                    // an error would be available...
                    if (!ec) {
                        // ... no error, so check if the message header just
                        // sent also has a message body...
                        if (!message->body.empty()) {
                            //auto message = boost::make_shared<Message>();
                            // message->body = qMessagesOut.front().body;
                            // message->message_header =
                            // qMessagesOut.front().message_header;
                            // message->TransactionId = msg.TransactionId;
                            // ...it does, so issue the task to write the body
                            // bytes
                            WriteBody(std::move(message));
                            // qMessagesOut.pop_front();
                        } else {
                            if (messagesent_handler) {
                                messagesent_handler(message, self);
                            }
                            // ...it didnt, so we are done with this message.
                            // Remove it from the outgoing message queue

                            // If the queue is not empty, there are more
                            // messages to send, so make this happen by issuing
                            // the task to send the next header.
                            if (!qMessagesOut.empty()) {
                                WriteHeader();
                            }
                        }
#ifdef VERBOSE_SERVER_DEBUG
                        {
                            std::stringstream ss;
                            ss << "[ Client " << GetId() << " ] "
                               << "Wrote " << length << " bytes. (header)";
                            std::string output = ss.str();
                            std::cout << output << std::endl;
                            output = "";
                            ss.str("");
                        }
#endif
                    } else {
                    // ...asio failed to write the message, we could analyse why
                    // but for now simply assume the connection has died by
                    // closing the socket. When a future attempt to write to
                    // this client fails due to the closed socket, it will be
                    // tidied up.
#ifdef VERBOSE_SERVER_DEBUG
                        {
                            std::stringstream ss;
                            ss << "[ Client " << GetId() << " ] "
                                << "Write Header Fail."
                                << " " << ec.value() << " - " << ec.message();
                            std::string output = ss.str();
                            std::cout << output << std::endl;
                            output = "";
                            ss.str("");
                        }
#endif
                        invalidState = true;
                        Disconnect(true, true, true);
                    }
                });
        }
    }

    // ASYNC - Prime context to write a message body
    void WriteBody(boost::shared_ptr<Message> message)
    {
        auto self = shared_from_this();
        // If this function is called, a header has just been sent, and that
        // header indicated a body existed for this message. Fill a transmission
        // buffer with the body data, and send it!
        async_write(
            socket_, boost::asio::buffer(message->body),
            [this, self, message](std::error_code              ec,
                                  [[maybe_unused]] std::size_t length) mutable {
                if (!ec) {
                    if (messagesent_handler) {
                        messagesent_handler(message, self);
                    }
                    // Sending was successful, so we are done with the
                    // message and remove it from the queue

                    // If the queue still has messages in it, then issue the
                    // task to send the next messages' header.
                    if (!qMessagesOut.empty()) { // TODO FIXME Race condition
                        WriteHeader();
                    }
#ifdef VERBOSE_SERVER_DEBUG
                    {
                        std::stringstream ss;
                        ss << "[ Client " << GetId() << " ] "
                           << "Wrote " << length << " bytes. (body)";
                        std::string output = ss.str();
                        std::cout << output << std::endl;
                        output = "";
                        ss.str("");
                    }
#endif
                } else {
                // Sending failed, see WriteHeader() equivalent for description
                // :P
#ifdef VERBOSE_SERVER_DEBUG
                    {
                        std::stringstream ss;
                        ss << "[ Client " << GetId() << " ] "
                            << "Write Body Fail."
                            << " " << ec.value() << " - " << ec.message();
                        std::string output = ss.str();
                        std::cout << output << std::endl;
                        output = "";
                        ss.str("");
                    }
#endif
                    invalidState = true;
                    Disconnect(true, true, true);
                }
            });
    }

    void AddToIncomingMessageQueue()
    {
        if (message_handler) {
            message_handler(
                boost::make_shared<Message>(std::move(tempInMsg)),
                shared_from_this());
        }

        tempInMsg.body.clear();
        tempInMsg.message_header.size = 0;
    }

    socket_t socket_;

    int connectionId;

    Message tempInMsg;
    Message tempOutMsg;

    ThreadSafeQueue<Message> qMessagesOut;

    boost::signals2::connection c;

    std::atomic_bool invalidState        = false;
    std::atomic_bool alreadyDisconnected = false;
};
