/* bidder_test.cc
   Eric Robert, 10 April 2014
   Copyright (c) 2013 Datacratic.  All rights reserved.
*/


#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include <boost/test/unit_test.hpp>

#include "jml/utils/testing/watchdog.h"
#include "rtbkit/common/win_cost_model.h"
#include "rtbkit/plugins/exchange/openrtb_exchange_connector.h"
#include "rtbkit/plugins/exchange/rtbkit_exchange_connector.h"
#include "rtbkit/testing/bid_stack.h"
#include "rtbkit/plugins/bidder_interface/multi_bidder_interface.h"

using namespace Datacratic;
using namespace RTBKIT;

#if 0
BOOST_AUTO_TEST_CASE( bidder_http_test )
{
    ML::Watchdog watchdog(10.0);

    Json::Value upstreamRouterConfig;
    upstreamRouterConfig[0]["exchangeType"] = "openrtb";

    Json::Value downstreamRouterConfig;
    downstreamRouterConfig[0]["exchangeType"] = "rtbkit";

    Json::Value upstreamBidderConfig;
    upstreamBidderConfig["type"] = "http";
    upstreamBidderConfig["adserver"]["winPort"] = 18143;
    upstreamBidderConfig["adserver"]["eventPort"] = 18144;

    Json::Value downstreamBidderConfig;
    downstreamBidderConfig["type"] = "agents";

    BidStack upstreamStack;
    BidStack downstreamStack;

    downstreamStack.runThen(
        downstreamRouterConfig, downstreamBidderConfig,
        USD_CPM(10), 0, [&](Json::Value const & json) {

        std::cerr << json << std::endl;
        const auto &bids = json["workers"][0]["bids"];
        const auto &wins = json["workers"][0]["wins"];
        const auto &events = json["workers"][0]["events"];

        // We don't use them for now but we might later on if we decide to extend the test
        (void) wins;
        (void) events;

        auto url = bids["url"].asString();
        auto resource = bids.get("resource", "/").asString();
        upstreamBidderConfig["router"]["host"] = "http://" + url;
        upstreamBidderConfig["router"]["path"] = resource;
        upstreamBidderConfig["adserver"]["host"] = "";


        upstreamStack.runThen(
            upstreamRouterConfig, upstreamBidderConfig, USD_CPM(20), 10,
            [&](Json::Value const &json)
        {
            // Since the FilterRegistry is shared amongst the routers,
            // the ExternalIdsCreativeExchangeFilter will also be added
            // to the upstream stack FilterPool. Thus we remove it before
            // starting the MockExchange to avoid being filtered
            upstreamStack.services.router->filters.removeFilter(
                ExternalIdsCreativeExchangeFilter::name);

            auto proxies = std::make_shared<ServiceProxies>();
            MockExchange mockExchange(proxies);
            mockExchange.start(json);
        });
    });


    auto upstreamEvents = upstreamStack.proxies->events->get(std::cerr);
    int upstreamBidCount = upstreamEvents["router.bid"];
    std::cerr << "UPSTREAM BID COUNT=" << upstreamBidCount << std::endl;

    auto downstreamEvents = downstreamStack.proxies->events->get(std::cerr);
    int downstreamBidCount = downstreamEvents["router.bid"];
    std::cerr << "DOWNSTREAM BID COUNT=" << downstreamBidCount << std::endl;

    //BOOST_CHECK_EQUAL(bpcEvents["router.cummulatedBidPrice"], count * 1000);
    //BOOST_CHECK_EQUAL(bpcEvents["router.cummulatedAuthorizedPrice"], count * 505);
}
#endif

struct BiddingAgentOfDestiny : public TestAgent {
    BiddingAgentOfDestiny(std::shared_ptr<ServiceProxies> proxies,
                          const Json::Value &bidderConfig,
                          const std::string &name = "biddingAgentOfDestiny",
                          const AccountKey &accountKey =
                             AccountKey({"testCampaign", "testStrategy"}))
        : TestAgent(proxies, name, accountKey)
     {
         config.maxInFlight = 10000;
         config.bidderConfig = bidderConfig;
     }

};

BOOST_AUTO_TEST_CASE( multi_bidder_test )
{
    Json::Value upstreamRouterConfig;
    upstreamRouterConfig[0]["exchangeType"] = "openrtb";

    Json::Value downstreamRouterConfig;
    downstreamRouterConfig[0]["exchangeType"] = "rtbkit";

    Json::Value upstreamBidderConfig = Json::parse(
            R"JSON(
            {
               "type": "multi",
               "interfaces": [
                   {
                       "iface.agents": { "type": "agents" }
                   },
                   {
                       "iface.http": {
                           "type": "http",

                           "router": {
                           },

                           "adserver": {
                               "winPort": 18143,
                               "eventPort": 18144
                           }
                       }
                   }
               ]
            }
            )JSON");

    Json::Value downstreamBidderConfig;
    downstreamBidderConfig["type"] = "agents";

    BidStack upstreamStack;
    BidStack downstreamStack;

    Json::Value destinyAgentConfig1;
    destinyAgentConfig1["iface.agents"]["probability"] = 0.7;
    destinyAgentConfig1["iface.http"]["probability"] = 0.3;

    Json::Value destinyAgentConfig2;
    destinyAgentConfig2["iface.agents"]["probability"] = 1.0;

    auto destinyAgent =
        std::make_shared<BiddingAgentOfDestiny>(
                upstreamStack.proxies,
                destinyAgentConfig1,
                "bidding_agent_of_destiny_1");

    auto destinyAgent2 =
        std::make_shared<BiddingAgentOfDestiny>(
                upstreamStack.proxies,
                destinyAgentConfig2,
                "bidding_agent_of_destiny_2");

    upstreamStack.addAgent(destinyAgent);
    upstreamStack.addAgent(destinyAgent2);

    downstreamStack.runThen(downstreamRouterConfig, downstreamBidderConfig, USD_CPM(10), 0,
                            [&](const Json::Value &json) {

        const auto &bids = json["workers"][0]["bids"];
        const auto &wins = json["workers"][0]["wins"];
        const auto &events = json["workers"][0]["events"];

        (void) wins;
        (void) events;

        auto url = bids["url"].asString();
        auto resource = bids.get("resource", "/").asString();
        auto &httpIface = upstreamBidderConfig["interfaces"][1]["iface.http"];
        httpIface["router"]["host"] = "http://" + url;
        httpIface["router"]["path"] = resource;
        httpIface["adserver"]["host"] = "";

        upstreamStack.runThen(upstreamRouterConfig, upstreamBidderConfig, USD_CPM(10),
                              10000,
                              [&](const Json::Value &json) {
            upstreamStack.services.router->filters.removeFilter(
                ExternalIdsCreativeExchangeFilter::name);

            auto proxies = std::make_shared<ServiceProxies>();
            MockExchange mockExchange(proxies);
                mockExchange.start(json);
        });

        auto bidder = std::static_pointer_cast<MultiBidderInterface>(
                upstreamStack.services.router->bidder);
        ExcAssert(bidder);
        auto stats = bidder->stats();
        auto percentage = [](int value, int total) {
            return std::to_string((value * 100) / total) + "%";
        };

        for (const auto &stat: stats) {
            size_t totalAuctions = stat.second.totalAuctions;
            std::cerr << "Stats for " << stat.first << std::endl;
            std::cerr << std::string(40, '-') << std::endl;
            std::cerr << std::string(4, ' ')
                      <<"- totalAuctionsSent: " << totalAuctions << std::endl;
            std::cerr << std::string(4, ' ')
                      << "- Auctions for BidderInterface: " << std::endl;
            for (const auto &interface: stat.second.interfacesStats) {
                size_t count = interface.second.auctionsSent;
                std::cerr << std::string(8, ' ')
                          << "* " << interface.first << ": " << count
                          << " (" + percentage(count, totalAuctions) << ")" << std::endl;
            }
            std::cerr << std::endl;
        }
    });
}
