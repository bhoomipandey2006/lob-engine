#include "order_book.hpp"
#include <utility>

namespace lob {

OrderBook::OrderBook(std::string symbol) : symbol_(std::move(symbol)) {}

void OrderBook::match_buy(Order& incoming, std::vector<Trade>& trades) {
    // Walk asks from lowest price up. Incoming buy crosses the spread
    // as long as its limit price is >= the best ask price.
    auto it = asks_.begin();
    while (it != asks_.end() && incoming.qty > 0 && incoming.price >= it->first) {
        auto& level = it->second; // FIFO deque at this price
        while (!level.empty() && incoming.qty > 0) {
            Order& resting = level.front();
            Qty traded = std::min(incoming.qty, resting.qty);

            trades.push_back(Trade{resting.id, incoming.id, resting.price,
                                    traded, next_seq_++});

            incoming.qty -= traded;
            resting.qty -= traded;

            if (resting.qty == 0) {
                locations_.erase(resting.id);
                level.pop_front();
            }
        }
        if (level.empty()) {
            it = asks_.erase(it); // remove empty price level
        } else {
            ++it;
        }
    }
}

void OrderBook::match_sell(Order& incoming, std::vector<Trade>& trades) {
    auto it = bids_.begin();
    while (it != bids_.end() && incoming.qty > 0 && incoming.price <= it->first) {
        auto& level = it->second;
        while (!level.empty() && incoming.qty > 0) {
            Order& resting = level.front();
            Qty traded = std::min(incoming.qty, resting.qty);

            trades.push_back(Trade{resting.id, incoming.id, resting.price,
                                    traded, next_seq_++});

            incoming.qty -= traded;
            resting.qty -= traded;

            if (resting.qty == 0) {
                locations_.erase(resting.id);
                level.pop_front();
            }
        }
        if (level.empty()) {
            it = bids_.erase(it);
        } else {
            ++it;
        }
    }
}

void OrderBook::rest_order(Order order) {
    Location loc{order.side, order.price};
    locations_[order.id] = loc;
    if (order.side == Side::Buy) {
        bids_[order.price].push_back(order);
    } else {
        asks_[order.price].push_back(order);
    }
}

SubmitResult OrderBook::submit_limit(OrderId id, Side side, Price price, Qty qty) {
    Order incoming{id, side, price, qty, next_seq_++};
    std::vector<Trade> trades;

    if (side == Side::Buy) {
        match_buy(incoming, trades);
    } else {
        match_sell(incoming, trades);
    }

    SubmitResult result;
    result.trades = std::move(trades);
    result.remaining_qty = incoming.qty;

    if (incoming.qty > 0) {
        rest_order(incoming);
        result.resting = true;
    }
    return result;
}

bool OrderBook::cancel(OrderId id) {
    auto it = locations_.find(id);
    if (it == locations_.end()) return false;

    Location loc = it->second;
    if (loc.side == Side::Buy) {
        auto level_it = bids_.find(loc.price);
        if (level_it == bids_.end()) return false;
        auto& dq = level_it->second;
        for (auto d_it = dq.begin(); d_it != dq.end(); ++d_it) {
            if (d_it->id == id) {
                dq.erase(d_it);
                break;
            }
        }
        if (dq.empty()) bids_.erase(level_it);
    } else {
        auto level_it = asks_.find(loc.price);
        if (level_it == asks_.end()) return false;
        auto& dq = level_it->second;
        for (auto d_it = dq.begin(); d_it != dq.end(); ++d_it) {
            if (d_it->id == id) {
                dq.erase(d_it);
                break;
            }
        }
        if (dq.empty()) asks_.erase(level_it);
    }
    locations_.erase(it);
    return true;
}

std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

size_t OrderBook::bid_order_count() const {
    size_t n = 0;
    for (auto& [price, dq] : bids_) n += dq.size();
    return n;
}

size_t OrderBook::ask_order_count() const {
    size_t n = 0;
    for (auto& [price, dq] : asks_) n += dq.size();
    return n;
}

} // namespace lob