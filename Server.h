#pragma once
#include "prerequisites.h"
#include <future>

namespace networking {

    struct listener {
        using acceptor_t = boost::asio::basic_socket_acceptor<tcp, Strand>;
        using socket_t   = boost::asio::basic_stream_socket<tcp, Strand>;
        using action_t   = std::function<void(socket_t&&)>;

        acceptor_t acceptor_;
        action_t   action_;

        listener(Strand strand, tcp::endpoint ep, action_t action)
            : acceptor_(strand)
            , action_(std::move(action))
        {
            acceptor_.open(ep.protocol());
            acceptor_.set_option(tcp::acceptor::reuse_address(true));
            acceptor_.set_option(tcp::acceptor::do_not_route(true));
            acceptor_.set_option(tcp::acceptor::keep_alive(false));
            acceptor_.set_option(tcp::acceptor::enable_connection_aborted(false));
            acceptor_.set_option(tcp::acceptor::linger(false, 3));

            acceptor_.bind(ep);
            acceptor_.listen();
        }

        void start() { accept_loop(); }
        void stop() {
            post(acceptor_.get_executor(), [this] {
                acceptor_.cancel();
                acceptor_.close();
            });
        }

      private:
        void accept_loop() {
            acceptor_.async_accept(                                         //
                make_strand(acceptor_.get_executor().get_inner_executor()), //
                [this](error_code ec, socket_t&& s) {
                    if (!ec) {
                        action_(std::move(s));
                        accept_loop();
                    } else {
                        debug << "[ Listener ] " << ec.message() << std::endl;
                    }
                });
        }
    };

    template <typename Factory>
    struct session_manager {
        using session_t = decltype(Factory{}(std::declval<listener::socket_t>(), 0));
        using handle_t  = decltype(std::weak_ptr(session_t{}));

        session_manager(Strand s, Factory factory)
            : strand_(s)
            , factory_(factory) { }

        Strand                  strand_;
        Factory                 factory_;
        int                     sessionId_{10'000};
        std::map<int, handle_t> sessions_;

        void shutdown() {
            for_each_session([](auto conn) { conn->Disconnect(true, true, true); });
        }

        void for_each_handle(auto operation) const {
            post(strand_, [this, f = std::move(operation)] {
                for (const auto& [id, handle] : sessions_)
                    f(handle);
            });
        }

        void for_each_session(auto operation) const {
            post(strand_, [this, f = std::move(operation)] {
                for (const auto& [id, handle] : sessions_)
                    if (auto sess = handle.lock())
                        f(sess);
            });
        }

        size_t Count()
        {
            return post(strand_, std::packaged_task<size_t()>([this] {
                            garbage_collect();
                            return sessions_.size();
                        }))
                .get();
        }

        listener::action_t enter() {
            return [this](listener::socket_t&& s) {
                auto id = sessionId_++;
                if (session_t sess = factory_(std::move(s), id)) {
                    debug << "[ Session " << id << " ] Accepted." << std::endl;
                    sessions_.emplace(id, std::move(sess));
                    garbage_collect();
                } else {
                    debug << "[ Session " << id << " ] Denied." << std::endl;
                }
            };
        }

      private:
        void garbage_collect()
        {
            std::erase_if(sessions_,
                          [](auto& kvp) { return kvp.second.expired(); });
        }
    };
} // namespace networking
