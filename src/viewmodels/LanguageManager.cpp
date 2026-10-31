#include "viewmodels/LanguageManager.hpp"

#include <QCoreApplication>
#include <QLocale>
#include <QQmlEngine>
#include <QSettings>
#include <QTranslator>

namespace fdss::viewmodels {

namespace {
constexpr auto kLanguageKey = "ui/language";
const QStringList kAvailableLanguages{QStringLiteral("pl"), QStringLiteral("en")};
const QString kSourceLanguage = QStringLiteral("en");
} // namespace

LanguageManager::LanguageManager(QQmlEngine* engine, QObject* parent)
    : QObject(parent), m_engine(engine) {
    const QString language = initialLanguage();
    // Source strings are English, so without a translator the UI is English.
    m_language = installTranslator(language) ? language : kSourceLanguage;
}

LanguageManager::~LanguageManager() {
    if (m_translator) {
        QCoreApplication::removeTranslator(m_translator.get());
    }
}

LanguageManager* LanguageManager::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) {
    Q_UNUSED(jsEngine)
    // Parentless: the QML engine takes ownership of the singleton.
    return new LanguageManager(qmlEngine);
}

QString LanguageManager::language() const { return m_language; }

QStringList LanguageManager::availableLanguages() const { return kAvailableLanguages; }

void LanguageManager::setLanguage(const QString& language) {
    if (language == m_language) {
        return;
    }
    if (!kAvailableLanguages.contains(language)) {
        qWarning("LanguageManager: unsupported language '%s', expected one of: %s",
                 qUtf8Printable(language), qUtf8Printable(kAvailableLanguages.join(u", ")));
        return;
    }
    if (!installTranslator(language)) {
        return;
    }
    m_language = language;
    QSettings().setValue(kLanguageKey, m_language);
    if (m_engine) {
        m_engine->retranslate();
    }
    emit languageChanged();
}

QString LanguageManager::initialLanguage() const {
    const QString stored = QSettings().value(kLanguageKey).toString();
    if (kAvailableLanguages.contains(stored)) {
        return stored;
    }
    return QLocale::system().language() == QLocale::Polish ? QStringLiteral("pl") : kSourceLanguage;
}

bool LanguageManager::installTranslator(const QString& language) {
    const QString path = QStringLiteral(":/i18n/FootballDSS_%1.qm").arg(language);
    auto translator = std::make_unique<QTranslator>();
    if (!translator->load(path)) {
        qWarning("LanguageManager: cannot load translation '%s'", qUtf8Printable(path));
        return false;
    }
    if (m_translator) {
        QCoreApplication::removeTranslator(m_translator.get());
    }
    QCoreApplication::installTranslator(translator.get());
    m_translator = std::move(translator);
    return true;
}

} // namespace fdss::viewmodels
