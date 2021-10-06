#pragma once
#include "prerequisites.h"
template <typename TMessage>
class Session
    : public std::enable_shared_from_this<Session<TMessage>> //
{
    using std::enable_shared_from_this<Session>::shared_from_this;
    using socket_t = boost::asio::basic_stream_socket<tcp, Strand>;

  public:
    using SessPtr = std::shared_ptr<Session>;
    using Message = TMessage;
    using MsgPtr  = std::shared_ptr<Message const>;

    enum class owner { server, client };

    socket_t& socket() { return socket_; }
    int       GetId() { return sessionId_; }
    void      SetId(int id) { sessionId_ = id; }

    void run()
    {
        debug << "[ Client " << GetId() << " ] " << "Connected.";

        tcp::no_delay option(true);
        socket_.set_option(option);

        Read();
    }

    void Disconnect(bool cancel, bool shutdown, bool close)
    {
        post(socket_.get_executor(), [=, self = shared_from_this()] {
            self->DoDisconnect(cancel, shutdown, close);
        });
    }

    void Send(MsgPtr msg)
    {
        post( //
            this->socket_.get_executor(),
            [this, msg = std::move(msg), self = shared_from_this()] {
                if (IsInvalid())
                    return;
                outgoing_.push_back(std::move(msg));
                if (outgoing_.size() == 1) // SEHE TODO FIXME Race condition?
                {
                    Write();
                }
            });
    }

    ~Session()
    {
        DoDisconnect(true, true, true);
    }

    using Handler = std::function<void(Message const&, SessPtr const&)>;

    Session(Strand strand, int id, Handler handler)
        : socket_(strand)
        , sessionId_(id)
        , handler_(handler)
    { }

    Session(socket_t&& s, int id, Handler handler)
        : socket_(std::move(s))
        , sessionId_(id)
        , handler_(std::move(handler))
    {
    }

  private:
    void DoDisconnect(bool cancel, bool shutdown, bool close)
    {
        outgoing_.clear();
        invalidState = true;
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

            debug << "[ Client " << GetId() << " ] "
                  << "Disconnected.";
        }
    }

    bool IsInvalid() const
    {
        return !socket_.is_open() || invalidState;
    }

    bool Report([[maybe_unused]] std::string_view caption, error_code ec,
                [[maybe_unused]] auto&&... what)
    {
        if (ec) {
            debug << "[ Client " << GetId() << " ] " << caption << " Failed "
                  << ec.message() << std::endl;
            Disconnect(true, true, true);
            return false;
        } else {
            [[maybe_unused]] auto print_arg = [](auto&& v) {
                debug << " " << v;
                return debug.good();
            };

            debug << "[ Client " << GetId() << " ] " << caption << " Success "
                  << " " << ec.message();
            if ((true && ... && print_arg(what)))
                debug << std::endl;
            return true;
        }
    }

    void Read()
    {
        async_read(
            socket_, incoming_,
            [this, self = shared_from_this()](error_code ec, std::size_t) {
                if (Report("Read Message", ec)) {
                    CommitIncoming();
                    Read(); // Go Back to waiting for header
                }
            });
    }

    void Write()
    {
        if (!outgoing_.empty()) {
            async_write( //
                socket_, *outgoing_.front(),
                [this, self = shared_from_this()](error_code ec, size_t len) {
                    outgoing_.pop_front();
                    if (Report("Write", ec, "Wrote", len, "bytes")) {
                        if (!outgoing_.empty()) {
                            Write(); // TODO FIXME Race condition?
                        }
                    }
                });
        }
    }

    void CommitIncoming()
    {
        if (handler_) {
            handler_(incoming_, shared_from_this());
        }

        incoming_.body.clear();
        incoming_.message_header.size = 0;
    }

    socket_t socket_;

    int sessionId_;
    Handler handler_;

    Message incoming_;
    std::deque<MsgPtr> outgoing_;

    std::atomic_bool invalidState        = false;
    std::atomic_bool alreadyDisconnected = false;
};
