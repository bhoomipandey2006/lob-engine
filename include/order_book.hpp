// order_book.hpp
//
// A price-time priority limit order book and matching engine.
//
// Design notes (read this before touching the .cpp):
//   - Prices are stored as int64_t "ticks" (price * 100 for 2 decimal paise
//     precision), never as double. Comparing doubles for price equality in
//     a matching engine is a real bug class -- avoid it entirely.
//   - Bids are kept in descending price order (best bid = highest price).
//     Asks are kept in ascending price order (best ask = lowest price).
//     At each price level, orders are FIFO (time priority) via std::deque.
//   - OrderId -> location index gives O(1) cancellation instead of O(n)
//     scanning, which matters once you're benchmarking this.
//
// This is deliberately a *single-threaded, in-process* engine. Making it
// lock-free / multi-threaded is a legitimate "Phase 5" extension once the
// core logic is correct and benchmarked -- don't reach for concurrency
// before you can prove the single-threaded version is correct and fast.

#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <optional>

namespace lob {

using OrderId = uint64_t;
using Price   = int64_t;   // ticks: real_price * 100
using Qty     = int64_t;
using Clock   = std::chrono::steady_clock;

enum class Side { Buy, Sell };

struct Order {
    OrderId id;
    Side side;
    Price price;
    Qty qty;          // remaining quantity
    uint64_t seq;      // insertion sequence number, for FIFO tie-break
};

struct Trade {
    OrderId resting_id;
    OrderId incoming_id;
    Price price;
    Qty qty;
    uint64_t seq;
};

// Result of submitting a new order: any trades it caused, plus whether
// (and how much of) it rests on the book afterward.
struct SubmitResult {
    std::vector<Trade> trades;
    Qty remaining_qty = 0;   // 0 if fully filled
    bool resting = false;
};

class OrderBook {
public:
    explicit OrderBook(std::string symbol = "");

    // Submit a new limit order. Matches against the opposite side first
    // (price-time priority), then rests any remainder on the book.
    SubmitResult submit_limit(OrderId id, Side side, Price price, Qty qty);

    // Cancel a resting order. Returns true if it was found and removed.
    bool cancel(OrderId id);

    // Top of book. std::nullopt if that side is empty.
    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;

    size_t bid_order_count() const;
    size_t ask_order_count() const;

    const std::string& symbol() const { return symbol_; }

private:
    // Price -> FIFO queue of resting orders at that price.
    // Bids: highest price first  -> std::map with std::greater.
    // Asks: lowest price first   -> std::map with std::less (default).
    using BidLevels = std::map<Price, std::deque<Order>, std::greater<Price>>;
    using AskLevels = std::map<Price, std::deque<Order>, std::less<Price>>;

    struct Location {
        Side side;
        Price price;
    };

    std::string symbol_;
    BidLevels bids_;
    AskLevels asks_;
    std::unordered_map<OrderId, Location> locations_; // for O(1) cancel
    uint64_t next_seq_ = 0;

    // Match an incoming buy against resting asks (ascending price).
    void match_buy(Order& incoming, std::vector<Trade>& trades);
    // Match an incoming sell against resting bids (descending price).
    void match_sell(Order& incoming, std::vector<Trade>& trades);

    void rest_order(Order order);
};

} // namespace lob