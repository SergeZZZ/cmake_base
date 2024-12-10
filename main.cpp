#include <array>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/json/src.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <type_traits>

namespace beast = boost::beast;          // From <boost/beast/core.hpp>
namespace websocket = beast::websocket;  // From <boost/beast/websocket.hpp>
namespace net = boost::asio;             // From <boost/asio.hpp>
namespace ssl = net::ssl;                // From <boost/asio/ssl.hpp>
using tcp = net::ip::tcp;                // From <boost/asio/ip/tcp.hpp>
namespace json = boost::json;            // From <boost/json.hpp>

enum class Side : uint8_t { ASK = 0, BID };

enum class Target : uint8_t { PAIR_1 = 0, PAIR_2 };

struct Quote {
  double price;
  Side side;
  Target target;
};

boost::lockfree::queue<Quote, boost::lockfree::capacity<1024>> quotes_queue;

std::array<std::string, 2> pairs = {"usdcusdt", "fdusdusdc"};

void SendOrdersWorker() {
  // connects

  while (1) {
    Quote q;
    if (quotes_queue.pop(q)) {
      std::cout << " sending order: side "
                << (q.side == Side::BID ? "BID" : "ASK")
                << " price: " << q.price << " target: " << pairs[(int)q.target]
                << "\n";
    } else {
      usleep(1);
    }
  }
  // set affinity
}

void ParseAndAdd(const std::string& msg, auto& bids, auto& asks) {
  auto parsed = json::parse(msg);

  auto obj = parsed.as_object();

  // Print update ID
 // std::cout << "Update ID: " << obj["u"] << "\n";

  auto process = [](auto& target, const auto& quote) {
    std::string price = json::value_to<std::string>(quote.at(0));
    std::string quantity = json::value_to<std::string>(quote.at(1));
    //std::cout << "  Price: " << price << ", Quantity: " << quantity << "\n";
    auto double_price = boost::lexical_cast<double>(price);
    auto double_size = boost::lexical_cast<double>(quantity);
    if (double_size == .0) [[unlikely]] {
      target.erase(double_price);
      return;
    }
    target.insert(double_price);
  };

  // Parse and print bids
 // std::cout << "Bids:\n";
  for (const auto& bid : obj["b"].as_array()) {
    process(bids, bid);
  }

  // Parse and print asks
  //std::cout << "Asks:\n";
  for (const auto& ask : obj["a"].as_array()) {
    process(asks, ask);
  }
}

auto ProcessThread(Target target) {
  try {
    std::set<double, std::greater<double>> bids;
    std::set<double, std::less<double>> asks;

    // I/O and SSL context
    net::io_context ioc;
    ssl::context ctx{ssl::context::tlsv12_client};

    // Set default verification paths
    ctx.set_default_verify_paths();

    // Resolve the server
    tcp::resolver resolver{ioc};
    auto const results = resolver.resolve("stream.binance.com", "9443");

    // WebSocket stream over SSL
    websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws{ioc, ctx};

    // Connect to the server
    beast::get_lowest_layer(ws).connect(results);

    // Perform SSL handshake
    ws.next_layer().handshake(ssl::stream_base::client);

    // Perform WebSocket handshake //depth
    const std::string subscribe_str =
        std::string("/ws/") + pairs[(int)target] + "@depth@100ms";
    ws.handshake("stream.binance.com", subscribe_str);

    std::cout << "Connected to Binance WebSocket!" << std::endl;

    // Buffer for reading messages
    beast::flat_buffer buffer;

    // Write a subscription message (if needed, Binance streams don’t require
    // explicit subscription for most use cases)
    // ws.write(net::buffer(R"({"method": "SUBSCRIBE", "params":
    // ["btcusdt@trade"], "id": 1})"));

    // Read messages
    double last_bid = 0;
    double last_ask = 0;
    for (;;) {
      ws.read(buffer);
      std::string message = beast::buffers_to_string(buffer.data());
      ParseAndAdd(message, bids, asks);
      if (!bids.empty()) {
        auto current_bid = bids.begin();
        if (*current_bid != last_bid) {
          Quote q{.price = *current_bid, .side = Side::BID, .target = target};
          quotes_queue.push(q);
          last_bid = *current_bid;
        }
      }

      if (!asks.empty()) {
        auto current_ask = asks.begin();
        if (*current_ask != last_ask) {
          Quote q{.price = *current_ask, .side = Side::ASK, .target = target};
          quotes_queue.push(q);
          last_ask = *current_ask;
        }
      }

      buffer.clear();
    }

    // Gracefully close the WebSocket connection
    ws.close(websocket::close_code::normal);

  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

int main() {
  std::jthread th1([] () {
    ProcessThread(Target::PAIR_1);
  });
  std::jthread th2([] () {
    ProcessThread(Target::PAIR_2);
  });
  std::jthread send([]() {
    SendOrdersWorker();
  });
  return 0;
}