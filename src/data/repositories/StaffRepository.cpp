#include "data/repositories/StaffRepository.hpp"

#include "data/repositories/RepositorySupport.hpp"

namespace fdss::data {

namespace {

const auto kSelectStaff = QStringLiteral("SELECT id, name, role, active FROM staff_user ");

core::StaffUser readStaffUser(RowReader& row) {
    core::StaffUser user;
    user.id = row.id<core::StaffUserId>();
    user.name = row.text();
    user.role = row.enumeration<core::Role>();
    user.active = row.boolean();
    return user;
}

core::StaffCredentials readCredentials(RowReader& row) {
    core::StaffCredentials credentials;
    credentials.hash = row.bytes();
    credentials.salt = row.bytes();
    credentials.iterations = row.integer();
    credentials.failedAttempts = row.integer();
    credentials.lockedUntil = row.optionalTimestamp();
    return credentials;
}

} // namespace

core::Result<std::vector<core::StaffUser>> StaffRepository::listActive() const {
    return selectAll(m_db, kSelectStaff + QStringLiteral("WHERE active = 1 ORDER BY id"), {},
                     "list active staff", readStaffUser);
}

core::Result<std::vector<core::StaffUser>> StaffRepository::listAll() const {
    return selectAll(m_db, kSelectStaff + QStringLiteral("ORDER BY id"), {}, "list staff",
                     readStaffUser);
}

core::Result<core::StaffUser> StaffRepository::get(core::StaffUserId id) const {
    return selectOne(m_db, kSelectStaff + QStringLiteral("WHERE id = ?"), {idValue(id)},
                     "get staff user", readStaffUser);
}

core::Result<core::StaffUserId> StaffRepository::create(const core::StaffUserDraft& draft,
                                                        const core::PinHash& pin) {
    const auto id = insertRow(
        m_db,
        QStringLiteral("INSERT INTO staff_user (name, role, active, pin_hash, salt, iterations) "
                       "VALUES (?, ?, ?, ?, ?, ?)"),
        {nameValue(draft.name), enumValue(draft.role), draft.active ? 1 : 0, bytesValue(pin.hash),
         bytesValue(pin.salt), pin.iterations},
        "create staff user");
    if (!id) {
        return std::unexpected(id.error());
    }
    return core::StaffUserId{*id};
}

core::Status StaffRepository::update(core::StaffUserId id, const core::StaffUserDraft& draft) {
    return changeRow(
        m_db, QStringLiteral("UPDATE staff_user SET name = ?, role = ?, active = ? WHERE id = ?"),
        {nameValue(draft.name), enumValue(draft.role), draft.active ? 1 : 0, idValue(id)},
        "update staff user");
}

core::Result<core::StaffCredentials> StaffRepository::credentials(core::StaffUserId id) const {
    return selectOne(m_db,
                     QStringLiteral("SELECT pin_hash, salt, iterations, failed_attempts, "
                                    "locked_until FROM staff_user WHERE id = ?"),
                     {idValue(id)}, "read credentials", readCredentials);
}

core::Status StaffRepository::setPin(core::StaffUserId id, const core::PinHash& pin) {
    return changeRow(
        m_db,
        QStringLiteral("UPDATE staff_user SET pin_hash = ?, salt = ?, iterations = ? WHERE id = ?"),
        {bytesValue(pin.hash), bytesValue(pin.salt), pin.iterations, idValue(id)}, "set PIN");
}

core::Status StaffRepository::setLoginState(core::StaffUserId id, int failedAttempts,
                                            std::optional<core::Timestamp> lockedUntil) {
    return changeRow(
        m_db,
        QStringLiteral("UPDATE staff_user SET failed_attempts = ?, locked_until = ? WHERE id = ?"),
        {failedAttempts, optionalTimestampValue(lockedUntil), idValue(id)}, "set login state");
}

core::Result<int> StaffRepository::countActiveCoaches() const {
    return selectOne(
        m_db, QStringLiteral("SELECT count(*) FROM staff_user WHERE active = 1 AND role = ?"),
        {enumValue(core::Role::Coach)}, "count active coaches",
        [](RowReader& row) { return row.integer(); });
}

} // namespace fdss::data
