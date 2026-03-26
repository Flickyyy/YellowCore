#include "auth_service.hpp"
#include "bank_service.hpp"
#include "command_dispatcher.hpp"
#include "price_engine.hpp"
#include "stock_service.hpp"
#include "tcp_server.hpp"

#include <boost/asio.hpp>

#include <cstdlib>
#include <iostream>
#include <csignal>
#include <string>

namespace {

std::size_t parse_threads(const char* value) {
    if (!value) return 4;
    auto parsed = std::strtoul(value, nullptr, 10);
    return parsed == 0 ? 4 : parsed;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string host = argc > 1 ? argv[1] : "0.0.0.0";
    const unsigned short port = static_cast<unsigned short>(argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 9090);
    const std::size_t threads = argc > 3 ? parse_threads(argv[3]) : 4;

    try {
        AuthService auth;
        BankService bank;
        PriceEngine prices;
        StockService stock(bank, prices);
        CommandDispatcher dispatcher(auth, bank, stock, prices);

        prices.start();

        auto address = boost::asio::ip::make_address(host);
        TcpServer server(address, port, threads, dispatcher);
        boost::asio::signal_set signals(server.io_context(), SIGINT, SIGTERM);
        signals.async_wait([&](const boost::system::error_code&, int) {
            server.stop();
        });

        std::cout << "YellowCore server listening on " << host << ':' << port
                  << " with " << threads << " worker threads" << std::endl;

        server.run();
        prices.stop();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Server startup failed: " << ex.what() << std::endl;
        return 1;
    }
}
