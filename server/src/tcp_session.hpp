#pragma once

#include "command_dispatcher.hpp"

#include <boost/asio.hpp>

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

class TcpSession : public std::enable_shared_from_this<TcpSession> {
public:
    TcpSession(boost::asio::ip::tcp::socket socket, const CommandDispatcher& dispatcher);

    void start();

private:
    void read_header();
    void read_body(std::uint32_t length);

    void enqueue_write(const std::string& framed_response);
    void write_next();

    void close();

    boost::asio::ip::tcp::socket socket_;
    boost::asio::strand<boost::asio::ip::tcp::socket::executor_type> strand_;
    const CommandDispatcher& dispatcher_;

    std::array<std::uint8_t, 4> header_{};
    std::vector<char> body_;
    std::deque<std::string> write_queue_;
    bool close_after_write_ = false;
};
