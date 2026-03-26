#pragma once

#include "command_dispatcher.hpp"

#include <boost/asio.hpp>

#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

class TcpServer {
public:
    TcpServer(const boost::asio::ip::address& address,
              unsigned short port,
              std::size_t worker_threads,
              const CommandDispatcher& dispatcher);

    boost::asio::io_context& io_context() { return io_context_; }

    void run();
    void stop();

private:
    void accept_next();

    boost::asio::io_context io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::size_t worker_threads_;
    const CommandDispatcher& dispatcher_;
    std::vector<std::thread> workers_;
};
