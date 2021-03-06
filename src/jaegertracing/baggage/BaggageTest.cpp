/*
 * Copyright (c) 2017 Uber Technologies, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>

#include "jaegertracing/baggage/BaggageSetter.h"
#include "jaegertracing/baggage/RemoteRestrictionManager.h"
#include "jaegertracing/baggage/RestrictionManager.h"
#include "jaegertracing/baggage/RestrictionsConfig.h"
#include "jaegertracing/metrics/Metrics.h"
#include "jaegertracing/testutils/MockAgent.h"

namespace jaegertracing {
namespace baggage {
namespace {

constexpr auto kDefaultMaxValueLength = 8;

class TestRestrictionManager : public RestrictionManager {
  public:
    ~TestRestrictionManager() { close(); }

    Restriction getRestriction(const std::string&,
                               const std::string& key) override
    {
        const auto keyAllowed = (!key.empty() && key[0] == 'a');
        return Restriction(keyAllowed, kDefaultMaxValueLength);
    }
};

template <typename FieldIterator>
void log(FieldIterator, FieldIterator)
{
}

}  // anonymous namespace

TEST(Baggage, restrictionManagerTest)
{
    auto logFn = &log<std::vector<Tag>::const_iterator>;
    TestRestrictionManager manager;
    auto metrics = metrics::Metrics::makeNullMetrics();
    BaggageSetter setter(manager, *metrics);
    Span span(
        nullptr,
        SpanContext(TraceID(),
                    123,
                    456,
                    static_cast<unsigned char>(SpanContext::Flag::kSampled),
                    SpanContext::StrMap()));
    auto baggage = span.context().baggage();
    setter.setBaggage(span, baggage, "abc", "123", logFn);
    ASSERT_EQ(1, baggage.size());
    ASSERT_EQ("123", baggage["abc"]);
    setter.setBaggage(span, baggage, "bcd", "234", logFn);
    ASSERT_EQ(1, baggage.size());
    setter.setBaggage(span, baggage, "abc", "1234567890", logFn);
    ASSERT_EQ("12345678", baggage["abc"]);
}

TEST(Baggage, testRemoteRestrictionManagerDefaults)
{
    auto logger = logging::nullLogger();
    auto metrics = metrics::Metrics::makeNullMetrics();
    RemoteRestrictionManager manager(
        "test-service",
        "",
        false,
        RemoteRestrictionManager::Clock::duration(),
        *logger,
        *metrics);
    ASSERT_EQ(RemoteRestrictionManager::defaultRefreshInterval(),
              manager.refreshInterval());
    ASSERT_EQ(
        Restriction(true, RemoteRestrictionManager::kDefaultMaxValueLength),
        manager.getRestriction("test-service", "abc"));
}

TEST(Baggage, testRemoteRestrictionManagerFunctionality)
{
    auto logger = logging::consoleLogger();
    auto metrics = metrics::Metrics::makeNullMetrics();
    auto mockAgent = testutils::MockAgent::make();
    mockAgent->start();
    RemoteRestrictionManager manager(
        "test-service",
        mockAgent->samplingServerAddress().authority(),
        true,
        std::chrono::milliseconds(100),
        *logger,
        *metrics);
    ASSERT_EQ(Restriction(false, 0),
              manager.getRestriction("test-service", "abc"));
    mockAgent->addBaggageRestriction("abc", Restriction(true, 1));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ASSERT_EQ(Restriction(true, 1),
              manager.getRestriction("test-service", "abc"));
    ASSERT_EQ(Restriction(false, 0),
              manager.getRestriction("test-service", "bcd"));
    manager.close();
}

}  // namespace baggage
}  // namespace jaegertracing
