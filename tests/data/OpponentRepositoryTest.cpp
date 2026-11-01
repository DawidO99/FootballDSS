#include "data/repositories/OpponentRepository.hpp"

#include "RepositoryFixture.hpp"

#include <gtest/gtest.h>

#include <optional>

using namespace fdss::core;
using namespace fdss::data;
using namespace fdss::tests;

namespace {

constexpr OpponentId kNoOpponent{999};

class OpponentRepositoryTest : public RepositoryTest {
protected:
    [[nodiscard]] OpponentRepository repository() { return OpponentRepository(*m_db); }

    OpponentId create(const char* name) {
        const auto id = repository().create(OpponentDraft{name, std::nullopt});
        EXPECT_TRUE(id.has_value()) << id.error().detail;
        return id.value_or(OpponentId{});
    }
};

} // namespace

TEST_F(OpponentRepositoryTest, CreatedOpponentCanBeRead) {
    const auto id = repository().create(OpponentDraft{"  Śląsk Wrocław ", "  gra długą piłką "});
    ASSERT_TRUE(id.has_value()) << id.error().detail;

    const auto opponent = repository().get(*id);
    ASSERT_TRUE(opponent.has_value()) << opponent.error().detail;
    EXPECT_EQ(opponent->id, *id);
    EXPECT_EQ(opponent->name, "Śląsk Wrocław");
    EXPECT_EQ(opponent->notes, "gra długą piłką");
}

TEST_F(OpponentRepositoryTest, NameKeyIsTheCaseFoldedTrimmedName) {
    const OpponentId id = create(" ŚLĄSK ");
    EXPECT_EQ(scalar(m_db->database(), QStringLiteral("SELECT name_key FROM opponent WHERE id = ?"),
                     {static_cast<qlonglong>(id.value)})
                  .toString(),
              QStringLiteral("śląsk"));
}

TEST_F(OpponentRepositoryTest, GetOfAMissingOpponentIsNotFound) {
    const auto opponent = repository().get(kNoOpponent);
    ASSERT_FALSE(opponent.has_value());
    EXPECT_EQ(opponent.error().code, ErrorCode::NotFound);
}

TEST_F(OpponentRepositoryTest, ListIsSortedByTheCaseFoldedName) {
    create("Orzeł");
    create("arka");
    create("Błękitni");
    const auto opponents = repository().list();
    ASSERT_TRUE(opponents.has_value()) << opponents.error().detail;
    ASSERT_EQ(opponents->size(), 3U);
    EXPECT_EQ(opponents->at(0).name, "arka");
    EXPECT_EQ(opponents->at(1).name, "Błękitni");
    EXPECT_EQ(opponents->at(2).name, "Orzeł");
}

TEST_F(OpponentRepositoryTest, NameUniquenessIgnoresCaseAlsoForPolishLetters) {
    create("Śląsk");
    for (const char* duplicate : {"śląsk", "ŚLĄSK", "  Śląsk  "}) {
        const auto id = repository().create(OpponentDraft{duplicate, std::nullopt});
        ASSERT_FALSE(id.has_value()) << duplicate;
        EXPECT_EQ(id.error().code, ErrorCode::Conflict) << duplicate;
        EXPECT_EQ(id.error().field, "name") << duplicate;
    }
    EXPECT_EQ(count("SELECT count(*) FROM opponent"), 1);
}

TEST_F(OpponentRepositoryTest, DifferentPolishLettersAreDifferentNames) {
    create("Łęk");
    EXPECT_TRUE(repository().create(OpponentDraft{"Lek", std::nullopt}).has_value());
}

TEST_F(OpponentRepositoryTest, EmptyNameIsInvalidInput) {
    const auto id = repository().create(OpponentDraft{"   ", std::nullopt});
    ASSERT_FALSE(id.has_value());
    EXPECT_EQ(id.error().code, ErrorCode::InvalidInput);
}

TEST_F(OpponentRepositoryTest, UpdateChangesNameKeyAndNotes) {
    const OpponentId id = create("Rywal");
    ASSERT_TRUE(repository().update(id, OpponentDraft{" Orzeł ", "notatka"}).has_value());

    const auto opponent = repository().get(id);
    ASSERT_TRUE(opponent.has_value());
    EXPECT_EQ(opponent->name, "Orzeł");
    EXPECT_EQ(opponent->notes, "notatka");

    // The old name is free again and the new one is taken.
    EXPECT_TRUE(repository().create(OpponentDraft{"Rywal", std::nullopt}).has_value());
    const auto taken = repository().create(OpponentDraft{"ORZEŁ", std::nullopt});
    ASSERT_FALSE(taken.has_value());
    EXPECT_EQ(taken.error().code, ErrorCode::Conflict);
}

TEST_F(OpponentRepositoryTest, UpdateCanChangeOnlyTheCaseOfTheOwnName) {
    const OpponentId id = create("rywal");
    ASSERT_TRUE(repository().update(id, OpponentDraft{"RYWAL", std::nullopt}).has_value());
    EXPECT_EQ(repository().get(id)->name, "RYWAL");
}

TEST_F(OpponentRepositoryTest, UpdateToATakenNameIsAConflictOnName) {
    create("Śląsk");
    const OpponentId other = create("Orzeł");
    const auto status = repository().update(other, OpponentDraft{"śląsk", std::nullopt});
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, ErrorCode::Conflict);
    EXPECT_EQ(status.error().field, "name");
    EXPECT_EQ(repository().get(other)->name, "Orzeł");
}

TEST_F(OpponentRepositoryTest, UpdateClearsEmptyNotes) {
    const auto id = repository().create(OpponentDraft{"Rywal", "notatka"});
    ASSERT_TRUE(id.has_value());
    ASSERT_TRUE(repository().update(*id, OpponentDraft{"Rywal", std::nullopt}).has_value());
    EXPECT_EQ(repository().get(*id)->notes, std::nullopt);
}

TEST_F(OpponentRepositoryTest, UpdateOfAMissingOpponentIsNotFound) {
    const auto status = repository().update(kNoOpponent, OpponentDraft{"Rywal", std::nullopt});
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, ErrorCode::NotFound);
}
