#pragma once
#include "prerequisites.h"
#include <span>

namespace protocol {
    namespace v1 {

        template <typename MsgId> struct Message {
            [[nodiscard]] size_t       size() const { return body.size(); }
            MessageHeader<MsgId>       message_header{};
            std::vector<unsigned char> body;

            using View = std::span<unsigned char const>;
            using Text = std::string_view;

            std::span<unsigned char> Alloc(size_t n)
            {
                auto const offset = body.size();
                body.resize(body.size() + n + sizeof(n));

                std::span target = body;
                target           = target.subspan(offset);
                memcpy(target.data(), &n, sizeof(n));

                message_header.size = static_cast<uint32_t>(size());

                return target.subspan(sizeof(n));
            }

            auto ByteFragments() const
            {
                std::vector<View> fragments;

                View remain = body;

                for (size_t n = 0; remain.size() > sizeof(n);) {
                    memcpy(&n, remain.data(), sizeof(n));
                    remain = remain.subspan(sizeof(n));
                    if (remain.size() < n)
                        throw std::runtime_error("Invalid text body");

                    fragments.emplace_back(remain.subspan(0u, n));
                    remain = remain.subspan(n);
                }
                return fragments;
            }

            auto TextFragments() const
            {
                // Can be optimized, but would duplicate code
                auto                          raw = ByteFragments();
                std::vector<std::string_view> fragments(raw.size());
                for (size_t i = 0; i < raw.size(); ++i)
                    fragments[i] = convert(raw[i]);
                return fragments;
            }

            template <typename T> void put(T const& object)
            {
                static_assert(                                //
                    std::is_trivial<T>::value                 //
                        && std::is_standard_layout<T>::value, //
                    "T is not trivial");

                body.resize(sizeof(T));
                std::memcpy(body.data(), &object, sizeof(T));

                message_header.size = (uint32_t)size();
            }

            template <typename T> T get() const
            {
                static_assert(                                //
                    std::is_trivial<T>::value                 //
                        && std::is_standard_layout<T>::value, //
                    "T is not trivial");

                if (sizeof(T) != body.size()) // TODO SEHE
                    throw std::runtime_error("Unexpected message body");

                T object;
                std::memcpy(&object, body.data(), sizeof(T));

                return object;
            }

            size_t raw_size() const {
                return buffer_size(raw_buffers());
            }
          private:
            static constexpr Text convert(View from)
            {
                return Text(reinterpret_cast<char const*>(from.data()),
                            from.size());
            }

            auto raw_buffers() const {
                return std::vector{
                    boost::asio::buffer(&message_header,
                                        sizeof(message_header)),
                    boost::asio::buffer(body),
                };
            }

            template <typename Stream, typename Token>
            friend auto async_read(Stream& as, Message& tempInMsg,
                                   Token&& token)
            {
                using result_type = typename boost::asio::async_result<
                    std::decay_t<Token>, void(error_code, size_t)>;
                using handler_type =
                    typename result_type::completion_handler_type;
                handler_type handler(std::forward<Token>(token));
                result_type  result(handler);

                async_read(
                    as,
                    boost::asio::buffer(&tempInMsg.message_header,
                                        sizeof(tempInMsg.message_header)),
                    [&tempInMsg, &as, h = std::move(handler)] //
                    (error_code ec, std::size_t hlen) {
                        if (!ec) {
                            tempInMsg.body.resize(
                                tempInMsg.message_header.size);
                            async_read( //
                                as, boost::asio::buffer(tempInMsg.body),
                                [hlen, h = std::move(h), &tempInMsg] //
                                (error_code ec, std::size_t blen) {
                                    std::move(h)(ec, hlen + blen);
                                });
                        } else {
                            std::move(h)(ec, hlen);
                        }
                    });

                return result.get();
            }

            template <typename Stream, typename H>
            friend void async_write(Stream& as, Message const& msg, H&& h) {
                return async_write(as, msg.raw_buffers(), std::forward<H>(h));
            }
        };

    } // namespace v1

    enum class MessageTypes : uint32_t {
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

    using MyMessage = v1::Message<MessageTypes>;
} // namespace protocol
