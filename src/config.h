#pragma once

#include <QApplication>
#include <QColor>
#include <QMap>
#include <QPair>
#include <QPalette>
#include <QString>
#include <QStringList>
#include <QVector>

namespace ps5 {

inline constexpr const char* APP_NAME = "PS5 Linux Tools";
inline constexpr const char* ORG_NAME = "ps5linuxtools";
inline constexpr const char* SETTINGS_NAME = "PS5ToolsGUI";
inline constexpr const char* HELPER_INSTALL_PATH = "/usr/local/libexec/ps5tools-helper";
inline constexpr const char* POLICY_INSTALL_PATH = "/usr/share/polkit-1/actions/org.ps5linuxtools.policy";

struct ServiceDef {
    QString unit;
    QString description;
};

struct StyleChoice {
    QString key;
    QString label;
    bool available;
};

struct ColorSchemeChoice {
    QString id;
    QString label;
};

// Localization helpers
bool isRu();
QString tx(const char* ru, const char* en);
QString tx(const QString& ru, const QString& en);
QString normalizeLangCode(const QString& raw);
QString detectSystemLanguage(QString* outSource = nullptr);
QString getLanguageMode();
QString resolveEffectiveLanguage(const QString& mode = QString());
QString setActiveLanguage(const QString& mode, bool persist = true);

QVector<ServiceDef> ps5Services();
QStringList allowedUnits();

QString resolveTool(const QString& name);
QString resolveAsset(const QString& name);
bool isPolkitHelperInstalled();
QMap<QString, bool> getSystemToolsStatus();

class KDETheme {
public:
    KDETheme();
    void reload(const QMap<QString, QString>* colorsOverride = nullptr,
                const QPalette* paletteOverride = nullptr);

    QString positive() const;
    QString negative() const;
    QString neutral() const;
    QString accent() const;
    QString overlayAccent() const;
    QString muted() const;

private:
    QPalette activePalette() const;
    QMap<QString, QString> m_colors;
    QPalette m_palette;
    bool m_hasPalette = false;

    static QString rgbToHex(const QString& rgb, const QString& fallback);
    QString ensureReadable(const QString& hexColor, const QString& fallback) const;
};

// Qt theme style & color palette adaptation
void captureInitialTheme(QApplication& app);
QString detectSystemStyleName();
QVector<StyleChoice> availableQtStyles();
QVector<ColorSchemeChoice> discoverColorSchemes();
QString findColorSchemeByStem(const QString& stem);
QPair<QString, QString> getThemeSettings();
void applyAppAppearance(QApplication& app, KDETheme& theme,
                          const QString& styleMode = QString(),
                          const QString& schemeMode = QString(),
                          bool persist = true);

} // namespace ps5
