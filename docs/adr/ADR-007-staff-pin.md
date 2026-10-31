# ADR-007: Staff access with per-user PIN (PBKDF2 via QPasswordDigestor)

**Status:** accepted · 2026-10-31

## Context
Several staff members (coach, assistant, physio, analyst) may share one laptop, and roles decide
what each of them may see and change (SPEC §5). The database is a local, unencrypted SQLite file
(RODO decision: data minimisation and roles instead of encryption). We need a simple login that a
coach on the bench can use in seconds.

## Decision
- Each staff user picks a PIN of 4–8 digits.
- Stored as PBKDF2-HMAC-SHA256: 16-byte random salt (`QRandomGenerator::system()`), 120 000
  iterations, 32-byte key; salt, iterations and key stored per user so parameters can be raised later.
- Implementation: `QPasswordDigestor::deriveKeyPbkdf2` from Qt Network, linked **only** into
  `fdss_services`. The app opens no sockets; Qt Network is used for this function alone.
- Comparison of derived keys in constant time.
- 5 wrong PINs in a row lock the user for 30 seconds (`failed_attempts`, `locked_until`).
- Staff users are deactivated, never deleted; the last active Coach cannot be deactivated or demoted.

## Alternatives
- Plain `QCryptographicHash` (original plan): fast hashes are unsuitable for secrets.
- Own PBKDF2 over `QMessageAuthenticationCode`: no extra module, but hand-written crypto is a risk
  we do not need to take.
- No login, only a profile picker: roles would be decorative.
- Database encryption (SQLCipher): complicates the Windows/macOS build; out of scope (kierunek rozwoju).

## Consequences
- The PIN protects staff members from each other on a shared machine. It does **not** protect
  against someone who copies `club.sqlite`; the thesis states this limitation explicitly.
- `windeployqt` ships `Qt6Network` (~1.5 MB) in the Windows package.
- Login takes roughly 0.1–0.3 s on a typical laptop because of the iteration count; acceptable.
