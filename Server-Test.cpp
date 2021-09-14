// Server-Test.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "prerequisites.h"

boost::asio::io_context context;
int messageCount;
boost::thread_group tg;
enum class MessageTypes : uint32_t
{
    ServerAccept,
    ServerDeny,
    ServerPing,
    MessageAll,
    SendText,
    ServerMessage,
    ServerMessage1,
    ServerMessage2,
    ServerMessage3,
    ServerMessage4,
    ServerMessage5,
    ServerMessage6,
    ServerMessage7,
    ServerMessage8,
    ServerMessage9,
};
class MyServer : public Server<MessageTypes>
{
public:
    MyServer(boost::asio::io_context& context, boost::asio::ip::tcp::endpoint ep) : Server<MessageTypes>(context,ep)
    {
 
    }
    virtual void OnClientDisconnect(boost::shared_ptr<Connection<MessageTypes>> client)
    {
        std::cout << "[ Client  " << client->GetId() << " ] Disconnected" << std::endl;
    }
    virtual bool OnClientConnect(boost::shared_ptr<Connection<MessageTypes>> client)
    {
        std::cout << "[ Client  " << client->GetId() << " ] Connected" << std::endl;
        Message<MessageTypes> msg;
        msg.message_header.id = MessageTypes::ServerAccept;
        msg << client->GetId();
        client->Send(msg);
        return true;
    }
    virtual void OnMessage(OwnedMessage<MessageTypes>& msg)
    {
        std::cout << "[ Client " << msg.remote->GetId() << " ] ";
        if (msg.msg.message_header.id == MessageTypes::SendText)
        {
            std::string message= msg.msg.GetString(0);
            std::cout << " Received Message" << std::endl;
            message = "";
            msg.remote->Send(msg.msg);//fire it back to the client
        }
    }
    virtual void OnMessageSent(OwnedMessage<MessageTypes>* msg)
    {
        //std::cout << "[ Client " << msg->remote->GetId() << " ] ";
        //std::cout << " Sent Message" << std::endl;
    }
};
MyServer* srv;
void RunAsioContext()
{
    std::cout << "IO Thread Starting - Id:  " << boost::this_thread::get_id() << std::endl;
    context.run();
    std::cout << "IO Thread finished - Id: " << boost::this_thread::get_id() << std::endl;
}
long long previous_time = 0;
long long highest_time = 0;
int total_thread_count = 0;
size_t max_thread_count = 1;
boost::asio::io_service io_service;
boost::posix_time::seconds interval(1);
boost::posix_time::seconds interval2(5);
boost::asio::deadline_timer timer(io_service, interval);
volatile bool stop = false;
void timedBcast(const boost::system::error_code& e)
{
    //std::cout << "Beginning BCAST..." << std::endl;
    std::chrono::high_resolution_clock::time_point t2 =
        std::chrono::high_resolution_clock::now();
    //while (!context.stopped() && !io_service.stopped() && !e && !stop)
    if (!context.stopped() && !io_service.stopped() && !e && !stop)
    {
      

        if (srv != nullptr)
        {
            if (messageCount >= 1000)
            {
                messageCount = 0;
            }
            
            //std::cout << "SENDING BCAST" << std::endl;
            //std::string message = "HELLO WORLD TO ALL BROADCAST! ";
            //message += std::to_string(messageCount++);
            
            int msg_sz = rand() % 102400 + 81920;
            std::string message = std::string(msg_sz, 'a');
            message += " ";
            message += std::to_string(messageCount++);
            Message<MessageTypes> msg;
            msg.message_header.id = MessageTypes::MessageAll;
            msg.Append(message);
            msg.TransactionId = "Broadcast";
            std::chrono::high_resolution_clock::time_point t1 =
                std::chrono::high_resolution_clock::now();
            srv->BroadcastMessage(msg);
            long long time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - t1).count();
            
            long long time2 = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - t2).count();
            if (time != previous_time && time > 6)
            {
                //timer += time - 6;
                std::cout << "Broadcast took " << time << "ms | " << time2 << "ms" << std::endl;
                previous_time = time;
                


            }
            int time_expire = 100 - time2 - 1;
            if (time_expire <= 5)
            {
                time_expire = 50;
            }
            // Reschedule the timer for 1 second in the future:
            timer.expires_from_now(boost::posix_time::milliseconds(time_expire));
            // Posts the timer event
            timer.async_wait(timedBcast);
            std::cout << "BROADCAST" << std::endl;

            
        }
    }
    //std::cout << "Exited bcast" << std::endl;
    //timer.cancel();
}
void monitorBuffer()
{
    int delay_ms = 1000;
    while (!context.stopped())
    {
        boost::this_thread::sleep_for(boost::chrono::milliseconds(delay_ms));
        if (srv != nullptr)
        {
            auto avg = srv->CalculateAverageBacklog();
            int num_threads = avg / 5 + avg % 5;
            for (int i = 0; i < num_threads; i++)
            {
                tg.create_thread(&RunAsioContext);
            }
            if (num_threads > 0)
            {
                std::cout << "ADDING MORE THREADS " << num_threads;
            }
        }
    }
}
int main()
{
    messageCount = 1;
    context.reset();
    io_service.reset();
    srand(time(NULL));
    boost::asio::ip::tcp::endpoint ep = boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v4::loopback(), 40000);

    srv = new MyServer(context, ep);

    

    for (unsigned i = 0; i < boost::thread::hardware_concurrency(); ++i)
        tg.create_thread(&RunAsioContext);
    
    std::cout << "Hello World!\n";
    timer.expires_from_now(boost::posix_time::milliseconds(5000));
    timer.async_wait(timedBcast);
    boost::thread t1([] {io_service.run(); });
    t1.detach();


    //boost::thread t2(&monitorBuffer);
    //t2.detach();

    std::string str;
    std::getline(std::cin, str);

    srv->interrupt();
    //t1.interrupt();
    stop = true;
    timer.cancel();
    io_service.stop();
    timer.cancel();
    
    context.stop();
 
    tg.join_all();
    if (t1.joinable())
    {
        t1.join();
    }
    //if (t2.joinable())
    //{
    //    t2.join();
    //}
    str="";
    std::getline(std::cin, str);
    delete srv;
    return 0;
    
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
