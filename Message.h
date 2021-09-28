#pragma once
#include "prerequisites.h"
#include <span>

template <typename MsgId> struct Message {
    [[nodiscard]] size_t size() const { return body.size(); }
    MessageHeader<MsgId>       message_header{};
    std::vector<unsigned char> body;

    using View = std::span<unsigned char const>;
    using Text = std::string_view;

    std::span<unsigned char> Alloc(size_t n) {
        auto const offset = body.size();
        body.resize(body.size() + n + sizeof(n));

        std::span target = body;
        target           = target.subspan(offset);
        memcpy(target.data(), &n, sizeof(n));

        message_header.size = static_cast<uint32_t>(size());

        return target.subspan(sizeof(n));
    }

    auto ByteFragments() const {
        std::vector<View> fragments;

        View   remain = body;

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

    auto TextFragments() const {
        // Can be optimized, but would duplicate code
        auto raw = ByteFragments();
        std::vector<std::string_view> fragments(raw.size());
        for (size_t i = 0; i < raw.size(); ++i)
            fragments[i] = convert(raw[i]);
        return fragments;
    }

    // Override for iostream - friendly description of message
    friend std::ostream& operator<<(std::ostream& os, const Message<MsgId>& msg)
    {
        os << "ID:" << int(msg.message_header.id)
           << " Size:" << msg.message_header.size;
        return os;
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

  private:
    static constexpr Text convert(View from) {
        return Text(reinterpret_cast<char const*>(from.data()), from.size());
    }
};
