#pragma once
#include "prerequisites.h"
template <typename MsgId, typename Executor>
class Connection
    : public boost::enable_shared_from_this<Connection<MsgId, Executor>> {
  public:
    using ConnPtr = boost::shared_ptr<Connection<MsgId, Executor>>;

    struct OwnedMessage {
        ConnPtr remote = nullptr;
        Message<MsgId> msg;

        /*
         * // Again, a friendly string maker
         * friend std::ostream& operator<<(std::ostream&       os,
         *                                 const OwnedMessage& msg)
         * { return os << msg.msg; }
         */
    };

    using ClientDisconnectCallbackType  = boost::function<void(ConnPtr const&)>;
    using ClientMessageCallbackType     = boost::function<void(OwnedMessage&)>;
    using ClientMessageSentCallbackType = boost::function<void(OwnedMessage*)>;

    enum class owner { server, client };

    static ConnPtr
    create(Executor executor, int id,
           ClientMessageCallbackType      message_handle,
           ClientMessageSentCallbackType  messagesent_handle,
           ClientDisconnectCallbackType   disconnect_handle,
           ThreadSafeQueue<OwnedMessage>& qIn)
    {
        // should be make_shared, but not posisble due to private constructor
        return ConnPtr(new Connection<MsgId, Executor>(
            executor, id, message_handle, messagesent_handle, disconnect_handle,
            qIn));
    }
    // void ModelItem::SubscribeItemCreated(const ModelItemSlot& slot)
    //{
    //    _itemCreated.connect(slot);
    //}

#if 0
    RegisterBroadcastSignal(boost::signals2::signal<void(Message<T>&)>& signal,
    Connection<T>* connection)
    {
        //bind(&ModelItem::OnItemCreated, listener, _1)
        RegisterSignals(signal,boost::bind(&Connection<T>::Send, connection, boost::placeholders::_1));
    }
    void UnRegisterBroadcastSignal(Connection<T>* connection)
    {
        //signal.disconnect(boost::bind(&Connection::Send, connection, boost::placeholders::_1));
    }
    static void RegisterSignals(boost::signals2::signal<void(Message<T>&)>& signal,boost::signals2::signal<void(Message<T>&)>::slot_type& slot)
    {
        signal.connect(slot);
    }
#endif

    tcp::socket& socket()
    {
        return this->socket_;
    }

    void accepted(boost::signals2::connection bcast_connection)
    {
#ifdef VERBOSE_SERVER_DEBUG
        std::stringstream ss;

        ss << "[ Client " << this->GetId() << " ] "
           << "Connected.";
        std::string output = ss.str();
        std::cout << output << std::endl;
        output = "";
        ss.str("");
#endif
        this->c = bcast_connection;
        tcp::no_delay option(true);
        this->socket_.set_option(option);
        this->ReadHeader();
    }

    int GetId()
    {
        return this->connectionId;
    }
    void Disconnect(bool cancel, bool shutdown, bool close)
    {
        this->qMessagesOut.clear();
        if (this->socket_.is_open() && !this->alreadyDisconnected) {
            this->alreadyDisconnected = true;
            boost::system::error_code ec;

            try {
                if (cancel) {
                    this->socket_.cancel();
                }
            } catch (const std::exception& ex) {
                std::cout << "[ Client " << this->GetId()
                          << " ] Cancel Exception: " << ex.what() << std::endl;
            }

            try {
                if (shutdown) {
                    this->socket_.shutdown(
                        boost::asio::socket_base::shutdown_both, ec);
                }

                if (ec) {
                    std::cout << " [ Client " << this->GetId()
                              << " ] Shutdown Error: " << ec.value() << " - "
                              << ec.message();
                }
            } catch (const std::exception& ex) {
                std::cout << "[ Client " << this->GetId()
                          << " ] Shutdown Exception: " << ex.what()
                          << std::endl;
            }

            try {
                if (close && !ec) {
                    this->socket_.close();
                }
            } catch (const std::exception& ex) {
                std::cout << "[ Client " << this->GetId()
                          << " ] Close Exception: " << ex.what() << std::endl;
            }

            // this->broadcastSignal.disconnect(boost::bind(&Connection::Send,
            // this); UnRegisterBroadcastSignal(this->broadcastSignal, this);
            this->c.disconnect();

#ifdef VERBOSE_SERVER_DEBUG
            std::stringstream ss;
            ss << "[ Client " << this->GetId() << " ] "
               << "Disconnected.";
            std::string output = ss.str();
            std::cout << output << std::endl;
            output = "";
            ss.str("");
#endif
            if (disconnect_handler) {
                this->disconnect_handler(this->shared_from_this());
            }
        }
        this->invalidState = true;
    }
    bool IsInvalid() const
    {
        return !this->socket_.is_open() || this->invalidState;
    }
    void Send(const Message<MsgId>& msg)
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
               ClientDisconnectCallbackType   disconnect_handle,
               ThreadSafeQueue<OwnedMessage>& qIn)
        : disconnect_handler(disconnect_handle)
        , message_handler(message_handle)
        , messagesent_handler(messagesent_handle)
        , strand_(executor)
        , socket_(strand_)
        , connectionId(id)
        , qMessagesIn(qIn)
    { }

    ClientDisconnectCallbackType  disconnect_handler;
    ClientMessageCallbackType     message_handler;
    ClientMessageSentCallbackType messagesent_handler;

    void ReadHeader()
    {
        auto self = this->shared_from_this();

        async_read(socket_,
                   boost::asio::buffer(&this->tempInMsg.message_header,
                                       sizeof(MessageHeader<MsgId>)),
                   [this, self](std::error_code ec, std::size_t /*length*/) {
                       if (!ec) {
                           // A complete message header has been read, check if
                           // this message has a body to follow...
                           if (this->tempInMsg.message_header.size > 0) {
                               // ...it does, so allocate enough space in the
                               // messages' body vector, and issue asio with the
                               // task to read the body.
                               this->tempInMsg.body.resize(
                                   this->tempInMsg.message_header.size);
                               this->ReadBody();
                           } else {
                               // it doesn't, so add this bodyless message to
                               // the connections incoming message queue
                               AddToIncomingMessageQueue();
                               // Go back to waiting for header
                               this->ReadHeader();
                           }
                       } else {
                // Reading form the client went wrong, most likely a disconnect
                // has occurred. Close the socket and let the system tidy it up
                // later.
#ifdef VERBOSE_SERVER_DEBUG

                           std::stringstream ss;

                           ss << "[ Client " << this->GetId() << " ] "
                              << "Read Header Failed."
                              << " " << ec.value() << " - " << ec.message();
                           std::string output = ss.str();
                           std::cout << output << std::endl;
                           output = "";
                           ss.str("");
#endif
                           this->invalidState = true;
                           this->Disconnect(false, true, true);
                       }
                   });
    }

    void ReadBody()
    {
        auto self = this->shared_from_this();
        async_read(socket_,
                   boost::asio::buffer(this->tempInMsg.body.data(),
                                       this->tempInMsg.body.size()),
                   [this, self](std::error_code ec, std::size_t /*length*/) {
                       if (!ec) {
                           // ...and they have! The message is now complete, so
                           // add the whole message to incoming queue
                           AddToIncomingMessageQueue();

#ifdef VERBOSE_SERVER_DEBUG

                           std::stringstream ss;

                           ss << "[ Client " << this->GetId()
                              << " ] SENT: " << this->tempInMsg.body.data();

                           std::string output = ss.str();
                           std::cout << output << std::endl;
                           output = "";
                           ss.str("");
#endif
                           // Go Back to waiting for header
                           this->ReadHeader();
                       } else {
                // As above!
#ifdef VERBOSE_SERVER_DEBUG

                           std::stringstream ss;
                           ss << "[ Client " << this->GetId() << " ] "
                              << "Read Body Failed."
                              << " " << ec.value() << " - " << ec.message();
                           std::string output = ss.str();
                           std::cout << output << std::endl;
                           output = "";
                           ss.str("");
#endif
                           this->invalidState = true;
                           this->Disconnect(false, true, true);
                       }
                   });
    }

    void WriteHeader()
    {
        auto self = this->shared_from_this();
        // If this function is called, we know the outgoing message queue must
        // have at least one message to send. So allocate a transmission buffer
        // to hold the message, and issue the work - asio, send these bytes
        if (!qMessagesOut.empty()) {
            auto message =
                std::make_shared<Message<MsgId>>(qMessagesOut.pop_front());
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
                            auto message = std::make_shared<Message<MsgId>>();
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
                                OwnedMessage tempOutMsg{
                                    this->shared_from_this(),
                                    std::move(*message),
                                };
                                this->messagesent_handler(&tempOutMsg);
                                message.reset();
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

                        std::stringstream ss;
                        ss << "[ Client " << this->GetId() << " ] "
                           << "Wrote " << length << " bytes. (header)";
                        std::string output = ss.str();
                        std::cout << output << std::endl;
                        output = "";
                        ss.str("");
#endif
                    } else {
                    // ...asio failed to write the message, we could analyse why
                    // but for now simply assume the connection has died by
                    // closing the socket. When a future attempt to write to
                    // this client fails due to the closed socket, it will be
                    // tidied up.
#ifdef VERBOSE_SERVER_DEBUG

                        std::stringstream ss;
                        ss << "[ Client " << this->GetId() << " ] "
                           << "Write Header Fail."
                           << " " << ec.value() << " - " << ec.message();
                        std::string output = ss.str();
                        std::cout << output << std::endl;
                        output = "";
                        ss.str("");
#endif
                        this->invalidState = true;
                        this->Disconnect(true, true, true);
                    }
                });
        }
    }

    // ASYNC - Prime context to write a message body
    void WriteBody(std::shared_ptr<Message<MsgId>> message)
    {
        auto self = this->shared_from_this();
        // If this function is called, a header has just been sent, and that
        // header indicated a body existed for this message. Fill a transmission
        // buffer with the body data, and send it!
        async_write(
            socket_, boost::asio::buffer(message->body),
            [this, self, message](std::error_code              ec,
                                  [[maybe_unused]] std::size_t length) mutable {
                if (!ec) {
                    if (messagesent_handler) {
                        OwnedMessage tempOutMsg{
                            this->shared_from_this(),
                            std::move(*message),
                        };
                        this->messagesent_handler(&tempOutMsg);
                        message.reset();
                    }
                    // Sending was successful, so we are done with the
                    // message and remove it from the queue

                    // If the queue still has messages in it, then issue the
                    // task to send the next messages' header.
                    if (!qMessagesOut.empty()) { // TODO FIXME Race condition
                        WriteHeader();
                    }
#ifdef VERBOSE_SERVER_DEBUG

                    std::stringstream ss;
                    ss << "[ Client " << this->GetId() << " ] "
                       << "Wrote " << length << " bytes. (body)";
                    std::string output = ss.str();
                    std::cout << output << std::endl;
                    output = "";
                    ss.str("");
#endif
                } else {
                // Sending failed, see WriteHeader() equivalent for description
                // :P
#ifdef VERBOSE_SERVER_DEBUG

                    std::stringstream ss;
                    ss << "[ Client " << this->GetId() << " ] "
                       << "Write Body Fail."
                       << " " << ec.value() << " - " << ec.message();
                    std::string output = ss.str();
                    std::cout << output << std::endl;
                    output = "";
                    ss.str("");
#endif
                    this->invalidState = true;
                    this->Disconnect(true, true, true);
                }
            });
    }

    void AddToIncomingMessageQueue()
    {
        OwnedMessage msg{this->shared_from_this(), tempInMsg};
        // qMessagesIn.push_back(msg);

        if (message_handler) {
            this->message_handler(msg);
        }

        msg.msg.body.clear();
        msg.msg.message_header.size = 0;
        tempInMsg.body.clear();
        tempInMsg.message_header.size = 0;
    }

    boost::asio::strand<Executor> strand_;
    tcp::socket                   socket_;

    int connectionId;

    Message<MsgId> tempInMsg;
    Message<MsgId> tempOutMsg;

    ThreadSafeQueue<OwnedMessage>&  qMessagesIn;
    ThreadSafeQueue<Message<MsgId>> qMessagesOut;

    boost::signals2::connection c;

    bool invalidState        = false;
    bool alreadyDisconnected = false;
};
