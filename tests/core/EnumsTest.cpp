#include "core/domain/Enums.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace fdss::core;

namespace {

// Every value of the enumeration in declaration order, e.g. "coach,assistant,physio,analyst".
template <class E> std::string listedTexts() {
    std::string text;
    for (const E value : allValues<E>()) {
        if (!text.empty()) {
            text += ',';
        }
        text += toDbText(value);
    }
    return text;
}

template <class E> void expectRoundTrip() {
    for (const E value : allValues<E>()) {
        const auto parsed = fromDbText<E>(toDbText(value));
        ASSERT_TRUE(parsed.has_value()) << toDbText(value);
        EXPECT_EQ(*parsed, value);
    }
}

template <class E> void expectUnknownTextFails() {
    for (const char* text : {"", "unknown_value", " coach", "COACH"}) {
        const auto parsed = fromDbText<E>(text);
        ASSERT_FALSE(parsed.has_value()) << text;
        EXPECT_EQ(parsed.error().code, ErrorCode::InvalidInput);
    }
}

} // namespace

// Written out from SPEC section 6, so a value missing from EnumValues fails here.
TEST(EnumsTest, ListTheValuesOfTheSpec) {
    EXPECT_EQ(listedTexts<TeamCategory>(), "senior,u19,u17,u15,u13,u11,other");
    EXPECT_EQ(listedTexts<Role>(), "coach,assistant,physio,analyst");
    EXPECT_EQ(listedTexts<Position>(), "gk,rb,cb,lb,rwb,lwb,dm,cm,am,rm,lm,rw,lw,st");
    EXPECT_EQ(listedTexts<PreferredFoot>(), "left,right,both,unknown");
    EXPECT_EQ(listedTexts<AvailabilityType>(), "injury,illness,suspension,absence");
    EXPECT_EQ(listedTexts<MatchStatus>(), "planned,live,finished");
    EXPECT_EQ(listedTexts<Venue>(), "home,away,neutral");
    EXPECT_EQ(listedTexts<SessionType>(), "training,match_prep,recovery,gym,other");
    EXPECT_EQ(listedTexts<AttendanceStatus>(), "present,absent,excused,injured");
    EXPECT_EQ(listedTexts<MatchEventType>(), "goal,assist,yellow,red,sub,opponent_goal");
    EXPECT_EQ(listedTexts<ClockStep>(),
              "first_half_start,first_half_end,second_half_start,match_end");
    EXPECT_EQ(listedTexts<MetricValueType>(), "integer,decimal,boolean");
    EXPECT_EQ(listedTexts<MetricAggregation>(), "sum,avg,min,max,last");
    EXPECT_EQ(listedTexts<MetricScope>(), "match,training,player");
}

TEST(EnumsTest, EveryValueRoundTrips) {
    expectRoundTrip<TeamCategory>();
    expectRoundTrip<Role>();
    expectRoundTrip<Position>();
    expectRoundTrip<PreferredFoot>();
    expectRoundTrip<AvailabilityType>();
    expectRoundTrip<MatchStatus>();
    expectRoundTrip<Venue>();
    expectRoundTrip<SessionType>();
    expectRoundTrip<AttendanceStatus>();
    expectRoundTrip<MatchEventType>();
    expectRoundTrip<ClockStep>();
    expectRoundTrip<MetricValueType>();
    expectRoundTrip<MetricAggregation>();
    expectRoundTrip<MetricScope>();
}

TEST(EnumsTest, UnknownTextFails) {
    expectUnknownTextFails<TeamCategory>();
    expectUnknownTextFails<Role>();
    expectUnknownTextFails<Position>();
    expectUnknownTextFails<PreferredFoot>();
    expectUnknownTextFails<AvailabilityType>();
    expectUnknownTextFails<MatchStatus>();
    expectUnknownTextFails<Venue>();
    expectUnknownTextFails<SessionType>();
    expectUnknownTextFails<AttendanceStatus>();
    expectUnknownTextFails<MatchEventType>();
    expectUnknownTextFails<ClockStep>();
    expectUnknownTextFails<MetricValueType>();
    expectUnknownTextFails<MetricAggregation>();
    expectUnknownTextFails<MetricScope>();
}

TEST(EnumsTest, PositionsBelongToTheGroupsOfTheSpec) {
    EXPECT_EQ(positionGroup(Position::Gk), PositionGroup::Goalkeeper);
    for (const Position position :
         {Position::Rb, Position::Cb, Position::Lb, Position::Rwb, Position::Lwb}) {
        EXPECT_EQ(positionGroup(position), PositionGroup::Defender) << toDbText(position);
    }
    for (const Position position :
         {Position::Dm, Position::Cm, Position::Am, Position::Rm, Position::Lm}) {
        EXPECT_EQ(positionGroup(position), PositionGroup::Midfielder) << toDbText(position);
    }
    for (const Position position : {Position::Rw, Position::Lw, Position::St}) {
        EXPECT_EQ(positionGroup(position), PositionGroup::Forward) << toDbText(position);
    }
}

TEST(EnumsTest, PositionListRoundTrips) {
    const std::vector<Position> positions{Position::Cb, Position::Rb, Position::Dm};
    const std::string text = positionsToDbText(positions);
    EXPECT_EQ(text, "cb,rb,dm");
    const auto parsed = positionsFromDbText(text);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, positions);
}

TEST(EnumsTest, EmptyPositionListIsEmptyText) {
    EXPECT_EQ(positionsToDbText({}), "");
    const auto parsed = positionsFromDbText("");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->empty());
}

TEST(EnumsTest, PositionListWithAnUnknownTokenFails) {
    for (const char* text : {"cb,xx", "cb,", ",cb", "cb, rb", "CB"}) {
        const auto parsed = positionsFromDbText(text);
        ASSERT_FALSE(parsed.has_value()) << text;
        EXPECT_EQ(parsed.error().code, ErrorCode::InvalidInput);
    }
}
