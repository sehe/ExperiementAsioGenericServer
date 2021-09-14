#pragma once
#include "prerequisites.h"
template <typename T>
class Server
{
public:
	Server(boost::asio::io_context& io_context, boost::asio::ip::tcp::endpoint endpoint) :io_context_(io_context), acceptor_(io_context)
	{
		
		this->connectionIds = 10000;
		this->acceptor_.open(endpoint.protocol());
		this->acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
		this->acceptor_.set_option(boost::asio::ip::tcp::acceptor::do_not_route(true));
		this->acceptor_.set_option(boost::asio::ip::tcp::acceptor::keep_alive(false));
		this->acceptor_.set_option(boost::asio::ip::tcp::acceptor::enable_connection_aborted(false));
		this->acceptor_.set_option(boost::asio::ip::tcp::acceptor::linger(false, 3));
		

		
		this->acceptor_.bind(endpoint);
		this->acceptor_.listen();
		this->shutdownBeginning = false;
		this->shutdownCompleted = false;
		std::unique_lock<std::mutex> conMutex(this->connectionMutex);
		this->connections = std::map<int,boost::shared_ptr<Connection<T>>>();
		this->connectionsToRemove = std::vector<int>();
		conMutex.unlock();

		//this->sendingThread = std::thread(&Server::processMsgs, this);

		this->start_accept();

	}

	void interrupt() {
		this->shutdownBeginning = true;
		//this->messageVar.notify_one();
		this->acceptor_.cancel();
		this->acceptor_.close();
		std::unique_lock<std::mutex> conMutex(this->connectionMutex);
		for (const auto& [key, value] : this->connections) {
			value->Disconnect(true,true,true);
		}
		this->connections.clear();
		conMutex.unlock();

		this->shutdownCompleted = true;
	}
	unsigned long long CalculateAverageBacklog()
	{
		
		size_t total = 0;
		size_t count = 0;
		std::unique_lock<std::mutex> conMutex(this->connectionMutex);
		for (const auto& [key, value] : this->connections)
		{
			if (!value->IsInvalid())
			{
				size_t backlog = value->GetSendBacklog();
				total += backlog;
				count++;
			}
		}
		conMutex.unlock();
		if (count > 0)
		{
			auto average = total / count;
			return average;
		}
		return 0;

	}
	void BroadcastMessage(Message<T>& msg)
	{
		std::unique_lock<std::mutex> conMutex(this->connectionMutex);

			
		/*for (const auto& [key, value] : this->connections)
		{
			if (!value->IsInvalid())
			{
				value->Send(msg);
			}
		}*/

		std::for_each(
			std::execution::par_unseq,
			this->connections.begin(),
			this->connections.end(),
			[msg](auto&& item)
			{
				item.second->Send(msg);
			});
		conMutex.unlock();

		//this->bcast(msg);
	}
	~Server() {}
	bool IsShutDownCompleted() {
		return this->shutdownCompleted;
	}

protected:
	// This server class should override thse functions to implement
	// customised functionality

	// Called when a client connects, you can veto the connection by returning false
	virtual bool OnClientConnect(boost::shared_ptr<Connection<T>> client)
	{
		return false;
	}

	// Called when a client appears to have disconnected
	virtual void OnClientDisconnect(boost::shared_ptr<Connection<T>> client)
	{

	}

	// Called when a message arrives
	virtual void OnMessage(OwnedMessage<T>& message)
	{

	}
	virtual void OnMessageSent(OwnedMessage<T>* message)
	{

	}

private:
	void client_disconnected(boost::shared_ptr<Connection<T>> connection)
	{
		OnClientDisconnect(connection);
		removeConnection(connection);
	}
	void client_message(OwnedMessage<T>& message)
	{
		OnMessage(message);
	}
	void message_sent(OwnedMessage<T>* message)
	{
		OnMessageSent(message);
	}
	void start_accept() {
		if (!this->shutdownBeginning && this->acceptor_.is_open())
		{
			int id = this->connectionIds++;
			boost::shared_ptr<Connection<T>> new_connection =
				Connection<T>::create(this->io_context_, id, boost::bind(&Server::client_message, this, boost::placeholders::_1), boost::bind(&Server::message_sent, this, boost::placeholders::_1), boost::bind(&Server::client_disconnected, this, boost::placeholders::_1), qMessagesIn);


			this->acceptor_.async_accept(new_connection->socket(),
				boost::bind(&Server::handle_accept, this, new_connection,
					boost::asio::placeholders::error));
		}
	}



#pragma region Handle the Connection Acceptance
	void handle_accept(boost::shared_ptr<Connection<T>> new_connection,
		const boost::system::error_code& error) {
		if (!error && !this->shutdownBeginning)
		{
			this->start_accept();
			if (OnClientConnect(new_connection))
			{
				boost::signals2::connection c = this->bcast.connect(boost::bind(&Connection<T>::Send, new_connection, boost::placeholders::_1));
				new_connection->accepted(c);
				this->addConnection(std::move(new_connection));
				
			}
			else
			{
#ifdef VERBOSE_SERVER_DEBUG

				std::cout << "[ Client " << new_connection->GetId() << " ]  Connection Denied." << std::endl;
#endif
			}
			
		}
		else if (!this->shutdownBeginning)
		{
			this->start_accept();
#ifdef VERBOSE_SERVER_DEBUG

			std::cout << "[ SERVER ] New connection error: " << error.message() << std::endl;
#endif
			
		}

		
	}
#pragma endregion

#pragma region Add Connection
	void addConnection(boost::shared_ptr<Connection<T>> connection)
	{
		if (!this->shutdownBeginning)
		{
			std::unique_lock<std::mutex> conMutex(this->connectionMutex);
			this->connections.insert(std::make_pair(connection->GetId(),std::move(connection)));
			conMutex.unlock();
		}
	}
#pragma endregion

#pragma region Remove Connection
	void removeConnectionById(int id)
	{
		if (!this->shutdownBeginning)
		{
			std::unique_lock<std::mutex> conMutex(this->connectionMutex);
			this->connections.erase(id);
			conMutex.unlock();


		}
	}
	void removeConnection(boost::shared_ptr<Connection<T>> connection)
	{
		if (!this->shutdownBeginning)
		{
			int id = connection->GetId();
			std::unique_lock<std::mutex> conMutex(this->connectionMutex);
			this->connections.erase(id);
			conMutex.unlock();


		}
	}
	
#pragma endregion

	boost::asio::io_context& io_context_;
	boost::asio::ip::tcp::acceptor acceptor_;
	std::mutex connectionMutex;
	std::map<int,boost::shared_ptr<Connection<T>>> connections;
	std::vector<int> connectionsToRemove;
	volatile bool shutdownBeginning;
	volatile bool shutdownCompleted;
	int connectionIds;
	ThreadSafeQueue<OwnedMessage<T>> qMessagesIn;
	boost::signals2::signal<void(Message<T>&)> bcast;

};

