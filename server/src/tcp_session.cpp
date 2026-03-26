#include "tcp_session.hpp"

#include "tcp_framing.hpp"

#include <nlohmann/json.hpp>

#include <iostream>

TcpSession::TcpSession(boost::asio::ip::tcp::socket socket, const CommandDispatcher& dispatcher)
        : socket_(std::move(socket)),
            strand_(boost::asio::make_strand(socket_.get_executor())),
            dispatcher_(dispatcher) {}

void TcpSession::start() {
    read_header();
}

void TcpSession::read_header() {
    auto self = shared_from_this();
    boost::asio::async_read(
        socket_, boost::asio::buffer(header_),
        boost::asio::bind_executor(strand_, [this, self](const boost::system::error_code& ec, std::size_t) {
            if (ec) {
                close();
                return;
            }

            std::uint32_t length = decode_be_u32(header_.data());
            if (length == 0 || length > kMaxFrameSize) {
                auto response = nlohmann::json{{"status", "error"}, {"message", "Invalid frame size"}};
                close_after_write_ = true;
                enqueue_write(frame_json_payload(response.dump()));
                return;
            }

            read_body(length);
        }));
}

void TcpSession::read_body(std::uint32_t length) {
    body_.assign(length, '\0');

    auto self = shared_from_this();
    boost::asio::async_read(
        socket_, boost::asio::buffer(body_),
        boost::asio::bind_executor(strand_, [this, self](const boost::system::error_code& ec, std::size_t) {
            if (ec) {
                close();
                return;
            }

            nlohmann::json response;
            try {
                const std::string payload(body_.begin(), body_.end());
                auto request = nlohmann::json::parse(payload);
                response = dispatcher_.handle_message(request);
            } catch (const std::exception& ex) {
                response = {
                    {"status", "error"},
                    {"message", std::string("Bad JSON: ") + ex.what()}
                };
            }

            enqueue_write(frame_json_payload(response.dump()));
            read_header();
        }));
}

void TcpSession::enqueue_write(const std::string& framed_response) {
    auto self = shared_from_this();
    boost::asio::post(strand_, [this, self, framed_response] {
        bool write_in_progress = !write_queue_.empty();
        write_queue_.push_back(framed_response);
        if (!write_in_progress) {
            write_next();
        }
    });
}

void TcpSession::write_next() {
    if (write_queue_.empty()) {
        return;
    }

    auto self = shared_from_this();
    boost::asio::async_write(
        socket_, boost::asio::buffer(write_queue_.front()),
        boost::asio::bind_executor(strand_, [this, self](const boost::system::error_code& ec, std::size_t) {
            if (ec) {
                close();
                return;
            }

            write_queue_.pop_front();
            if (!write_queue_.empty()) {
                write_next();
                return;
            }

            if (close_after_write_) {
                close();
            }
        }));
}

void TcpSession::close() {
    boost::system::error_code ignored;
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored);
    socket_.close(ignored);
}
