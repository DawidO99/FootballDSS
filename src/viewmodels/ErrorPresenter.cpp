#include "viewmodels/ErrorPresenter.hpp"

#include "services/AuthService.hpp"

namespace fdss::viewmodels {

QString ErrorPresenter::message(const core::Error& error) {
    switch (error.code) {
    case core::ErrorCode::InvalidInput:
        return invalidInputMessage(error.field);
    case core::ErrorCode::Conflict:
        return conflictMessage(error.field);
    case core::ErrorCode::NotFound:
        return tr("This item no longer exists. Open the screen again and check the current data.");
    case core::ErrorCode::PermissionDenied:
        return tr("Your role does not allow this. Ask the coach to do it or to change your role.");
    case core::ErrorCode::DatabaseLocked:
        return tr("Another program is using the database, possibly a second FootballDSS window. "
                  "Close it and try again.");
    case core::ErrorCode::NotADatabase:
        return tr("The database file is damaged. Close the app and replace the file with a copy "
                  "from the backups folder.");
    case core::ErrorCode::IncompatibleVersion:
        return tr("This data was saved by a newer version of FootballDSS. Install the newer "
                  "version to use it.");
    case core::ErrorCode::Io:
        return tr("A file cannot be read or written. Check that the disk is not full and that you "
                  "may write to the folder.");
    case core::ErrorCode::UnsupportedFormat:
        return tr("This file format is not supported. Save the file as CSV or XLSX and try again.");
    case core::ErrorCode::Database:
    case core::ErrorCode::Internal:
        break;
    }
    return tr("Something went wrong and the change was not saved. Try again. If this repeats, "
              "start the app again.");
}

QString ErrorPresenter::invalidInputMessage(const std::string& field) {
    if (field == "pin") {
        return tr("A PIN has 4 to 8 digits.");
    }
    if (field == "pin_repeat") {
        return tr("The PINs do not match. Enter the same PIN twice.");
    }
    if (field == "pin_same") {
        return tr("The new PIN must differ from the current one.");
    }
    if (field == "club_name") {
        return tr("Enter the club name, up to 100 characters.");
    }
    if (field == "team_name") {
        return tr("Enter the team name, up to 100 characters.");
    }
    if (field == "coach_name") {
        return tr("Enter the coach's name, up to 50 characters.");
    }
    if (field == "name") {
        return tr("Enter a name, up to 50 characters.");
    }
    if (field == "season") {
        return tr("Enter the season in the form 2026/27.");
    }
    return tr("One of the values is not valid. Check what you entered and try again.");
}

QString ErrorPresenter::pinCheckMessage(const services::PinCheck& check) {
    switch (check.status) {
    case services::PinCheckStatus::Accepted:
        break;
    case services::PinCheckStatus::WrongPin:
        return tr("Wrong PIN. Attempts left: %1.").arg(check.remainingAttempts);
    case services::PinCheckStatus::LockedOut:
        return tr("Too many wrong PINs. Try again in %1 s.").arg(check.lockedFor.count());
    }
    return {};
}

QString ErrorPresenter::conflictMessage(const std::string& field) {
    if (field == "last_coach") {
        return tr("The club must keep an active coach. Make someone else a coach first.");
    }
    return tr("This conflicts with data that already exists. Change the value and try again.");
}

QString ErrorPresenter::startupMessage(core::ErrorCode code) {
    switch (code) {
    case core::ErrorCode::NotADatabase:
        return tr("This file is not a FootballDSS database, or it is damaged. Close the app. Then "
                  "move the file out of the data folder to start with a new database, or replace "
                  "it with a copy from the backups folder. In both cases delete the files next to "
                  "it that end in -wal and -shm.");
    case core::ErrorCode::DatabaseLocked:
        return tr("Another program is using the database, possibly a second FootballDSS window. "
                  "Close it and start the app again.");
    case core::ErrorCode::IncompatibleVersion:
        return tr("This database was saved by a newer version of FootballDSS. Install the newer "
                  "version to open it. The file was not changed.");
    case core::ErrorCode::Io:
        return tr("The database file cannot be read or written. Check that the disk is not full "
                  "and that you may write to the data folder.");
    case core::ErrorCode::Internal:
        return tr("FootballDSS could not start its database module. Install the app again.");
    case core::ErrorCode::Database:
    case core::ErrorCode::NotFound:
    case core::ErrorCode::InvalidInput:
    case core::ErrorCode::Conflict:
    case core::ErrorCode::PermissionDenied:
    case core::ErrorCode::UnsupportedFormat:
        break;
    }
    return tr("The database could not be opened because of an unexpected error. Start the app "
              "again. If this repeats, close the app, replace the file with a copy from the "
              "backups folder and delete the files next to it that end in -wal and -shm.");
}

} // namespace fdss::viewmodels
