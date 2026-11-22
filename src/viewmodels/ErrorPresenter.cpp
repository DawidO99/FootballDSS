#include "viewmodels/ErrorPresenter.hpp"

#include "core/domain/Validation.hpp"
#include "services/AuthService.hpp"

namespace fdss::viewmodels {

QString ErrorPresenter::message(const core::Error& error) {
    switch (error.code) {
    case core::ErrorCode::InvalidInput:
        return invalidInputMessage(error.field);
    case core::ErrorCode::Conflict:
        return conflictMessage(error.field);
    case core::ErrorCode::NotFound:
        if (error.field == "nothing_to_undo") {
            return tr("There is nothing to undo.");
        }
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
        if (error.field == "tactic_version") {
            return tr("This tactics file was saved by a newer version of FootballDSS. Install the "
                      "newer version to open it.");
        }
        return tr("This data was saved by a newer version of FootballDSS. Install the newer "
                  "version to use it.");
    case core::ErrorCode::Io:
        if (error.field == "import_file") {
            return tr("The file cannot be read. Check that it still exists and is not open in "
                      "another program, then choose it again.");
        }
        if (error.field == "database_missing") {
            return tr("The database file is gone: it was deleted or moved while the app was "
                      "running. Nothing was saved. Close the app and open it again; restore a "
                      "backup if the data is missing.");
        }
        if (error.field == "import_template") {
            return tr("The template cannot be saved there. Choose another folder.");
        }
        return tr("A file cannot be read or written. Check that the disk is not full and that you "
                  "may write to the folder.");
    case core::ErrorCode::UnsupportedFormat:
        if (error.field == "import_xls") {
            return tr("This is an old Excel file (.xls). Open it in Excel, save it as .xlsx and "
                      "import that file.");
        }
        if (error.field == "import_json") {
            return tr("JSON files are not supported for this type of import. Choose a CSV or "
                      "XLSX file.");
        }
        if (error.field == "import_xlsx_corrupt") {
            return tr("This file is not a valid .xlsx workbook, or it is damaged. Open it in "
                      "Excel, save it again and import the new file.");
        }
        if (error.field == "import_file_type") {
            return tr("This file type is not supported. Accepted: .csv, .txt, .tsv, .xlsx. Choose "
                      "another file.");
        }
        if (error.field == "import_not_table") {
            return tr("The file does not look like a table: no column is recognised and the rows "
                      "have different numbers of columns. Choose the file with the data or fill in "
                      "the template.");
        }
        if (error.field == "import_wide_format") {
            return tr("The file has one column per date. The import needs one row per player and "
                      "session: save the training template, copy the data into it and import "
                      "that file.");
        }
        if (error.field == "import_type") {
            return tr("This import cannot be undone here.");
        }
        if (error.field == "tactic_file") {
            return tr("This is not a tactics file of FootballDSS (.tactic.json), or it is damaged. "
                      "Choose another file.");
        }
        return tr("This file format is not supported. Save the file as CSV or XLSX and try again.");
    case core::ErrorCode::Database:
        if (error.field == "match_history") {
            return tr("The history of this match is inconsistent: its events do not fit the saved "
                      "lineup, so Match Day cannot show it. Restore a backup of the database or "
                      "ask for help.");
        }
        break;
    case core::ErrorCode::Internal:
        break;
    }
    return tr("Something went wrong and the change was not saved. Try again. If this repeats, "
              "start the app again.");
}

QString ErrorPresenter::invalidInputMessage(const std::string& field) {
    // Import (P16).
    if (field == "import_csv_quote") {
        return tr(
            "The CSV has a quoted field that is never closed. Close the quote in the file and "
            "import it again.");
    }
    if (field == "import_file_size") {
        return tr("The file is larger than 20 MB. Remove what is not needed and try again.");
    }
    if (field == "import_empty") {
        return tr("The file is empty. Choose the file with the data, or fill in the template.");
    }
    if (field == "import_no_rows") {
        return tr("The file has only a header and no rows. Add the data below the header, or "
                  "choose another sheet.");
    }
    if (field == "import_sheet") {
        return tr("This sheet no longer exists. Open the file again.");
    }
    if (field == "import_has_errors") {
        return tr("Some rows have errors. Fix them in the file, or choose to skip them.");
    }
    if (field == "import_nothing") {
        return tr("There is nothing to import: every row is skipped, already in the app or has "
                  "errors.");
    }
    // Live tactics (P13b): MatchDayService::recordTacticChange().
    if (field == "tactic_shape") {
        return tr("This shape cannot be used: it needs 11 places on the pitch and at most one "
                  "goalkeeper.");
    }
    if (field == "tactic_unchanged") {
        return tr("The shape is the same as before. Move a player or choose another formation.");
    }
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
    // Load signal thresholds (P19).
    if (field == "ratio_low") {
        return tr(
            "Enter a ratio above 0 and below the elevated ratio, with at most two decimals, for "
            "example 0.8.");
    }
    if (field == "ratio_elevated") {
        return tr(
            "Enter a ratio above the underload ratio and below the high ratio, with at most two "
            "decimals, for example 1.3.");
    }
    if (field == "ratio_high") {
        return tr(
            "Enter a ratio above the elevated ratio, at most 5, with at most two decimals, for "
            "example 1.5.");
    }
    if (field == "monotony_high") {
        return tr(
            "Enter a monotony above 1, at most 10, with at most two decimals, for example 2.");
    }
    if (field == "season") {
        return tr("Enter the season as two consecutive years in the form 2026/27.");
    }
    if (field == "first_name") {
        return tr("Enter the first name, up to 50 characters.");
    }
    if (field == "last_name") {
        return tr("Enter the last name, up to 50 characters.");
    }
    if (field == "shirt_number") {
        return tr("Enter a shirt number from %1 to %2, or leave it empty.")
            .arg(core::kMinShirtNumber)
            .arg(core::kMaxShirtNumber);
    }
    if (field == "birth_year") {
        return tr("Enter a birth year from %1 on, or leave it empty. A player is at least %2 years "
                  "old.")
            .arg(core::kMinBirthYear)
            .arg(core::kYoungestPlayerAge);
    }
    if (field == "secondary_positions") {
        return tr("Choose each secondary position once, and not the primary one.");
    }
    if (field == "from_date") {
        return tr("Enter the first day of the absence.");
    }
    if (field == "expected_to") {
        return tr("The expected return cannot be before the first day of the absence.");
    }
    if (field == "actual_to") {
        return tr("The return cannot be before the first day of the absence.");
    }
    if (field == "opponent_name") {
        return tr("Enter the opponent's name, up to 100 characters.");
    }
    if (field == "opponent_id") {
        return tr("Choose the opponent, or add a new one.");
    }
    if (field == "date") {
        return tr("Enter the date.");
    }
    if (field == "kickoff_time" || field == "start_time") {
        return tr("Enter the time as HH:mm, for example 18:30, or leave it empty.");
    }
    if (field == "duration_min") {
        return tr("Enter a duration from %1 to %2 minutes.")
            .arg(core::kMinSessionDuration)
            .arg(core::kMaxSessionDuration);
    }
    if (field == "rpe") {
        return tr("Enter an RPE from 0 to %1, only for a player who was present.")
            .arg(core::kMaxRpe);
    }
    if (field == "minutes_override") {
        return tr("Enter minutes from 0 to %1, only for a player who was present.")
            .arg(core::kMaxAttendanceMinutes);
    }
    if (field == "player_id") {
        return tr("This player cannot be saved in this session. Open the session again.");
    }
    if (field == "template_name") {
        return tr("Enter a template name, up to 50 characters.");
    }
    if (field == "tactic_file_size") {
        return tr("This file is too large to be a tactics file. Choose a .tactic.json file.");
    }
    if (field == "lineup_shape") {
        return tr("This lineup cannot be saved. Open the match again and set up the lineup.");
    }
    if (field == "match_format") {
        return tr("Choose 1 to 4 parts of 10 to 60 minutes, at most 130 minutes in all.");
    }
    if (field == "minute_not_played") {
        return tr("This minute has not been played yet. Enter a minute up to the current one.");
    }
    if (field == "second_yellow_without_red") {
        return tr("This yellow card would make another one the second, without its red card. "
                  "Check the cards of the player.");
    }
    if (field == "minute_required") {
        return tr("Enter the minute of the action.");
    }
    if (field == "minute_out_of_match") {
        return tr("This minute is outside the match.");
    }
    if (field == "clock_time") {
        return tr("The clock can be set to at most 30 minutes past the end of the part.");
    }
    if (field == "clock_before_event") {
        return tr("The clock cannot go back before an event of this part. Undo the event first.");
    }
    if (field == "note_text") {
        return tr("Write the note, up to 500 characters.");
    }
    if (field == "player_not_on_pitch") {
        return tr("This player is not on the pitch. Choose a player on the pitch.");
    }
    if (field == "player_not_on_bench") {
        return tr("This player is already on the pitch. Choose a player from the bench.");
    }
    if (field == "player_substituted") {
        return tr("This player has been substituted and cannot come back.");
    }
    if (field == "player_sent_off") {
        return tr("This player has been sent off and cannot be substituted.");
    }
    if (field == "same_player") {
        return tr("Choose two different players.");
    }
    if (field == "player_unknown" || field == "player_missing") {
        return tr("This player is not in the lineup of the match. Choose another player.");
    }
    if (field == "rating") {
        return tr("Enter a rating from 1 to 10.");
    }
    if (field == "report_note") {
        return tr("Write the note of the report, up to 2000 characters.");
    }
    if (field == "minute_out_of_range" || field == "after_final_minute") {
        return tr("The minute of this event is outside the match. Check the clock and try again.");
    }
    // Custom metrics (P17).
    if (field == "name_pl" || field == "name_en") {
        return tr("A metric name can have up to 50 characters.");
    }
    if (field == "key") {
        return tr("The name needs at least one Latin letter or digit.");
    }
    if (field == "unit") {
        return tr("The unit can have up to 20 characters.");
    }
    if (field == "min" || field == "max") {
        return tr("Enter a number such as 4,35, with the maximum not below the minimum. A "
                  "whole-number metric needs whole limits; a yes/no metric has none.");
    }
    if (field == "aggregation") {
        return tr("A yes/no metric can be summed, averaged or show its last answer.");
    }
    if (field == "value" || field == "value_range") {
        return tr("This value does not fit the metric. Check its type and range.");
    }
    if (field == "scope" || field == "team") {
        return tr("This metric is entered for another kind of event. Choose the metric again.");
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
    if (field == "import_matches_changed") {
        return tr("The data in the app changed since the rows were checked, so nothing was "
                  "imported. The report shows what the import would do now; check it and import "
                  "again.");
    }
    if (field == "import_undone") {
        return tr("This import has already been undone.");
    }
    if (field == "last_coach") {
        return tr("The club must keep an active coach. Make someone else a coach first.");
    }
    if (field == "shirt_number") {
        return tr("Another player already has this shirt number. Choose a different one.");
    }
    if (field == "opponent_name") {
        return tr("This opponent already exists. Choose it from the list.");
    }
    if (field == "match_not_planned") {
        return tr("This match has already started or finished, so it cannot be changed here.");
    }
    if (field == "match_in_use") {
        return tr("This match has data attached to it, such as metrics, so it cannot be deleted.");
    }
    if (field == "template_name") {
        return tr("A template with this name already exists. Choose a different name.");
    }
    if (field == "another_live_match") {
        return tr("Another match of this team is live. Finish it in Match Day first.");
    }
    if (field == "no_lineup") {
        return tr("This match has no saved lineup. Set up and save the lineup in Pre-match first.");
    }
    if (field == "lineup_errors") {
        return tr("The lineup of this match has errors. Fix them in Pre-match first.");
    }
    if (field == "match_not_live") {
        return tr("This match is not live. Open Match Day again.");
    }
    if (field == "match_not_running") {
        return tr("No half is running. Start the half first; in the break only substitutions "
                  "can be recorded.");
    }
    if (field == "clockless") {
        return tr("This match runs without the clock: type the minute of every action.");
    }
    if (field == "clock_not_running") {
        return tr("The clock can be set only while a part is played.");
    }
    if (field == "match_not_over") {
        return tr("With 7 players or more the match ends with the clock, at the end of its last "
                  "part.");
    }
    if (field == "tactic_change_later") {
        return tr("A later change of shape is already recorded. A change can be entered only at "
                  "or after the minute of the last one.");
    }
    if (field == "too_few_players") {
        return tr("Fewer than 7 players are on the pitch, so the match cannot go on. End the match "
                  "or undo the last action.");
    }
    if (field == "event_conflict") {
        return tr("This event conflicts with a later one. Undo or correct the later event first.");
    }
    if (field == "match_not_finished") {
        return tr("This match is not finished. Finish it in Match Day first.");
    }
    if (field == "not_correctable") {
        return tr("The events of this match cannot be corrected: its stored minutes do not follow "
                  "from them.");
    }
    if (field == "event_dependent") {
        return tr("A later event needs this one. Delete the later event first.");
    }
    if (field == "undo_end_another_live") {
        return tr("Another match of this team is live. Finish it or undo its start first.");
    }
    if (field == "match_started") {
        return tr("The match has kicked off, so it cannot go back to planned. Undo the kick-off "
                  "first.");
    }
    if (field == "session_in_use") {
        return tr("This session has metric values attached to it, so it cannot be deleted.");
    }
    if (field == "key" || field == "name_pl" || field == "name_en") {
        return tr("A metric with a similar name already exists. Choose another name.");
    }
    if (field == "metric_in_use") {
        return tr("This metric already has values, so it cannot be deleted: the values would be "
                  "lost. Clear its values in the Metrics section first.");
    }
    if (field == "metric_locked") {
        return tr("This metric already has values, so its type and what it is entered for cannot "
                  "change. Create a new metric instead.");
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
