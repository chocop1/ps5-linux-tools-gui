#include "config.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QPalette>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleFactory>
#include <QTextStream>
#include <atomic>

namespace ps5 {

static std::atomic<bool> g_isRu{false};
static QString g_initialStyleName;
static QPalette g_initialPalette;
static bool g_hasInitialPalette = false;

QString normalizeLangCode(const QString& raw) {
    if (raw.trimmed().isEmpty()) return {};
    QString first = raw.split(':').first().split(',').first().trimmed();
    QString primary = first.split('_').first().split('-').first().split('.').first().toLower();
    if (primary.isEmpty() || primary == "c" || primary == "posix")
        return {};
    return (primary == "ru") ? "ru" : "en";
}

static QMap<QString, QString> parseIniFile(const QString& path, const QStringList& sectionPrefixes) {
    QMap<QString, QString> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;
    QTextStream in(&f);
    QString section;
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith(';'))
            continue;
        if (line.startsWith('[') && line.endsWith(']')) {
            section = line.mid(1, line.size() - 2);
            continue;
        }
        int eq = line.indexOf('=');
        if (eq <= 0) continue;
        bool want = sectionPrefixes.isEmpty();
        for (const QString& pref : sectionPrefixes) {
            if (section == pref || section.startsWith(pref)) {
                want = true;
                break;
            }
        }
        if (want) {
            out[section + "." + line.left(eq).trimmed()] = line.mid(eq + 1).trimmed();
        }
    }
    return out;
}

QString detectSystemLanguage(QString* outSource) {
    auto finish = [&](const QString& code, const QString& src) -> QString {
        if (outSource) *outSource = src;
        return code;
    };

    const QMap<QString, QString> plasma = parseIniFile(
        QDir::homePath() + "/.config/plasma-localerc", {"Translations", "Formats"});
    for (const char* k : {"Translations.Language", "Translations.LANGUAGE",
                             "Formats.LANG", "Formats.LC_MESSAGES"}) {
        QString v = plasma.value(k).trimmed();
        QString c = normalizeLangCode(v);
        if (!c.isEmpty())
            return finish(c, QString("plasma-localerc (%1)").arg(v));
    }

    const QMap<QString, QString> kde = parseIniFile(
        QDir::homePath() + "/.config/kdeglobals", {"Translations"});
    for (const char* k : {"Translations.Language", "Translations.LANGUAGE"}) {
        QString v = kde.value(k).trimmed();
        QString c = normalizeLangCode(v);
        if (!c.isEmpty())
            return finish(c, QString("kdeglobals (%1)").arg(v));
    }

    for (const char* envName : {"LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"}) {
        QString v = QString::fromLocal8Bit(qgetenv(envName)).trimmed();
        QString c = normalizeLangCode(v);
        if (!c.isEmpty())
            return finish(c, QString("%1=%2").arg(envName, v));
    }

    QString qloc = QLocale::system().name();
    QString c = normalizeLangCode(qloc);
    return finish(c.isEmpty() ? "en" : c, QString("QLocale (%1)").arg(qloc));
}

QString getLanguageMode() {
    QSettings s(ORG_NAME, SETTINGS_NAME);
    QString mode = s.value("app/language", "system").toString().trimmed().toLower();
    if (mode == "ru" || mode == "en" || mode == "system")
        return mode;
    return "system";
}

QString resolveEffectiveLanguage(const QString& mode) {
    QString m = mode.isEmpty() ? getLanguageMode() : mode;
    if (m == "ru") return "ru";
    if (m == "en") return "en";
    return detectSystemLanguage(nullptr);
}

QString setActiveLanguage(const QString& mode, bool persist) {
    QString m = (mode == "ru" || mode == "en" || mode == "system") ? mode : QString("system");
    if (persist) {
        QSettings s(ORG_NAME, SETTINGS_NAME);
        s.setValue("app/language", m);
        s.sync();
    }
    QString eff = resolveEffectiveLanguage(m);
    g_isRu.store(eff == "ru", std::memory_order_relaxed);
    QLocale::setDefault(QLocale(eff == "ru" ? "ru_RU" : "en_US"));
    return eff;
}

bool isRu() {
    return g_isRu.load(std::memory_order_relaxed);
}

QString tx(const char* ru, const char* en) {
    return isRu() ? QString::fromUtf8(ru) : QString::fromUtf8(en);
}

QString tx(const QString& ru, const QString& en) {
    return isRu() ? ru : en;
}

QVector<ServiceDef> ps5Services() {
    return {
        {"ps5fan.service", tx("Контроль вентилятора PS5 (--fan on)", "PS5 fan control (--fan on)")},
        {"ps5boost.service", tx("Режим производительности Boost (--boost on)", "Performance Boost mode (--boost on)")},
        {"ps5-bt-quiet.service", tx("Отключение битого hci и запуск рабочего Bluetooth", "Disable faulty HCI and start working Bluetooth")},
        {"ps5-bt-sync.service", tx("Синхронизация сопряжений DualSense с PS5 OS", "Sync DualSense pairings with PS5 OS")},
        {"ps5-stage-firmware.service", tx("Загрузка прошивки Wi-Fi NXP IW620 с EFI", "Stage NXP IW620 Wi-Fi firmware from EFI")},
        {"ps5-stage-gpu-firmware.service", tx("Загрузка прошивки GPU PS5 Pro (Viola) с EFI", "Stage PS5 Pro (Viola) GPU firmware from EFI")},
        {"ps5-fan-curve.service", tx("Пользовательская кривая вентилятора (ps5_fan)", "Custom fan curve (ps5_fan)")},
    };
}

QStringList allowedUnits() {
    return {
        "ps5fan.service",
        "ps5boost.service",
        "ps5-bt-quiet.service",
        "ps5-bt-sync.service",
        "ps5-stage-firmware.service",
        "ps5-stage-gpu-firmware.service",
        "ps5-fan-curve.service",
    };
}

static bool isFusePath(const QString& path) {
    return path.startsWith("/tmp/.mount_");
}

static QString stageToExt4(const QString& srcPath) {
    QString cacheDir = QDir::homePath() + "/.local/share/ps5tools/bin";
    QDir().mkpath(cacheDir);
    QFileInfo fi(srcPath);
    QString dstPath = cacheDir + "/" + fi.fileName();

    QFile src(srcPath);
    if (!src.open(QIODevice::ReadOnly)) return srcPath;
    QByteArray srcBytes = src.readAll();
    QByteArray srcHash = QCryptographicHash::hash(srcBytes, QCryptographicHash::Sha256);

    bool needCopy = true;
    QFile dstCheck(dstPath);
    if (dstCheck.exists() && dstCheck.open(QIODevice::ReadOnly)) {
        QByteArray dstHash = QCryptographicHash::hash(dstCheck.readAll(), QCryptographicHash::Sha256);
        needCopy = (dstHash != srcHash);
    }
    if (needCopy) {
        QFile::remove(dstPath);
        QFile out(dstPath);
        if (out.open(QIODevice::WriteOnly)) {
            out.write(srcBytes);
            out.close();
        }
    }
    QFile::setPermissions(dstPath,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
        QFileDevice::ReadGroup | QFileDevice::ExeGroup |
        QFileDevice::ReadOther | QFileDevice::ExeOther);
    return dstPath;
}

QString resolveTool(const QString& name) {
    const QStringList sysDirs = {
        "/usr/local/sbin", "/usr/local/bin", "/usr/local/libexec", "/usr/sbin", "/usr/bin"
    };
    for (const QString& dir : sysDirs) {
        QString cand = dir + "/" + name;
        if (QFileInfo::exists(cand) && QFileInfo(cand).isFile())
            return cand;
    }

    QStringList envDirs;
    QByteArray appimg = qgetenv("APPIMAGE");
    if (!appimg.isEmpty())
        envDirs << QFileInfo(QString::fromLocal8Bit(appimg)).absolutePath();
    QByteArray owd = qgetenv("OWD");
    if (!owd.isEmpty())
        envDirs << QString::fromLocal8Bit(owd);

    for (const QString& base : envDirs) {
        for (const QString& cand : {base + "/ps5-linux-tools/" + name, base + "/" + name}) {
            if (QFileInfo::exists(cand) && QFileInfo(cand).isFile() && !isFusePath(cand))
                return cand;
        }
    }

    QString appDir = QCoreApplication::applicationDirPath();
    const QStringList localCands = {
        appDir + "/ps5-linux-tools/" + name,
        appDir + "/" + name,
        appDir + "/../lib/ps5tools/ps5-linux-tools/" + name,
        appDir + "/../lib/ps5tools/" + name,
    };
    for (const QString& cand : localCands) {
        QString clean = QDir::cleanPath(cand);
        if (QFileInfo::exists(clean) && QFileInfo(clean).isFile()) {
            return isFusePath(clean) ? stageToExt4(clean) : clean;
        }
    }

    QString found = QStandardPaths::findExecutable(name);
    return found.isEmpty() ? name : found;
}

QString resolveAsset(const QString& name) {
    QString appDir = QCoreApplication::applicationDirPath();
    const QStringList cands = {
        appDir + "/" + name,
        appDir + "/../lib/ps5tools/" + name,
        appDir + "/../share/icons/hicolor/scalable/apps/" + name,
        "/usr/share/icons/hicolor/scalable/apps/" + name,
        "/usr/share/ps5tools/" + name,
    };
    for (const QString& c : cands) {
        QString clean = QDir::cleanPath(c);
        if (QFileInfo::exists(clean) && QFileInfo(clean).isFile())
            return clean;
    }
    return QString();
}

bool isPolkitHelperInstalled() {
    return QFileInfo::exists(HELPER_INSTALL_PATH) && QFileInfo::exists(POLICY_INSTALL_PATH);
}

QMap<QString, bool> getSystemToolsStatus() {
    return {
        {"ps5_control", QFileInfo::exists("/usr/local/sbin/ps5_control")},
        {"m2_init", QFileInfo::exists("/usr/local/sbin/m2_init")},
        {"m2_install.sh", QFileInfo::exists("/usr/local/sbin/m2_install.sh")},
        {"m2_exec.sh", QFileInfo::exists("/usr/local/sbin/m2_exec.sh")},
        {"ps5-linux-warm-reboot.sh", QFileInfo::exists("/usr/local/sbin/ps5-linux-warm-reboot.sh")},
        {"helper", QFileInfo::exists(HELPER_INSTALL_PATH)},
        {"policy", QFileInfo::exists(POLICY_INSTALL_PATH)},
    };
}

static QColor colorFromStr(const QString& val, const QColor& fallback) {
    if (val.trimmed().isEmpty()) return fallback;
    QString v = val.trimmed();
    if (v.startsWith('#')) {
        QColor c(v);
        return c.isValid() ? c : fallback;
    }
    const QStringList p = v.split(',');
    if (p.size() < 3) return fallback;
    bool ok1 = false, ok2 = false, ok3 = false;
    int r = p[0].trimmed().toInt(&ok1);
    int g = p[1].trimmed().toInt(&ok2);
    int b = p[2].trimmed().toInt(&ok3);
    if (!ok1 || !ok2 || !ok3) return fallback;
    QColor c(r, g, b);
    return c.isValid() ? c : fallback;
}

static void deriveBevelAndDisabled(QPalette& pal, const QColor& btnBg, const QColor& winFg,
                                   const QColor& textFg, const QColor& btnFg, const QColor& inactiveFg) {
    const bool isDark = btnBg.lightness() < 128;
    pal.setColor(QPalette::Light, btnBg.lighter(isDark ? 165 : 135));
    pal.setColor(QPalette::Midlight, btnBg.lighter(isDark ? 125 : 112));
    pal.setColor(QPalette::Dark, btnBg.darker(isDark ? 160 : 145));
    pal.setColor(QPalette::Mid, btnBg.darker(isDark ? 125 : 118));
    pal.setColor(QPalette::Shadow, btnBg.darker(isDark ? 230 : 260));

    pal.setColor(QPalette::Disabled, QPalette::WindowText, inactiveFg);
    pal.setColor(QPalette::Disabled, QPalette::Text, inactiveFg);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, inactiveFg);
    pal.setColor(QPalette::Disabled, QPalette::Highlight, pal.color(QPalette::Mid));
    pal.setColor(QPalette::Disabled, QPalette::HighlightedText, winFg);

    pal.setColor(QPalette::Inactive, QPalette::WindowText, winFg);
    pal.setColor(QPalette::Inactive, QPalette::Text, textFg);
    pal.setColor(QPalette::Inactive, QPalette::ButtonText, btnFg);
}

static QPalette buildPaletteFromIni(const QMap<QString, QString>& colors, const QPalette* basePal = nullptr) {
    QPalette pal = basePal ? *basePal : QPalette();
    if (colors.isEmpty()) return pal;

    QColor winBg = colorFromStr(colors.value("Colors:Window.BackgroundNormal"), pal.color(QPalette::Window));
    QColor winFg = colorFromStr(colors.value("Colors:Window.ForegroundNormal"), pal.color(QPalette::WindowText));
    QColor winInact = colorFromStr(colors.value("Colors:Window.ForegroundInactive"),
                                   QColor(winBg.lightness() < 128 ? "#7a8580" : "#6e7377"));

    QColor viewBg = colorFromStr(colors.value("Colors:View.BackgroundNormal"), pal.color(QPalette::Base));
    QColor viewAlt = colorFromStr(colors.value("Colors:View.BackgroundAlternate"), viewBg.darker(106));
    QColor viewFg = colorFromStr(colors.value("Colors:View.ForegroundNormal"), winFg);
    QColor viewInact = colorFromStr(colors.value("Colors:View.ForegroundInactive"), winInact);
    QColor linkFg = colorFromStr(colors.value("Colors:View.ForegroundLink"), QColor("#2980b9"));
    QColor visitFg = colorFromStr(colors.value("Colors:View.ForegroundVisited"), QColor("#8e44ad"));

    QColor btnBg = colorFromStr(colors.value("Colors:Button.BackgroundNormal"), winBg);
    QColor btnFg = colorFromStr(colors.value("Colors:Button.ForegroundNormal"), winFg);

    QColor selBg = colorFromStr(colors.value("Colors:Selection.BackgroundNormal"), QColor("#3daee9"));
    QColor selFg = colorFromStr(colors.value("Colors:Selection.ForegroundNormal"), QColor("#ffffff"));

    QColor tipBg = colorFromStr(colors.value("Colors:Tooltip.BackgroundNormal"), winBg);
    QColor tipFg = colorFromStr(colors.value("Colors:Tooltip.ForegroundNormal"), winFg);

    for (QPalette::ColorGroup grp : {QPalette::Active, QPalette::Inactive}) {
        pal.setColor(grp, QPalette::Window, winBg);
        pal.setColor(grp, QPalette::WindowText, winFg);
        pal.setColor(grp, QPalette::Base, viewBg);
        pal.setColor(grp, QPalette::AlternateBase, viewAlt);
        pal.setColor(grp, QPalette::Text, viewFg);
        pal.setColor(grp, QPalette::PlaceholderText, viewInact);
        pal.setColor(grp, QPalette::Button, btnBg);
        pal.setColor(grp, QPalette::ButtonText, btnFg);
        pal.setColor(grp, QPalette::BrightText, QColor(winBg.lightness() < 128 ? "#ffffff" : "#000000"));
        pal.setColor(grp, QPalette::Highlight, selBg);
        pal.setColor(grp, QPalette::HighlightedText, selFg);
        pal.setColor(grp, QPalette::ToolTipBase, tipBg);
        pal.setColor(grp, QPalette::ToolTipText, tipFg);
        pal.setColor(grp, QPalette::Link, linkFg);
        pal.setColor(grp, QPalette::LinkVisited, visitFg);
    }

    deriveBevelAndDisabled(pal, btnBg, winFg, viewFg, btnFg, winInact);
    return pal;
}

static QPair<QPalette, QMap<QString, QString>> builtinWin9xScheme() {
    QMap<QString, QString> colors = {
        {"Colors:Window.BackgroundNormal", "212,208,200"},
        {"Colors:Window.ForegroundNormal", "0,0,0"},
        {"Colors:Window.ForegroundInactive", "90,90,90"},
        {"Colors:Window.ForegroundPositive", "0,122,0"},
        {"Colors:Window.ForegroundNeutral", "154,91,0"},
        {"Colors:Window.ForegroundNegative", "176,0,0"},
        {"Colors:View.BackgroundNormal", "255,255,255"},
        {"Colors:View.BackgroundAlternate", "242,242,240"},
        {"Colors:View.ForegroundNormal", "0,0,0"},
        {"Colors:View.ForegroundInactive", "110,110,110"},
        {"Colors:View.ForegroundLink", "0,0,192"},
        {"Colors:View.ForegroundVisited", "128,0,128"},
        {"Colors:Button.BackgroundNormal", "212,208,200"},
        {"Colors:Button.ForegroundNormal", "0,0,0"},
        {"Colors:Selection.BackgroundNormal", "0,0,128"},
        {"Colors:Selection.ForegroundNormal", "255,255,255"},
        {"Colors:Tooltip.BackgroundNormal", "255,255,225"},
        {"Colors:Tooltip.ForegroundNormal", "0,0,0"},
        {"General.AccentColor", "0,0,128"},
    };
    return {buildPaletteFromIni(colors), colors};
}

static QPair<QPalette, QMap<QString, QString>> builtinFusionDarkScheme() {
    QMap<QString, QString> colors = {
        {"Colors:Window.BackgroundNormal", "43,43,43"},
        {"Colors:Window.ForegroundNormal", "238,238,238"},
        {"Colors:Window.ForegroundInactive", "150,155,160"},
        {"Colors:Window.ForegroundPositive", "46,204,113"},
        {"Colors:Window.ForegroundNeutral", "243,156,18"},
        {"Colors:Window.ForegroundNegative", "231,76,60"},
        {"Colors:View.BackgroundNormal", "30,30,30"},
        {"Colors:View.BackgroundAlternate", "38,38,38"},
        {"Colors:View.ForegroundNormal", "238,238,238"},
        {"Colors:View.ForegroundInactive", "145,150,155"},
        {"Colors:View.ForegroundLink", "61,174,233"},
        {"Colors:View.ForegroundVisited", "155,89,182"},
        {"Colors:Button.BackgroundNormal", "53,53,53"},
        {"Colors:Button.ForegroundNormal", "238,238,238"},
        {"Colors:Selection.BackgroundNormal", "48,140,198"},
        {"Colors:Selection.ForegroundNormal", "255,255,255"},
        {"Colors:Tooltip.BackgroundNormal", "35,35,35"},
        {"Colors:Tooltip.ForegroundNormal", "238,238,238"},
        {"General.AccentColor", "61,174,233"},
    };
    return {buildPaletteFromIni(colors), colors};
}

static QPair<QPalette, QMap<QString, QString>> builtinFusionLightScheme() {
    QMap<QString, QString> colors = {
        {"Colors:Window.BackgroundNormal", "239,239,239"},
        {"Colors:Window.ForegroundNormal", "24,26,28"},
        {"Colors:Window.ForegroundInactive", "105,112,119"},
        {"Colors:Window.ForegroundPositive", "28,132,72"},
        {"Colors:Window.ForegroundNeutral", "180,92,0"},
        {"Colors:Window.ForegroundNegative", "196,43,58"},
        {"Colors:View.BackgroundNormal", "255,255,255"},
        {"Colors:View.BackgroundAlternate", "245,246,248"},
        {"Colors:View.ForegroundNormal", "24,26,28"},
        {"Colors:View.ForegroundInactive", "110,115,122"},
        {"Colors:View.ForegroundLink", "41,128,185"},
        {"Colors:View.ForegroundVisited", "142,68,173"},
        {"Colors:Button.BackgroundNormal", "232,233,235"},
        {"Colors:Button.ForegroundNormal", "24,26,28"},
        {"Colors:Selection.BackgroundNormal", "48,140,198"},
        {"Colors:Selection.ForegroundNormal", "255,255,255"},
        {"Colors:Tooltip.BackgroundNormal", "255,255,235"},
        {"Colors:Tooltip.ForegroundNormal", "24,26,28"},
        {"General.AccentColor", "41,128,185"},
    };
    return {buildPaletteFromIni(colors), colors};
}

void captureInitialTheme(QApplication& app) {
    if (g_initialStyleName.isEmpty() && app.style())
        g_initialStyleName = app.style()->objectName();
    if (!g_hasInitialPalette) {
        g_initialPalette = app.palette();
        g_hasInitialPalette = true;
    }
}

QString detectSystemStyleName() {
    if (!g_initialStyleName.isEmpty())
        return g_initialStyleName;
    const QMap<QString, QString> kde = parseIniFile(QDir::homePath() + "/.config/kdeglobals", {"KDE"});
    QString ws = kde.value("KDE.widgetStyle");
    if (!ws.isEmpty()) return ws;
    QString lnf = kde.value("KDE.LookAndFeelPackage").toLower();
    if (lnf.contains("oxygen")) return "Oxygen";
    if (lnf.contains("union")) return "Union";
    return "Breeze";
}

static QString matchFactoryKey(const QString& target) {
    const QString low = target.toLower();
    for (const QString& k : QStyleFactory::keys()) {
        if (k.toLower() == low) return k;
    }
    return {};
}

QVector<StyleChoice> availableQtStyles() {
    QString sys = detectSystemStyleName();
    QString sysDisp = sys.isEmpty() ? QString("KDE") : (sys[0].toUpper() + sys.mid(1));
    return {
        {"system", tx(QString("Как в системе (%1)").arg(sysDisp), QString("Follow system (%1)").arg(sysDisp)), true},
        {"Breeze", "KDE Breeze", !matchFactoryKey("Breeze").isEmpty()},
        {"Oxygen", "KDE Oxygen", !matchFactoryKey("Oxygen").isEmpty()},
        {"Union", "KDE Union", !matchFactoryKey("Union").isEmpty()},
        {"Fusion", "Qt Fusion", !matchFactoryKey("Fusion").isEmpty()},
        {"Windows", "Microsoft Windows 9x", !matchFactoryKey("Windows").isEmpty()},
    };
}

QVector<ColorSchemeChoice> discoverColorSchemes() {
    const QMap<QString, QString> kde = parseIniFile(QDir::homePath() + "/.config/kdeglobals", {"General"});
    QString sysScheme = kde.value("General.ColorScheme");
    if (sysScheme.isEmpty()) sysScheme = "kdeglobals";

    QVector<ColorSchemeChoice> items = {
        {"system", tx(QString("Как в системе (%1)").arg(sysScheme), QString("Follow system (%1)").arg(sysScheme))},
        {"auto", tx("Авто (палитра стиля)", "Auto (style default)")},
        {"builtin:win9x", "Microsoft Windows 9x Classic"},
        {"builtin:fusion-dark", "Fusion Dark"},
        {"builtin:fusion-light", "Fusion Light"},
    };

    QStringList seen;
    const QStringList dirs = {
        QDir::homePath() + "/.local/share/color-schemes",
        "/usr/share/color-schemes",
    };
    for (const QString& dirPath : dirs) {
        QDir dir(dirPath);
        if (!dir.exists()) continue;
        const QFileInfoList files = dir.entryInfoList({"*.colors"}, QDir::Files, QDir::Name);
        for (const QFileInfo& fi : files) {
            QString stem = fi.completeBaseName();
            if (seen.contains(stem)) continue;
            seen.append(stem);
            const QMap<QString, QString> ini = parseIniFile(fi.absoluteFilePath(), {"General"});
            QString name = isRu() ? ini.value("General.Name[ru]") : ini.value("General.Name");
            if (name.isEmpty()) name = ini.value("General.Name");
            if (name.isEmpty()) name = stem;
            items.append({"file:" + fi.absoluteFilePath(), name});
        }
    }
    return items;
}

QString findColorSchemeByStem(const QString& stem) {
    const QStringList dirs = {
        QDir::homePath() + "/.local/share/color-schemes",
        "/usr/share/color-schemes",
    };
    for (const QString& dirPath : dirs) {
        QDir dir(dirPath);
        if (!dir.exists()) continue;
        const QFileInfoList files = dir.entryInfoList({"*.colors"}, QDir::Files);
        for (const QFileInfo& fi : files) {
            if (fi.completeBaseName().compare(stem, Qt::CaseInsensitive) == 0)
                return "file:" + fi.absoluteFilePath();
        }
    }
    return {};
}

QPair<QString, QString> getThemeSettings() {
    QSettings s(ORG_NAME, SETTINGS_NAME);
    QString st = s.value("app/theme_style", "system").toString().trimmed();
    QString sc = s.value("app/color_scheme", "system").toString().trimmed();
    if (st.isEmpty()) st = "system";
    if (sc.isEmpty()) sc = "system";
    return {st, sc};
}

void applyAppAppearance(QApplication& app, KDETheme& theme,
                          const QString& styleModeIn, const QString& schemeModeIn, bool persist) {
    auto cur = getThemeSettings();
    QString styleMode = styleModeIn.isEmpty() ? cur.first : styleModeIn;
    QString schemeMode = schemeModeIn.isEmpty() ? cur.second : schemeModeIn;

    if (persist) {
        QSettings s(ORG_NAME, SETTINGS_NAME);
        s.setValue("app/theme_style", styleMode);
        s.setValue("app/color_scheme", schemeMode);
        s.sync();
    }

    QString targetName = (styleMode == "system") ? detectSystemStyleName() : styleMode;
    QString factoryKey = matchFactoryKey(targetName);
    if (factoryKey.isEmpty()) factoryKey = matchFactoryKey("Breeze");
    if (factoryKey.isEmpty()) factoryKey = matchFactoryKey("Fusion");

    QStyle* styleObj = factoryKey.isEmpty() ? nullptr : QStyleFactory::create(factoryKey);
    if (styleObj) app.setStyle(styleObj);

    const QMap<QString, QString> kdeIni = parseIniFile(
        QDir::homePath() + "/.config/kdeglobals", {"Colors:", "General", "KDE"});
    QMap<QString, QString> colorsMap;
    QPalette pal = app.palette();

    auto baseSysPal = [&]() -> const QPalette* {
        return g_hasInitialPalette ? &g_initialPalette : nullptr;
    };

    if (schemeMode == "system") {
        colorsMap = kdeIni;
        if (!kdeIni.isEmpty()) pal = buildPaletteFromIni(kdeIni, baseSysPal());
        else if (g_hasInitialPalette) pal = g_initialPalette;
    } else if (schemeMode == "auto") {
        QString low = factoryKey.toLower();
        if (low == "windows") {
            auto res = builtinWin9xScheme();
            pal = res.first;
            colorsMap = res.second;
        } else if (low == "fusion") {
            bool darkSys = g_hasInitialPalette ? (g_initialPalette.color(QPalette::Window).lightness() < 128) : true;
            auto res = darkSys ? builtinFusionDarkScheme() : builtinFusionLightScheme();
            pal = res.first;
            colorsMap = res.second;
        } else if (low == "oxygen") {
            bool darkSys = g_hasInitialPalette ? (g_initialPalette.color(QPalette::Window).lightness() < 128) : true;
            QString ox = findColorSchemeByStem(darkSys ? "OxygenDark" : "Oxygen");
            if (!ox.isEmpty()) {
                colorsMap = parseIniFile(ox.mid(5), {"Colors:", "General"});
                QPalette stdP = styleObj ? styleObj->standardPalette() : app.palette();
                pal = buildPaletteFromIni(colorsMap, &stdP);
            } else {
                colorsMap = kdeIni;
                pal = buildPaletteFromIni(kdeIni, baseSysPal());
            }
        } else {
            colorsMap = kdeIni;
            if (!kdeIni.isEmpty()) pal = buildPaletteFromIni(kdeIni, baseSysPal());
            else if (styleObj) pal = styleObj->standardPalette();
        }
    } else if (schemeMode == "builtin:win9x") {
        auto res = builtinWin9xScheme();
        pal = res.first;
        colorsMap = res.second;
    } else if (schemeMode == "builtin:fusion-dark") {
        auto res = builtinFusionDarkScheme();
        pal = res.first;
        colorsMap = res.second;
    } else if (schemeMode == "builtin:fusion-light") {
        auto res = builtinFusionLightScheme();
        pal = res.first;
        colorsMap = res.second;
    } else if (schemeMode.startsWith("file:")) {
        QString filePath = schemeMode.mid(5);
        colorsMap = parseIniFile(filePath, {"Colors:", "General"});
        if (!colorsMap.isEmpty()) {
            QPalette stdP = styleObj ? styleObj->standardPalette() : app.palette();
            pal = buildPaletteFromIni(colorsMap, &stdP);
        } else {
            colorsMap = kdeIni;
            if (g_hasInitialPalette) pal = g_initialPalette;
        }
    } else {
        colorsMap = kdeIni;
        if (g_hasInitialPalette) pal = g_initialPalette;
    }

    app.setPalette(pal);
    theme.reload(&colorsMap, &pal);
}

KDETheme::KDETheme() {
    reload();
}

void KDETheme::reload(const QMap<QString, QString>* colorsOverride, const QPalette* paletteOverride) {
    if (colorsOverride) {
        m_colors = *colorsOverride;
    } else {
        m_colors = parseIniFile(QDir::homePath() + "/.config/kdeglobals", {"Colors:", "General", "KDE"});
    }
    if (paletteOverride) {
        m_palette = *paletteOverride;
        m_hasPalette = true;
    } else {
        m_hasPalette = false;
    }
}

QPalette KDETheme::activePalette() const {
    return m_hasPalette ? m_palette : QApplication::palette();
}

QString KDETheme::rgbToHex(const QString& rgb, const QString& fallback) {
    const QStringList p = rgb.split(',');
    if (p.size() < 3) return fallback;
    bool ok1 = false, ok2 = false, ok3 = false;
    int r = p[0].trimmed().toInt(&ok1);
    int g = p[1].trimmed().toInt(&ok2);
    int b = p[2].trimmed().toInt(&ok3);
    if (!ok1 || !ok2 || !ok3) return fallback;
    return QColor(r, g, b).name();
}

QString KDETheme::ensureReadable(const QString& hexColor, const QString& fallback) const {
    QColor c(hexColor);
    if (!c.isValid()) return fallback;
    QColor winBg = activePalette().color(QPalette::Window);
    int h, s, l, a;
    c.getHsl(&h, &s, &l, &a);
    if (winBg.lightness() < 128) {
        if (l < 130) c.setHsl(h, qMax(s, 140), 155, a);
    } else {
        if (l > 125) c.setHsl(h, qMax(s, 150), 95, a);
    }
    return c.name();
}

QString KDETheme::positive() const {
    QString raw = m_colors.value("Colors:Window.ForegroundPositive");
    QString hex = raw.isEmpty() ? "#27ae60" : rgbToHex(raw, "#27ae60");
    return ensureReadable(hex, "#2ecc71");
}

QString KDETheme::negative() const {
    QString raw = m_colors.value("Colors:Window.ForegroundNegative");
    QString hex = raw.isEmpty() ? "#da4453" : rgbToHex(raw, "#da4453");
    return ensureReadable(hex, "#e74c3c");
}

QString KDETheme::neutral() const {
    QString raw = m_colors.value("Colors:Window.ForegroundNeutral");
    QString hex = raw.isEmpty() ? "#f67400" : rgbToHex(raw, "#f67400");
    return ensureReadable(hex, "#f39c12");
}

QString KDETheme::accent() const {
    QString raw = m_colors.value("General.AccentColor");
    if (raw.isEmpty()) raw = m_colors.value("Colors:Window.DecorationFocus");
    if (raw.isEmpty()) raw = m_colors.value("Colors:Selection.BackgroundNormal");
    QString hex = raw.isEmpty() ? activePalette().color(QPalette::Highlight).name() : rgbToHex(raw, "#3daee9");
    return ensureReadable(hex, "#3daee9");
}

QString KDETheme::overlayAccent() const {
    QColor c(accent());
    if (!c.isValid()) return "#3daee9";
    int h, s, l, a;
    c.getHsl(&h, &s, &l, &a);
    if (l < 135) c.setHsl(h, qMax(s, 140), 165, a);
    return c.name();
}

QString KDETheme::muted() const {
    QString raw = m_colors.value("Colors:Window.ForegroundInactive");
    QColor winBg = activePalette().color(QPalette::Window);
    if (!raw.isEmpty()) {
        QColor c = colorFromStr(raw, QColor(winBg.lightness() < 128 ? "#8c9691" : "#5a5f64"));
        return c.name();
    }
    return (winBg.lightness() < 128) ? "#94a09a" : "#555b60";
}

} // namespace ps5
