-- FootballDSS schema, version 4: match report (packet P14). docs/SPEC.md section 6 documents it.
--
-- match.report_note: the note of the match report, plain text of 1-2000 characters after trimming
-- (the service trims; empty is NULL). Ratings and RPE live in appearance since version 1.

ALTER TABLE match ADD COLUMN report_note TEXT
    CHECK (report_note IS NULL OR length(trim(report_note)) BETWEEN 1 AND 2000);
