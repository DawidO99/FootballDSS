#include "core/domain/Metric.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <optional>
#include <vector>

using namespace fdss::core;
using namespace std::chrono;

TEST(MetricTest, KeyIsAnAsciiSlugOfTheName) {
    EXPECT_EQ(metricKey(std::string_view{"Sprint 30 m"}), "sprint_30_m");
    EXPECT_EQ(metricKey(std::string_view{"  Skok w dał (cm) "}), "skok_w_dal_cm");
    EXPECT_EQ(metricKey(std::string_view{"Łączna ŻÓŁTA-kartka"}), "laczna_zolta_kartka");
    EXPECT_EQ(metricKey(std::string_view{"VO2max #1"}), "vo2max_1");
    EXPECT_EQ(metricKey(std::string_view{"--"}), "");
    EXPECT_EQ(metricKey(std::string_view{"Спринт"}), "");
}

TEST(MetricTest, NameKeyIgnoresCaseDiacriticsSpacesAndPunctuation) {
    const std::string expected = "sprint30m";
    for (const char* name : {"Sprint 30m", "sprint 30 m", "Sprint-30M", " SPRINT 30 M. ",
                             "Sprint\u2013 30m", "Sprint (30 m)"}) {
        EXPECT_EQ(metricNameKey(name), expected) << name;
    }
    EXPECT_EQ(metricNameKey("Skok w dał"), metricNameKey("skok w dal"));
    EXPECT_NE(metricNameKey("Sprint 30 m"), metricNameKey("Sprint 40 m"));
    EXPECT_EQ(metricNameKey("Спринт!"), "Спринт") << "other letters are kept as they are";
    EXPECT_EQ(metricNameKey(" - "), "");
}

TEST(MetricTest, KeyComesFromTheEnglishNameFirst) {
    MetricDefinitionDraft draft;
    draft.namePl = "Skok w dal";
    draft.nameEn = "Long jump";
    EXPECT_EQ(metricKey(draft), "long_jump");
    draft.nameEn = "  ";
    EXPECT_EQ(metricKey(draft), "skok_w_dal");
}

TEST(MetricTest, ScopeOfTheTarget) {
    EXPECT_EQ(scopeOf(MetricTarget{MatchId{1}}), MetricScope::Match);
    EXPECT_EQ(scopeOf(MetricTarget{TrainingSessionId{1}}), MetricScope::Training);
    EXPECT_EQ(scopeOf(MetricTarget{2026y / November / 10d}), MetricScope::Player);
}

TEST(MetricTest, DecimalAcceptsCommaAndPoint) {
    EXPECT_EQ(parseMetricValue("4,35", MetricValueType::Decimal), 4.35);
    EXPECT_EQ(parseMetricValue("4.35", MetricValueType::Decimal), 4.35);
    EXPECT_EQ(parseMetricValue(" 12 ", MetricValueType::Decimal), 12.0);
    EXPECT_EQ(parseMetricValue(",5", MetricValueType::Decimal), 0.5);
    EXPECT_EQ(parseMetricValue("7,", MetricValueType::Decimal), 7.0);
    EXPECT_EQ(parseMetricValue("-1,25", MetricValueType::Decimal), -1.25);
    EXPECT_EQ(parseMetricValue("+3", MetricValueType::Decimal), 3.0);
    EXPECT_EQ(parseMetricValue("0,1", MetricValueType::Decimal), 0.1);
    EXPECT_EQ(parseMetricValue("123456789012345", MetricValueType::Decimal), 123456789012345.0);
}

TEST(MetricTest, DecimalRefusesWhatIsNotANumber) {
    for (const char* text : {"", " ", "-", ",", "1,2,3", "1.234,5", "1 000", "1e3", "abc", "4,3s",
                             "1234567890123456", "--1", "nan", "inf"}) {
        EXPECT_EQ(parseMetricValue(text, MetricValueType::Decimal), std::nullopt) << text;
    }
}

TEST(MetricTest, IntegerNeedsAWholeValue) {
    EXPECT_EQ(parseMetricValue("12", MetricValueType::Integer), 12.0);
    EXPECT_EQ(parseMetricValue("12,0", MetricValueType::Integer), 12.0);
    EXPECT_EQ(parseMetricValue("-3", MetricValueType::Integer), -3.0);
    EXPECT_EQ(parseMetricValue("12,5", MetricValueType::Integer), std::nullopt);
    EXPECT_EQ(parseMetricValue("x", MetricValueType::Integer), std::nullopt);
}

TEST(MetricTest, BooleanIsZeroOrOne) {
    EXPECT_EQ(parseMetricValue("1", MetricValueType::Boolean), 1.0);
    EXPECT_EQ(parseMetricValue(" 0", MetricValueType::Boolean), 0.0);
    EXPECT_EQ(parseMetricValue("2", MetricValueType::Boolean), std::nullopt);
    EXPECT_EQ(parseMetricValue("1,0", MetricValueType::Boolean), std::nullopt);
    EXPECT_EQ(parseMetricValue("yes", MetricValueType::Boolean), std::nullopt);
}

TEST(MetricTest, AggregationFunctions) {
    // Sprint times of four sessions, oldest first.
    const std::vector values{4.40, 4.30, 4.50, 4.20};
    EXPECT_DOUBLE_EQ(*aggregate(values, MetricAggregation::Sum), 17.40);
    EXPECT_DOUBLE_EQ(*aggregate(values, MetricAggregation::Avg), 4.35);
    EXPECT_DOUBLE_EQ(*aggregate(values, MetricAggregation::Min), 4.20);
    EXPECT_DOUBLE_EQ(*aggregate(values, MetricAggregation::Max), 4.50);
    EXPECT_DOUBLE_EQ(*aggregate(values, MetricAggregation::Last), 4.20);

    const std::array one{7.0};
    for (const MetricAggregation aggregation : allValues<MetricAggregation>()) {
        EXPECT_DOUBLE_EQ(*aggregate(one, aggregation), 7.0) << toDbText(aggregation);
        EXPECT_EQ(aggregate({}, aggregation), std::nullopt) << toDbText(aggregation);
    }

    // A boolean metric: 3 "yes" of 4 answers.
    const std::array answers{1.0, 0.0, 1.0, 1.0};
    EXPECT_DOUBLE_EQ(*aggregate(answers, MetricAggregation::Sum), 3.0);
    EXPECT_DOUBLE_EQ(*aggregate(answers, MetricAggregation::Avg), 0.75);
}

TEST(MetricTest, TrendAgainstThePreviousValue) {
    EXPECT_EQ(trendOf(4.2, std::nullopt), MetricTrend::None);
    EXPECT_EQ(trendOf(4.2, 4.3), MetricTrend::Down);
    EXPECT_EQ(trendOf(4.4, 4.3), MetricTrend::Up);
    EXPECT_EQ(trendOf(4.3, 4.3), MetricTrend::Same);
}

TEST(MetricTest, ImprovementFollowsHigherIsBetter) {
    EXPECT_TRUE(isImprovement(MetricTrend::Up, true));
    EXPECT_FALSE(isImprovement(MetricTrend::Down, true));
    EXPECT_TRUE(isImprovement(MetricTrend::Down, false)) << "a faster sprint";
    EXPECT_FALSE(isImprovement(MetricTrend::Up, false));
    for (const bool higherIsBetter : {true, false}) {
        EXPECT_FALSE(isImprovement(MetricTrend::Same, higherIsBetter));
        EXPECT_FALSE(isImprovement(MetricTrend::None, higherIsBetter));
    }
}
