#pragma once
#include "prerequisites.h"
template <typename MsgId> struct Message {

    [[nodiscard]] size_t size() const
    {
        return this->body.size();
    }
    MessageHeader<MsgId>       message_header{};
    std::vector<unsigned char> body;
    std::string                TransactionId;
    void                       Append(const char* data)
    {
        size_t i           = body.size();
        size_t data_length = strlen(data);
        body.resize(body.size() + sizeof(size_t) + data_length);
        std::memcpy(body.data() + i, &data_length, sizeof(size_t));
        std::memcpy(body.data() + sizeof(size_t) + i, data, data_length);
        message_header.size = (uint32_t)size();
    }
    void Append(std::string& data)
    {
        const char* data_cStr = data.c_str();
        Append(data_cStr);
    }
    void GetString(size_t offset, std::string& dst)
    {
        if (body.size() >= offset + sizeof(size_t)) {
            size_t length;
            std::memcpy(&length, body.data() + offset, sizeof(size_t));
            if (body.size() >= offset + sizeof(size_t) + length) {
                dst.assign(length, body[sizeof(size_t) + offset]);
            }
        }

        std::string error = "ERROR";
        dst.assign(error);
    }
    std::string GetString(size_t offset)
    {
        if (body.size() >= offset + sizeof(size_t)) {
            size_t length;
            std::memcpy(&length, body.data() + offset, sizeof(size_t));
            if (body.size() >= offset + sizeof(size_t) + length) {
                std::string s = std::string(
                    body.begin() + offset + sizeof(size_t),
                    body.begin() + offset + sizeof(size_t) + length); // WORKS

                return s;
            }
        }
        return "ERROR";
    }

    // Override for std::cout compatibility - produces friendly description of
    // message
    friend std::ostream& operator<<(std::ostream& os, const Message<MsgId>& msg)
    {
        os << "ID:" << int(msg.message_header.id)
           << " Size:" << msg.message_header.size;
        return os;
    }

    // Convenience Operator overloads - These allow us to add and remove stuff
    // from the body vector as if it were a stack, so First in, Last Out. These
    // are a template in itself, because we dont know what data type the user is
    // pushing or popping, so lets allow them all. NOTE: It assumes the data
    // type is fundamentally Plain Old Data (POD). TLDR: Serialise & Deserialise
    // into/from a vector

    // Pushes any POD-like data into the message buffer
    template <typename DataType>
    friend Message<MsgId>& operator<<(Message<MsgId>& msg, const DataType& data)
    {
        // Check that the type of the data being pushed is trivially copyable
        static_assert(std::is_standard_layout<DataType>::value,
                      "Data is too complex to be pushed into vector");

        // Cache current size of vector, as this will be the point we insert the
        // data
        size_t i = msg.body.size();

        // Resize the vector by the size of the data being pushed
        msg.body.resize(msg.body.size() + sizeof(DataType));

        // Physically copy the data into the newly allocated vector space
        std::memcpy(msg.body.data() + i, &data, sizeof(DataType));

        // Recalculate the message size
        msg.message_header.size = (uint32_t)msg.size();

        // Return the target message so it can be "chained"
        return msg;
    }

    // Pulls any POD-like data form the message buffer
    template <typename DataType>
    friend Message<MsgId>& operator>>(Message<MsgId>& msg, DataType& data)
    {
        // Check that the type of the data being pushed is trivially copyable
        static_assert(std::is_standard_layout<DataType>::value,
                      "Data is too complex to be pulled from vector");

        // Cache the location towards the end of the vector where the pulled
        // data starts
        size_t i = msg.body.size() - sizeof(DataType);

        // Physically copy the data from the vector into the user variable
        std::memcpy(&data, msg.body.data() + i, sizeof(DataType));

        // Shrink the vector to remove read bytes, and reset end position
        msg.body.resize(i);

        // Recalculate the message size
        msg.message_header.size = msg.size();

        // Return the target message so it can be "chained"
        return msg;
    }
};
