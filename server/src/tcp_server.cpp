#include "tcp_server.hpp"

#include "tcp_session.hpp"

#include <iostream>

TcpServer::TcpServer(const boost::asio::ip::address& address,
                     unsigned short port,
                     std::size_t worker_threads,
                     const CommandDispatcher& dispatcher)
    : io_context_(static_cast<int>(worker_threads > 0 ? worker_threads : 1)),
      acceptor_(io_context_),
      worker_threads_(worker_threads > 0 ? worker_threads : 1),
      dispatcher_(dispatcher) {
    boost::asio::ip::tcp::endpoint endpoint(address, port);

    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(boost::asio::socket_base::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen(boost::asio::socket_base::max_listen_connections);
}

void TcpServer::run() {
    accept_next();

    for (std::size_t i = 1; i < worker_threads_; ++i) {
        workers_.emplace_back([this] { io_context_.run(); });
    }

    io_context_.run();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void TcpServer::stop() {
    boost::system::error_code ignored;
    acceptor_.close(ignored);
    io_context_.stop();
}

void TcpServer::accept_next() {
    acceptor_.async_accept([this](const boost::system::error_code& ec, boost::asio::ip::tcp::socket socket) {
        if (!ec) {
            std::make_shared<TcpSession>(std::move(socket), dispatcher_)->start();
        }

        if (acceptor_.is_open()) {
            accept_next();
        }
    });
}
