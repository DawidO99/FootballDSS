#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

class QQmlEngine;
class QJSEngine;
class QTranslator;

namespace fdss::viewmodels {

// Owns the UI language: installs the translator, retranslates QML and persists the choice.
// Exactly one instance per engine, created by the QML engine through create().
class LanguageManager : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString language READ language NOTIFY languageChanged FINAL)
    Q_PROPERTY(QStringList availableLanguages READ availableLanguages CONSTANT FINAL)

public:
    explicit LanguageManager(QQmlEngine* engine, QObject* parent = nullptr);
    ~LanguageManager() override;

    static LanguageManager* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    [[nodiscard]] QString language() const;
    [[nodiscard]] QStringList availableLanguages() const;

    // Accepts a code from availableLanguages; anything else is reported and ignored.
    Q_INVOKABLE void setLanguage(const QString& language);

signals:
    void languageChanged();

private:
    [[nodiscard]] QString initialLanguage() const;
    [[nodiscard]] bool installTranslator(const QString& language);

    QPointer<QQmlEngine> m_engine;
    std::unique_ptr<QTranslator> m_translator;
    QString m_language;
};

} // namespace fdss::viewmodels
