// Copyright 2021 Optiver Asia Pacific Pty. Ltd.
//
// This file is part of Ready Trader Go.
//
//     Ready Trader Go is free software: you can redistribute it and/or
//     modify it under the terms of the GNU Affero General Public License
//     as published by the Free Software Foundation, either version 3 of
//     the License, or (at your option) any later version.
//
//     Ready Trader Go is distributed in the hope that it will be useful,
//     but WITHOUT ANY WARRANTY; without even the implied warranty of
//     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//     GNU Affero General Public License for more details.
//
//     You should have received a copy of the GNU Affero General Public
//     License along with Ready Trader Go.  If not, see
//     <https://www.gnu.org/licenses/>.
#ifndef CPPREADY_TRADER_GO_AUTOTRADER_H
#define CPPREADY_TRADER_GO_AUTOTRADER_H

#include <array>
#include <memory>
#include <string>
#include <unordered_set>
#include <deque>
#include <unordered_map>
#include <chrono>
#include <thread>

#include <boost/asio/io_context.hpp>

#include <ready_trader_go/baseautotrader.h>
#include <ready_trader_go/types.h>

class AutoTrader : public ReadyTraderGo::BaseAutoTrader
{
public:
    explicit AutoTrader(boost::asio::io_context& context);

    // Called when the execution connection is lost.
    void DisconnectHandler() override;

    // Called when the matching engine detects an error.
    // If the error pertains to a particular order, then the client_order_id
    // will identify that order, otherwise the client_order_id will be zero.
    void ErrorMessageHandler(unsigned long clientOrderId,
                             const std::string& errorMessage) override;

    // Called when one of your hedge orders is filled, partially or fully.
    //
    // The price is the average price at which the order was (partially) filled,
    // which may be better than the order's limit price. The volume is
    // the number of lots filled at that price.
    //
    // If the order was unsuccessful, both the price and volume will be zero.
    void HedgeFilledMessageHandler(unsigned long clientOrderId,
                                   unsigned long price,
                                   unsigned long volume) override;

    // Called periodically to report the status of an order book.
    // The sequence number can be used to detect missed or out-of-order
    // messages. The five best available ask (i.e. sell) and bid (i.e. buy)
    // prices are reported along with the volume available at each of those
    // price levels.
    void OrderBookMessageHandler(ReadyTraderGo::Instrument instrument,
                                 unsigned long sequenceNumber,
                                 const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& askPrices,
                                 const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& askVolumes,
                                 const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& bidPrices,
                                 const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& bidVolumes) override;

    // Called when one of your orders is filled, partially or fully.
    void OrderFilledMessageHandler(unsigned long clientOrderId,
                                   unsigned long price,
                                   unsigned long volume) override;

    // Called when the status of one of your orders changes.
    // The fill volume is the number of lots already traded, remaining volume
    // is the number of lots yet to be traded and fees is the total fees paid
    // or received for this order.
    // Remaining volume will be set to zero if the order is cancelled.
    void OrderStatusMessageHandler(unsigned long clientOrderId,
                                   unsigned long fillVolume,
                                   unsigned long remainingVolume,
                                   signed long fees) override;

    // Called periodically when there is trading activity on the market.
    // The five best ask (i.e. sell) and bid (i.e. buy) prices at which there
    // has been trading activity are reported along with the aggregated volume
    // traded at each of those price levels.
    // If there are less than five prices on a side, then zeros will appear at
    // the end of both the prices and volumes arrays.
    void TradeTicksMessageHandler(ReadyTraderGo::Instrument instrument,
                                  unsigned long sequenceNumber,
                                  const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& askPrices,
                                  const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& askVolumes,
                                  const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& bidPrices,
                                  const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& bidVolumes) override;

    // Check if current message doesn't breach 50 messages limit
    // Return true if can send, false if can't due to limit
    bool checkMessageLimit();

    // Wrapper to send bid orders
    bool sendBidOrder(unsigned long price, long volume, ReadyTraderGo::Lifespan lifespanType);

    // Wrapper to send ask orders
    bool sendAskOrder(unsigned long price, long volume, ReadyTraderGo::Lifespan lifespanType);

    // Wrapper to send hedge orders
    // Hedge cannot be ignored, must be sent
    // This might be thread-unsafe, but everything here is thread-unsafe
    bool sendHedgeOrder(unsigned long price, unsigned long volume, ReadyTraderGo::Side side);

    // Wrapper to send cancel orders
    // Return False if throttled
    bool sendCancelOrder(unsigned long orderId);

    // Cancel all orders that can be arbitraged
    // Example: If future trades at 100 and 120, cancel all bid > 120 and ask < 100
    void trimOrder();

    // arbitrage if bid is higher than ask between ETF/future
    // Arbitrage can helps to reduce position as well, but can also limit market making
    // Maybe show more preference towards arbitrage that reduce position rather than increase
    void handleArbitrage(const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& askPrices,
                        const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& askVolumes,
                        const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& bidPrices,
                        const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& bidVolumes);
    
    // Cancel all bid and ask that has low chance of being filled
    void clearBook(const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& askPrices,
                    const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& askVolumes,
                    const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& bidPrices,
                    const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& bidVolumes);
    
    // Setup bid and ask order based on price of future
    // bid: [future_bid - 3, future_bid - 2,... future_bid - 1]
    // ask: [future_ask + 1, future_ask +2,...  future_ask + 3]
    void handleMarketMaking(const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& askPrices,
                            const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& askVolumes,
                            const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& bidPrices,
                            const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& bidVolumes);

private:
    unsigned long mNextMessageId = 1;
    // unsigned long mAskId = 0;
    // unsigned long mAskPrice = 0;
    // unsigned long mBidId = 0;
    // unsigned long mBidPrice = 0;
    signed long mPosition = 0;
    std::unordered_map<unsigned long, unsigned long> mAsks; // {id: rice}
    std::unordered_map<unsigned long, unsigned long> mBids; // {id: rice}

    unsigned long futureBid = 0;
    unsigned long futureAsk = 0;
    long delta = 0;
    unsigned long msgSeq = 0;
    std::deque<double> orderTimestamps;
    std::unordered_set<unsigned long> hedgeBid; // store message ID
    std::unordered_set<unsigned long> hedgeAsk; // store message ID
};

#endif //CPPREADY_TRADER_GO_AUTOTRADER_H
