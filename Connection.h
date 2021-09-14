#pragma once
#include "prerequisites.h"
template <typename T>
class Connection : public boost::enable_shared_from_this<Connection<T>>
{
public:
    enum class owner
    {
        server,
        client
    };

	static boost::shared_ptr<Connection<T>> create(
        boost::asio::io_context& io_context, int id, 
        ClientMessageCallbackType<T> message_handle, ClientMessageSentCallbackType<T> messagesent_handle, ClientDisconnectCallbackType<T> disconnect_handle, 
        ThreadSafeQueue<OwnedMessage<T>>& qIn) {
		return boost::shared_ptr<Connection<T>>(new Connection<T>(io_context,id,message_handle,messagesent_handle,disconnect_handle,qIn));
	}
    //void ModelItem::SubscribeItemCreated(const ModelItemSlot& slot)
    //{
    //    _itemCreated.connect(slot);
    //}

    /*static void RegisterBroadcastSignal(boost::signals2::signal<void(Message<T>&)>& signal, Connection<T>* connection)
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
    }*/

	boost::asio::ip::tcp::socket& socket() {
		return this->socket_;
	}

	void accepted(boost::signals2::connection bcast_connection) {
#ifdef VERBOSE_SERVER_DEBUG
        std::stringstream ss;

        ss << "[ Client " << this->GetId() << " ] " << "Connected.";
        std::string output = ss.str();
        std::cout << output << std::endl;
        output = "";
        ss.str("");
#endif
        this->c = bcast_connection;
        boost::asio::ip::tcp::no_delay option(true);
        this->socket_.set_option(option);
		this->ReadHeader();
	}
 
	

	int GetId() {
        return this->connectionId;
    }
	void Disconnect(bool cancel,bool shutdown, bool close) {
        this->qMessagesOut.clear();
        if (this->socket_.is_open() && !this->alreadyDisconnected)
        {
            this->alreadyDisconnected = true;
            boost::system::error_code ec;
            
            try
            {
                if (cancel)
                    this->socket_.cancel();
            }
            catch (const std::exception& ex)
            {
                std::cout << "[ Client " << this->GetId() << " ] Cancel Exception: " << ex.what() << std::endl;
            }

            try
            {
                if (shutdown)
                    this->socket_.shutdown(boost::asio::socket_base::shutdown_both, ec);

                if (ec)
                {
                    std::cout << " [ Client " << this->GetId() << " ] Shutdown Error: " << ec.value() << " - " << ec.message();
                }
            }
            catch (const std::exception& ex)
            {
                std::cout << "[ Client " << this->GetId() << " ] Shutdown Exception: " << ex.what() << std::endl;
            }

            try
            {
                if (close && !ec)
                    this->socket_.close();
            }
            catch (const std::exception& ex)
            {
                std::cout << "[ Client " << this->GetId() << " ] Close Exception: " << ex.what() << std::endl;
            }

            //this->broadcastSignal.disconnect(boost::bind(&Connection::Send, this);
            //UnRegisterBroadcastSignal(this->broadcastSignal, this);
            this->c.disconnect();
            

#ifdef VERBOSE_SERVER_DEBUG
            std::stringstream ss;
            ss << "[ Client " << this->GetId() << " ] " << "Disconnected.";
            std::string output = ss.str();
            std::cout << output << std::endl;
            output = "";
            ss.str("");
#endif
            if (this->disconnect_handler != nullptr)
            {
                this->disconnect_handler(this->shared_from_this());
            }
        }
        this->invalidState = true;
        
    }
	bool IsInvalid() const {
        return !this->socket_.is_open() || this->invalidState;
    }
    void Send(const Message<T>& msg)
    {

        bool bEmpty = qMessagesOut.empty();
        qMessagesOut.push_back(msg);
        if (bEmpty)
        {
            WriteHeader();
        }
        

    }
    size_t GetSendBacklog()
    {
        if (!IsInvalid())
        {
            return qMessagesOut.count();
        }
        return 0;
    }
    ~Connection() {
        Disconnect(true,true,true);
    }

private:
	Connection(boost::asio::io_context& io_context, int id, ClientMessageCallbackType<T> message_handle, ClientMessageSentCallbackType<T> messagesent_handle, ClientDisconnectCallbackType<T> disconnect_handle, ThreadSafeQueue<OwnedMessage<T>>& qIn) : socket_(io_context), strand_(io_context), qMessagesIn(qIn){
        this->connectionId = id;
        this->tempInMsg = Message<T>();
        this->invalidState = false;

        this->disconnect_handler = disconnect_handle;
        this->message_handler = message_handle;
        this->alreadyDisconnected = false;
        this->messagesent_handler = messagesent_handle;
    }

    ClientDisconnectCallbackType<T> disconnect_handler;
    ClientMessageCallbackType<T> message_handler;
    ClientMessageSentCallbackType<T> messagesent_handler;
    void ReadHeader() {
        auto self = this->shared_from_this();

        boost::asio::async_read(socket_, boost::asio::buffer(&this->tempInMsg.message_header, sizeof(MessageHeader<T>)),
            strand_.wrap([this, self](std::error_code ec, std::size_t length)
                {
                    if (!ec)
                    {
                        // A complete message header has been read, check if this message
                        // has a body to follow...
                        if (this->tempInMsg.message_header.size > 0)
                        {
                            // ...it does, so allocate enough space in the messages' body
                            // vector, and issue asio with the task to read the body.
                            this->tempInMsg.body.resize(this->tempInMsg.message_header.size);
                            this->ReadBody();
                        }
                        else
                        {
                            // it doesn't, so add this bodyless message to the connections
                            // incoming message queue
                            AddToIncomingMessageQueue();
                            //Go back to waiting for header
                            this->ReadHeader();
                        }
                    }
                    else
                    {
                        // Reading form the client went wrong, most likely a disconnect
                        // has occurred. Close the socket and let the system tidy it up later.
#ifdef VERBOSE_SERVER_DEBUG

                        std::stringstream ss;

                        ss << "[ Client " << this->GetId() << " ] " << "Read Header Failed." << " " << ec.value() << " - " << ec.message();
                        std::string output = ss.str();
                        std::cout << output << std::endl;
                        output = "";
                        ss.str("");
#endif
                        this->invalidState = true;
                        this->Disconnect(false,true,true);
                    }
                }));
    }
    void ReadBody() {
        auto self = this->shared_from_this();
        boost::asio::async_read(socket_, boost::asio::buffer(this->tempInMsg.body.data(), this->tempInMsg.body.size()),
            strand_.wrap([this, self](std::error_code ec, std::size_t length)
                {
                    if (!ec)
                    {
                        // ...and they have! The message is now complete, so add
                        // the whole message to incoming queue
                        AddToIncomingMessageQueue();

#ifdef VERBOSE_SERVER_DEBUG

                        std::stringstream ss;

                        ss << "[ Client " << this->GetId() << " ] SENT: " << this->tempInMsg.body.data();

                        std::string output = ss.str();
                        std::cout << output << std::endl;
                        output = "";
                        ss.str("");
#endif
                        //Go Back to waiting for header
                        this->ReadHeader();
                    }
                    else
                    {
                        // As above!
#ifdef VERBOSE_SERVER_DEBUG

                        std::stringstream ss;
                        ss << "[ Client " << this->GetId() << " ] " << "Read Body Failed." << " " << ec.value() << " - " << ec.message();
                        std::string output = ss.str();
                        std::cout << output << std::endl;
                        output = "";
                        ss.str("");
#endif
                        this->invalidState = true;
                        this->Disconnect(false,true,true);

                    }
                }));

    }
    void WriteHeader()
    {
        auto self = this->shared_from_this();
        // If this function is called, we know the outgoing message queue must have 
        // at least one message to send. So allocate a transmission buffer to hold
        // the message, and issue the work - asio, send these bytes
        if(!qMessagesOut.empty())
        { 
            Message<T> msg = qMessagesOut.pop_front();
            Message<T>* message = new Message<T>();
            message->body = msg.body;
            message->message_header = msg.message_header;
            message->TransactionId = msg.TransactionId;
            boost::asio::async_write(socket_, boost::asio::buffer(&message->message_header, sizeof(MessageHeader<T>)),
                strand_.wrap([this, self, message](std::error_code ec, std::size_t length)
                    {
                        // asio has now sent the bytes - if there was a problem
                        // an error would be available...
                        if (!ec)
                        {
                            // ... no error, so check if the message header just sent also
                            // has a message body...
                            if (message->body.size() > 0)
                            {
                                //Message<T>* message = new Message<T>();
                                //message->body = qMessagesOut.front().body;
                                //message->message_header = qMessagesOut.front().message_header;
                                //message->TransactionId = msg.TransactionId;
                                // ...it does, so issue the task to write the body bytes
                                WriteBody(message);
                                //qMessagesOut.pop_front();
                            }
                            else
                            {
                                if (this->messagesent_handler != nullptr)
                                {
                                    OwnedMessage<T>* tempOutMsg = new OwnedMessage<T>();
                                    //tempOutMsg->msg = qMessagesOut.front();
                                    tempOutMsg->msg = *message;
                                    tempOutMsg->remote = this->shared_from_this();
                                    this->messagesent_handler(tempOutMsg);
                                    delete tempOutMsg;
                                    delete message;
                                }
                                // ...it didnt, so we are done with this message. Remove it from 
                                // the outgoing message queue
                                qMessagesOut.pop_front();

                                // If the queue is not empty, there are more messages to send, so
                                // make this happen by issuing the task to send the next header.
                                if (!qMessagesOut.empty())
                                {
                                    WriteHeader();
                                }
                            }
    #ifdef VERBOSE_SERVER_DEBUG

                            std::stringstream ss;
                            ss << "[ Client " << this->GetId() << " ] " << "Wrote " << length << " bytes. (header)";
                            std::string output = ss.str();
                            std::cout << output << std::endl;
                            output = "";
                            ss.str("");
    #endif
                            }
                        else
                        {
                            // ...asio failed to write the message, we could analyse why but 
                            // for now simply assume the connection has died by closing the
                            // socket. When a future attempt to write to this client fails due
                            // to the closed socket, it will be tidied up.
    #ifdef VERBOSE_SERVER_DEBUG

                            std::stringstream ss;
                            ss << "[ Client " << this->GetId() << " ] " << "Write Header Fail." << " " << ec.value() << " - " << ec.message();
                            std::string output = ss.str();
                            std::cout << output << std::endl;
                            output = "";
                            ss.str("");
    #endif
                            this->invalidState = true;
                            this->Disconnect(true, true, true);
                        }

                        }));
        }
    }

    // ASYNC - Prime context to write a message body
   void WriteBody(Message<T>* message)
    {
        auto self = this->shared_from_this();
        // If this function is called, a header has just been sent, and that header
        // indicated a body existed for this message. Fill a transmission buffer
        // with the body data, and send it!
        boost::asio::async_write(socket_, boost::asio::buffer(message->body.data(), message->body.size()),
            strand_.wrap([this, self,message](std::error_code ec, std::size_t length)
            {
                if (!ec)
                {
                    if (this->messagesent_handler != nullptr)
                    {
                        
                        OwnedMessage<T>* tempOutMsg = new OwnedMessage<T>();
                        tempOutMsg->msg = *message;
                        tempOutMsg->remote = this->shared_from_this();
                        this->messagesent_handler(tempOutMsg);
                        delete tempOutMsg;
                        delete message;
                        
                    }
                    // Sending was successful, so we are done with the message
                    // and remove it from the queue
                    //qMessagesOut.pop_front();

                    // If the queue still has messages in it, then issue the task to 
                    // send the next messages' header.
                    if (!qMessagesOut.empty())
                    {
                        WriteHeader();
                    }
#ifdef VERBOSE_SERVER_DEBUG

                    std::stringstream ss;
                    ss << "[ Client " << this->GetId() << " ] " << "Wrote " << length << " bytes. (body)";
                    std::string output = ss.str();
                    std::cout << output << std::endl;
                    output = "";
                    ss.str("");
#endif
                }
                else
                {
                    // Sending failed, see WriteHeader() equivalent for description :P
#ifdef VERBOSE_SERVER_DEBUG

                    std::stringstream ss;
                    ss << "[ Client " << this->GetId() << " ] " << "Write Body Fail." << " " << ec.value() << " - " << ec.message();
                    std::string output = ss.str();
                    std::cout << output << std::endl;
                    output = "";
                    ss.str("");
#endif
                    this->invalidState = true;
                    this->Disconnect(true,true,true);
                }
            }));
    }
    void AddToIncomingMessageQueue()
    {

        
        OwnedMessage<T> msg{ this->shared_from_this(), tempInMsg };
        //qMessagesIn.push_back(msg);

        if (this->message_handler != nullptr)
        {
            this->message_handler(msg);
        }

        msg.msg.body.clear();
        msg.msg.message_header.size = 0;
        tempInMsg.body.clear();
        tempInMsg.message_header.size = 0;
        
    }

	boost::asio::ip::tcp::socket socket_;
    boost::asio::io_context::strand strand_;

	int connectionId;


	Message<T> tempInMsg;
    Message<T> tempOutMsg;

    ThreadSafeQueue<OwnedMessage<T>>& qMessagesIn;
    ThreadSafeQueue<Message<T>> qMessagesOut;
        

    boost::signals2::connection c;

	bool invalidState;
    bool alreadyDisconnected;
};

