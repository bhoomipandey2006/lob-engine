// naive_order_book.hpp
//
// A deliberately naive order book: unsorted std::vector storage, linear
// scan to find the best price, linear scan to cancel. Same external
// behavior as OrderBook (order_book.hpp), same price-time priority
// semantics -- just none of the indexing that makes the real one fast.
//
// This exists for exactly one purpose: to benchmark against OrderBook
// and produce a real, defensible number for "how much does the indexing
// actually buy you." Don't use this for anything else.

#pragma once

#include <cstdint>
#include <vector>
#include <optional>
#include "order_book.hpp" // reuse Order, Trade, Side, Price, Qty, SubmitResult

namespace lob
{

    class NaiveOrderBook
    {
    public:
        explicit NaiveOrderBook(std::string symbol = "");

        SubmitResult submit_limit(OrderId id, Side side, Price price, Qty qty);
        bool cancel(OrderId id);

        std::optional<Price> best_bid() const;
        std::optional<Price> best_ask() const;

    private:
        std::string symbol_;
        std::vector<Order> bids_; // unsorted -- best price found by O(n) scan
        std::vector<Order> asks_; // unsorted -- best price found by O(n) scan
        uint64_t next_seq_ = 0;

        void match_buy(Order &incoming, std::vector<Trade> &trades);
        void match_sell(Order &incoming, std::vector<Trade> &trades);

        // Find the index of the best (price-time priority) order in `side`,
        // among orders whose price the incoming order is willing to trade at.
        // Returns -1 if none qualifies. This is the O(n) scan naive pays on
        // every single match step.
        static int best_index_for_buy(const std::vector<Order> &asks, Price limit);
        static int best_index_for_sell(const std::vector<Order> &bids, Price limit);
    };

} // namespace lob