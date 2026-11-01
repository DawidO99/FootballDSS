#include "data/repositories/OpponentRepository.hpp"

#include "data/repositories/RepositorySupport.hpp"

namespace fdss::data {

namespace {

const auto kSelectOpponent = QStringLiteral("SELECT id, name, notes FROM opponent ");

core::Opponent readOpponent(RowReader& row) {
    core::Opponent opponent;
    opponent.id = row.id<core::OpponentId>();
    opponent.name = row.text();
    opponent.notes = row.optionalText();
    return opponent;
}

QVariant nameKeyValue(const std::string& name) { return nameValue(name).toString().toCaseFolded(); }

// The user edits the name; name_key is an implementation detail.
core::Error reportedOnName(core::Error error) {
    if (error.field == "name_key") {
        error.field = "name";
    }
    return error;
}

} // namespace

core::Result<std::vector<core::Opponent>> OpponentRepository::list() const {
    return selectAll(m_db, kSelectOpponent + QStringLiteral("ORDER BY name_key"), {},
                     "list opponents", readOpponent);
}

core::Result<core::Opponent> OpponentRepository::get(core::OpponentId id) const {
    return selectOne(m_db, kSelectOpponent + QStringLiteral("WHERE id = ?"), {idValue(id)},
                     "get opponent", readOpponent);
}

core::Result<core::OpponentId> OpponentRepository::create(const core::OpponentDraft& draft) {
    const auto id =
        insertRow(m_db,
                  QStringLiteral("INSERT INTO opponent (name, name_key, notes) "
                                 "VALUES (?, ?, ?)"),
                  {nameValue(draft.name), nameKeyValue(draft.name), optionalTextValue(draft.notes)},
                  "create opponent");
    if (!id) {
        return std::unexpected(reportedOnName(id.error()));
    }
    return core::OpponentId{*id};
}

core::Status OpponentRepository::update(core::OpponentId id, const core::OpponentDraft& draft) {
    auto status = changeRow(
        m_db, QStringLiteral("UPDATE opponent SET name = ?, name_key = ?, notes = ? WHERE id = ?"),
        {nameValue(draft.name), nameKeyValue(draft.name), optionalTextValue(draft.notes),
         idValue(id)},
        "update opponent");
    if (!status) {
        return std::unexpected(reportedOnName(status.error()));
    }
    return status;
}

} // namespace fdss::data
