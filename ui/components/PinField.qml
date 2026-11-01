import QtQuick

// Masked PIN input: only digits go in, 8 at most. AuthService holds the rule itself (4 to 8 digits),
// so a PIN that is too short is reported by the viewmodel through `error`.
// API: as TextInputField. Set Accessible.name to the field label.
TextInputField {
    echoMode: TextInput.Password
    inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
    maximumLength: 8
    validator: RegularExpressionValidator {
        regularExpression: /[0-9]*/
    }
}
