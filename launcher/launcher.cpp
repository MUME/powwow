// what the interface of the launcher uses

#include "launcher.h"
#include "gitversion.h"
#if defined(Q_OS_MACOS)
#  include "macos.h"
#endif

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QQuickStyle>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QTemporaryFile>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

static const QString SUFFIX = ".powwow";

// the settings of the launcher, such as the powwow that it runs, under a
// name of their own: those of the application powwow would be powwow.conf
// in ~/.config/powwow on Linux, the user-wide settings file of powwow
static QSettings launcher_settings()
{
    return QSettings("powwow", "launcher");
}

// the folder of the definition files: $POWWOWDIR, or where each system
// keeps the files of an app
static QString default_folder()
{
    QString dir = qEnvironmentVariable("POWWOWDIR");
    if (!dir.isEmpty())
        return dir;
#if defined(Q_OS_WIN)
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        + "/powwow";
#elif defined(Q_OS_MACOS)
    // the folder of the user-wide settings file, as powwow takes it
    QString config = QDir::homePath() + "/.config/powwow";
    if (QFileInfo(config).isDir())
        return config;
    return QDir::homePath() + "/Library/Application Support/powwow";
#else
    QString data = qEnvironmentVariable("XDG_DATA_HOME");
    if (data.isEmpty())
        data = QDir::homePath() + "/.local/share";
    return data + "/powwow";
#endif
}

#if !defined(Q_OS_WIN)
// does the launcher run from its build directory, or an app bundle in it?
static bool in_build_dir()
{
    QString dir = QFileInfo(QCoreApplication::applicationDirPath())
        .canonicalFilePath();
    QString build = QFileInfo(LAUNCHER_BUILD_DIR).canonicalFilePath();
    return !build.isEmpty() && (dir == build || dir.startsWith(build + "/"));
}
#endif

// powwow of the same tree, if the launcher runs from its build directory;
// next to the launcher, as installed with it, or in PATH, or where package
// managers put it, as an app started from the desktop may not have their
// folders in PATH
static QString find_powwow()
{
    QString dir = QCoreApplication::applicationDirPath();
#if !defined(Q_OS_WIN)
    // from its build directory, the powwow of the same tree, which has the
    // options and the manual that it was built with
    QFileInfo built(POWWOW_BUILD_PROGRAM);
    if (in_build_dir() && built.isFile() && built.isExecutable())
        return QDir::cleanPath(built.filePath());
#endif
    const QStringList beside = {
#if defined(Q_OS_WIN)
        dir + "/../bin/powwow.exe",
        dir + "/bin/powwow.exe",
#else
        dir + "/powwow",
#endif
    };
    for (const QString &path : beside) {
        QFileInfo info(path);
        if (info.isFile() && info.isExecutable())
            return QDir::cleanPath(path);
    }
    QString path = QStandardPaths::findExecutable("powwow");
    if (path.isEmpty()) {
        path = QStandardPaths::findExecutable(
            "powwow", { "/opt/homebrew/bin", "/usr/local/bin",
                        "/opt/local/bin", "/usr/pkg/bin" });
    }
    return path;
}

// 'path' as typed in a shell, with ~ for the home folder
static QString expand_path(const QString &typed)
{
    QString path = typed.trimmed();
    if (path == "~" || path.startsWith("~/"))
        path = QDir::homePath() + path.mid(1);
#if defined(Q_OS_WIN)
    // and %NAME% for the folders that Windows names so
    static const QRegularExpression name("%([^%]+)%");
    QRegularExpressionMatch m;
    while ((m = name.match(path)).hasMatch()) {
        QString value = qEnvironmentVariable(m.captured(1).toUtf8());
        if (value.isEmpty())
            break;
        path.replace(m.capturedStart(), m.capturedLength(), value);
    }
    path = QDir::fromNativeSeparators(path);
#endif
    return path;
}

// 'path' as people write it: on Windows with backslashes, and the folders
// of the user or the programs as %NAME%, such as %LOCALAPPDATA%; elsewhere
// with ~ for the home folder
static QString shown_path(const QString &path)
{
#if defined(Q_OS_WIN)
    QString shown = QDir::toNativeSeparators(path);
    // the longest first, as LOCALAPPDATA is in USERPROFILE
    for (const char *name : { "LOCALAPPDATA", "APPDATA", "USERPROFILE",
                              "ProgramFiles" }) {
        QString value = QDir::toNativeSeparators(qEnvironmentVariable(name));
        if (!value.isEmpty()
            && shown.startsWith(value + "\\", Qt::CaseInsensitive)) {
            return "%" + QString(name) + "%" + shown.mid(value.size());
        }
    }
    return shown;
#else
    QString home = QDir::homePath();
    if (path.startsWith(home + "/"))
        return "~" + path.mid(home.size());
    return path;
#endif
}

Launcher::Launcher(QObject *parent)
    : QObject(parent), m_folder(default_folder())
{
    if (powwowChoosable())
        m_powwow = expand_path(launcher_settings().value("powwow").toString());
    if (m_powwow.isEmpty())
        m_powwow = find_powwow();
    check();
    QObject::connect(&m_watcher, &QFileSystemWatcher::directoryChanged,
                     this, &Launcher::refresh);
    refresh();
}

void Launcher::setPowwow(const QString &chosen)
{
    QString path = expand_path(chosen);
    if (path == m_powwow)
        return;
    m_powwow = path;
    QSettings settings = launcher_settings();
    if (path == find_powwow())
        settings.remove("powwow");
    else
        settings.setValue("powwow", path);
    check();
    emit powwowChanged();
    refresh();
}

bool Launcher::fail(const QString &message)
{
    m_error = message;
    emit errorChanged();
    return false;
}

QString Launcher::file(const QString &mud) const
{
    return m_folder + "/" + mud;
}

// the definition files: the files of the folder, except the user-wide
// settings file, which is there on macOS, the settings of the launcher,
// if POWWOWDIR is ~/.config/powwow, and hidden and backup files
void Launcher::refresh()
{
    if (QFileInfo(m_folder).isDir() && m_watcher.directories().isEmpty())
        m_watcher.addPath(m_folder);

    QStringList files;
    const QFileInfoList infos = QDir(m_folder).entryInfoList(
        QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &info : infos) {
        QString name = info.fileName();
        if (name != "powwow.conf" && name != "launcher.conf"
            && !name.endsWith('~'))
            files << name;
    }
    // in the order that the user dragged them into, and those added since
    // after them, by name
    QStringList muds;
    const QStringList order =
        launcher_settings().value("order").toStringList();
    for (const QString &mud : order) {
        if (files.contains(mud))
            muds << mud;
    }
    for (const QString &mud : std::as_const(files)) {
        if (!muds.contains(mud))
            muds << mud;
    }
    if (muds != m_muds) {
        m_muds = muds;
        emit mudsChanged();
    }

    QStringList hosts;
    for (const QString &mud : std::as_const(m_muds)) {
        QString host, error;
        if (m_usable)
            run({ "--get", "host", file(mud) }, &host, &error);
        hosts << host;
    }
    if (hosts != m_hosts) {
        m_hosts = hosts;
        emit hostsChanged();
    }
}

void Launcher::setOrder(const QStringList &muds)
{
    launcher_settings().setValue("order", muds);
    refresh();
}

// the MUD 'mud' after the MUD 'after' in the order, or in its place if
// 'mud' is that renamed
void Launcher::placeAfter(const QString &mud, const QString &after)
{
    QStringList order = m_muds;
    order.removeAll(mud);
    int i = order.indexOf(after);
    if (i < 0)
        return;
    order.insert(i + 1, mud);
    launcher_settings().setValue("order", order);
}

// powwow --config with 'args', its standard output in 'out', or what
// went wrong in 'error'
bool Launcher::run(const QStringList &args, QString *out, QString *error)
{
    if (m_powwow.isEmpty()) {
        *error = notFound();
        return false;
    }
    QProcess process;
    // in UTF-8, as its output is read, also where the launcher has no
    // locale, as when it is started from the Finder
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(env.contains("LC_ALL") ? "LC_ALL" : "LC_CTYPE", "C.UTF-8");
    process.setProcessEnvironment(env);
    // with --config, unless the arguments start with --no-config
    process.start(m_powwow, (args.value(0) == "--no-config"
                             ? args : QStringList{ "--config" } + args));
    if (!process.waitForStarted()) {
        *error = tr("Cannot run %1: %2").arg(shown_path(m_powwow),
                                             process.errorString());
        return false;
    }
    process.closeWriteChannel();
    if (!process.waitForFinished(10000)) {
        process.kill();
        *error = tr("%1 did not finish.").arg(shown_path(m_powwow));
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0) {
        *error = QString::fromUtf8(process.readAllStandardError())
            .trimmed();
        if (error->isEmpty())
            *error = tr("%1 failed.").arg(shown_path(m_powwow));
        return false;
    }
    if (out != nullptr) {
        *out = QString::fromUtf8(process.readAllStandardOutput());
        if (out->endsWith('\n'))
            out->chop(1);
    }
    return true;
}

bool Launcher::run(const QStringList &args, QString *out)
{
    QString error;
    return run(args, out, &error) || fail(error);
}

// whether powwow takes --get, which older ones do not
void Launcher::check()
{
    QString error;
    m_usable = run({ "--get", "save" }, nullptr, &error);
}

// the version, with the commit for a -dev version, as powwow shows it
QString Launcher::version()
{
    QString version = POWWOW_VERSION;
    QString commit = GIT_COMMIT;
    if (version.endsWith("-dev") && !commit.isEmpty())
        version += " (" + commit + ")";
    return version;
}

QString Launcher::powwowVersion()
{
    QString out, error;
    if (!run({ "--version" }, &out, &error))
        return QString();
    return out.section('\n', 0, 0);
}

QString Launcher::get(const QString &name, const QString &mud)
{
    if (!m_usable)
        return QString();
    QStringList args = { "--get", name };
    if (!mud.isEmpty())
        args << file(mud);
    QString value;
    if (!run(args, &value))
        return QString();
    return value;
}

QString Launcher::shortcutText(const QVariant &shortcut)
{
    QKeySequence keys;
    if (shortcut.typeId() == QMetaType::QString)
        keys = QKeySequence(shortcut.toString());
    else if (shortcut.canConvert<int>())
        keys = QKeySequence(QKeySequence::StandardKey(shortcut.toInt()));
    return keys.toString(QKeySequence::NativeText);
}

QVariantMap Launcher::settings(const QString &mud)
{
    QVariantMap map;
    if (!m_usable)
        return map;
    QStringList args = { "--list-settings" };
    if (!mud.isEmpty())
        args << file(mud);
    QString out;
    if (!run(args, &out))
        return map;
    for (const QString &line : out.split('\n')) {
        qsizetype eq = line.indexOf('=');
        if (eq > 0)
            map.insert(line.left(eq), line.mid(eq + 1));
    }
    return map;
}

bool Launcher::set(const QString &name, const QString &value,
                   const QString &mud)
{
    QStringList args = { "--set", name + "=" + value };
    if (!mud.isEmpty())
        args << file(mud);
    return run(args, nullptr);
}

// whether 'name' can be that of a definition file
bool Launcher::checkName(const QString &name)
{
    if (name.isEmpty() || name.startsWith('.') || name.contains('/')
        || name.contains('\\')) {
        return fail(tr("“%1” cannot be the name of a file.").arg(name));
    }
    return true;
}

// whether a MUD other than 'except' has the name 'name', with or without
// .powwow
static bool taken(const QStringList &muds, const QString &name,
                  const QString &except = QString())
{
    for (const QString &mud : muds) {
        if (mud != except && (mud == name || mud == name + SUFFIX))
            return true;
    }
    return false;
}

bool Launcher::add(const QString &name, const QString &host, bool identify)
{
    if (!checkName(name))
        return false;
    QString mud = name + SUFFIX;
    if (taken(m_muds, name) || QFileInfo::exists(file(mud)))
        return fail(tr("There is already a MUD called “%1”.").arg(name));
    if (!QDir().mkpath(m_folder))
        return fail(tr("Cannot create the folder %1.")
                    .arg(shown_path(m_folder)));
    bool ok = (set("host", host, mud)
               && (!identify || set("identify", "on", mud)));
    refresh();
    return ok;
}

// the file of the MUD 'mud' if it were called 'name', which keeps its
// .powwow, or its lack of one; empty, having said why, if no other MUD
// can be called that
QString Launcher::renamed(const QString &mud, const QString &name)
{
    if (!checkName(name))
        return QString();
    QString renamed = mud.endsWith(SUFFIX) ? name + SUFFIX : name;
    if (taken(m_muds, name, mud) || QFileInfo::exists(file(renamed))) {
        fail(tr("There is already a MUD called “%1”.").arg(name));
        return QString();
    }
    return renamed;
}

// whether no powwow has the MUD 'mud' open; if one has, say 'message'
// about it
bool Launcher::notInUse(const QString &mud, const QString &message)
{
    if (get("in-use", mud).isEmpty())
        return true;
    QString name = mud.endsWith(SUFFIX) ? mud.chopped(SUFFIX.size()) : mud;
    return fail(message.arg(name));
}

bool Launcher::editable(const QString &mud)
{
    return notInUse(mud, tr("“%1” is in use by powwow; quit it there before"
                            " editing it."));
}

bool Launcher::removable(const QString &mud)
{
    return notInUse(mud, tr("“%1” is in use by powwow; quit it there before"
                            " removing it."));
}

bool Launcher::edit(const QString &mud, const QString &name,
                    const QString &host, bool identify)
{
    if (!editable(mud))
        return false;
    QString to = mud;
    if (name != (mud.endsWith(SUFFIX) ? mud.chopped(SUFFIX.size()) : mud)) {
        to = renamed(mud, name);
        if (to.isEmpty())
            return false;
        if (!QFile::rename(file(mud), file(to)))
            return fail(tr("Cannot rename %1.").arg(shown_path(file(mud))));
        // in the place of the old name
        placeAfter(to, mud);
    }
    bool ok = (set("host", host, to)
               && set("identify", identify ? "on" : "off", to));
    refresh();
    return ok;
}

QString Launcher::duplicate(const QString &mud, const QString &name)
{
    QString to = renamed(mud, name);
    if (to.isEmpty())
        return QString();
    if (!QFile::copy(file(mud), file(to))) {
        fail(tr("Cannot copy %1.").arg(shown_path(file(mud))));
        return QString();
    }
    placeAfter(to, mud);
    refresh();
    return to;
}

// the name of a MUD for the file 'path': its own, without .powwow, or
// with a number after it if a MUD has that name
static QString import_name(const QString &path, const QStringList &muds,
                           const QString &folder)
{
    QString base = QFileInfo(path).fileName();
    if (base.endsWith(SUFFIX))
        base.chop(SUFFIX.size());
    QString name = base;
    for (int n = 2; taken(muds, name)
                    || QFileInfo::exists(folder + "/" + name + SUFFIX); ++n)
        name = QString("%1 %2").arg(base).arg(n);
    return name;
}

QString Launcher::importFile(const QUrl &url)
{
    QString path = url.toLocalFile();
    if (!QFileInfo(path).isFile()) {
        fail(tr("%1 is not a file.").arg(shown_path(path)));
        return QString();
    }
    if (!QDir().mkpath(m_folder)) {
        fail(tr("Cannot create the folder %1.").arg(shown_path(m_folder)));
        return QString();
    }
    // the commands of powwow, as every definition file starts with one
    QFile f(path);
    QByteArray first;
    if (f.open(QIODevice::ReadOnly)) {
        while (!f.atEnd() && (first = f.readLine().trimmed()).isEmpty())
            ;
    }
    if (!first.startsWith('#')) {
        fail(tr("%1 is not a definition file of powwow.")
             .arg(shown_path(path)));
        return QString();
    }
    QString mud = import_name(path, m_muds, m_folder) + SUFFIX;
    if (!QFile::copy(path, file(mud))) {
        fail(tr("Cannot copy %1.").arg(shown_path(path)));
        return QString();
    }
    // loaded and saved by powwow once, which converts an older file; with
    // --no-config, so that its own settings stay in it rather than going
    // to the user-wide settings file
    QString error, save;
    if (!m_usable
        || !run({ "--no-config", "--get", "save", file(mud) }, &save, &error)
        || !run({ "--no-config", "--set", "save=" + save, file(mud) },
                nullptr, &error)) {
        QFile::remove(file(mud));
        fail(m_usable ? tr("%1 is not a definition file of powwow: %2")
                            .arg(shown_path(path), error)
                      : notFound());
        return QString();
    }
    refresh();
    return mud;
}

QVariantMap Launcher::info(const QString &mud) const
{
    QString path = file(mud);
    QFileInfo fileInfo(path);
    QLocale locale;
    QVariantMap info = {
        { "path", shown_path(path) },
        { "size", locale.formattedDataSize(fileInfo.size()) },
        { "modified", locale.toString(fileInfo.lastModified(),
                                      QLocale::ShortFormat) },
    };

    // UTF-8, or ISO-8859-1 in a file older than that, as powwow reads it
    QFile f(path);
    QByteArray bytes;
    if (f.open(QIODevice::ReadOnly))
        bytes = f.readAll();
    QStringDecoder utf8(QStringDecoder::Utf8);
    QString text = utf8(bytes);
    if (utf8.hasError())
        text = QString::fromLatin1(bytes);
    QStringList lines = text.split('\n');
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    // the secrets of #secret, which are passwords, hidden
    for (QString &line : lines) {
        if (line.startsWith("#secret "))
            line = "#secret =…";
    }

    // as powwow saves them; the aliases and actions, which most MUDs have,
    // even when there are none, with what to say then; and the settings,
    // those of the connection and the session, each by its own command
    static const struct {
        const char *title;
        QStringList starts;
        const char *none;
    } kinds[] = {
        { QT_TR_NOOP("Aliases"), { "#alias " }, QT_TR_NOOP("No aliases") },
        { QT_TR_NOOP("Actions"), { "#action " }, QT_TR_NOOP("No actions") },
        { QT_TR_NOOP("Prompts"), { "#prompt " }, nullptr },
        { QT_TR_NOOP("Substitutions"), { "#substitute " }, nullptr },
        { QT_TR_NOOP("Marks"), { "#mark " }, nullptr },
        { QT_TR_NOOP("Variables"), { "#(" }, nullptr },
        { QT_TR_NOOP("Keys"), { "#bind " }, nullptr },
        { QT_TR_NOOP("Settings"),
          { "#host ", "#delim ", "#hilite ", "#groupdelim ", "#init ",
            "#setvar ", "#option ", "#secret " },
          nullptr },
    };
    QVariantList sections;
    for (const auto &kind : kinds) {
        QStringList found;
        for (const QString &line : std::as_const(lines)) {
            for (const QString &start : kind.starts) {
                if (line.startsWith(start)) {
                    found << line;
                    break;
                }
            }
        }
        if (!found.isEmpty() || kind.none != nullptr) {
            sections << QVariantMap{
                { "title", tr(kind.title) },
                { "count", found.size() },
                { "text", found.join('\n') },
                { "none", kind.none != nullptr ? tr(kind.none) : QString() },
            };
        }
    }
    sections << QVariantMap{ { "title", tr("File") },
                             { "count", lines.size() },
                             { "text", lines.join('\n') } };
    info["sections"] = sections;
    return info;
}

void Launcher::reveal(const QString &mud) const
{
    revealPath(file(mud));
}

void Launcher::revealPath(const QString &path) const
{
    QString native = QDir::toNativeSeparators(expand_path(path));
#if defined(Q_OS_WIN)
    // Explorer takes the file quoted after the comma, which QProcess
    // would not quote so
    QProcess explorer;
    explorer.setProgram("explorer.exe");
    explorer.setNativeArguments("/select,\"" + native + "\"");
    explorer.startDetached();
#elif defined(Q_OS_MACOS)
    QProcess::startDetached("open", { "-R", native });
#else
    // the folder: file managers have no common way to select a file
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QFileInfo(native).absolutePath()));
#endif
}

QString Launcher::configFile()
{
    QString path, error;
    if (!m_usable || !run({ "--get", "config-file" }, &path, &error))
        return QString();
    return shown_path(expand_path(path));
}

QString Launcher::manualText()
{
    QString path, error;
    if (!m_usable || !run({ "--get", "manual" }, &path, &error))
        return QString();
    QFile f(expand_path(path));
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(f.readAll());
}

QString Launcher::style() const
{
    return QQuickStyle::name();
}

bool Launcher::powwowChoosable() const
{
#if defined(Q_OS_WIN)
    return false;
#else
    return true;
#endif
}

QString Launcher::notFound() const
{
    return (powwowChoosable()
            ? tr("powwow was not found: choose it in the settings.")
            : tr("powwow was not found beside the launcher: install powwow"
                 " again."));
}

QUrl Launcher::powwowUrl() const
{
    if (m_powwow.isEmpty() || !QFileInfo::exists(m_powwow))
        return QUrl();
    return QUrl::fromLocalFile(m_powwow);
}

QUrl Launcher::powwowFolderUrl() const
{
    if (m_powwow.isEmpty() || !QFileInfo::exists(m_powwow))
        return QUrl();
    return QUrl::fromLocalFile(QFileInfo(m_powwow).absolutePath());
}

QString Launcher::folder() const
{
    return shown_path(m_folder);
}

QString Launcher::powwow() const
{
    return shown_path(m_powwow);
}

QString Launcher::markdownHtml(const QString &markdown, bool dark) const
{
    QTextDocument doc;
    doc.setMarkdown(markdown);
    // the shades of GitHub, which most people know code by
    QColor inlineBack = dark ? QColor(110, 118, 129, 102)
                             : QColor(175, 184, 193, 51);
    QColor blockBack = dark ? QColor(255, 255, 255, 18)
                            : QColor(0, 0, 0, 13);
    QStringList fixed = fixedFont().families();
    for (QTextBlock block = doc.begin(); block.isValid();
         block = block.next()) {
        QTextCursor cursor(block);
        QTextBlockFormat bf = block.blockFormat();
        // a code block, of ``` or indented, as Qt reads it
        if (bf.hasProperty(QTextFormat::BlockCodeFence)
            || bf.nonBreakableLines()) {
            bf.setBackground(blockBack);
            cursor.setBlockFormat(bf);
            QTextCharFormat cf;
            cf.setFontFamilies(fixed);
            cursor.select(QTextCursor::BlockUnderCursor);
            cursor.mergeCharFormat(cf);
            continue;
        }
        // inline code, which Qt marks as of a fixed pitch
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !fragment.charFormat().fontFixedPitch())
                continue;
            QTextCursor c(&doc);
            c.setPosition(fragment.position());
            c.setPosition(fragment.position() + fragment.length(),
                          QTextCursor::KeepAnchor);
            QTextCharFormat cf;
            cf.setBackground(inlineBack);
            cf.setFontFamilies(fixed);
            c.mergeCharFormat(cf);
        }
    }
    return doc.toHtml();
}

QFont Launcher::fixedFont() const
{
    return QFontDatabase::systemFont(QFontDatabase::FixedFont);
}

bool Launcher::remove(const QString &mud)
{
    if (!removable(mud))
        return false;
    QString path = file(mud);
    if (!QFile::moveToTrash(path) && !QFile::remove(path))
        return fail(tr("Cannot remove %1.").arg(shown_path(path)));
    refresh();
    return true;
}

bool Launcher::connect(const QString &mud)
{
    return connectFile(file(mud));
}

// the terminals that powwow can run in, as saved in the settings, and
// their names
namespace {
struct terminal_kind {
    const char *id;
    const char *name;
};
}

static const terminal_kind TERMINALS[] = {
#if defined(Q_OS_MACOS)
    // iTerm2 if installed, or else Terminal
    { "auto", QT_TRANSLATE_NOOP("Launcher", "Automatic") },
    { "terminal", "Terminal" },
    { "iterm", "iTerm2" },
    { "other", QT_TRANSLATE_NOOP("Launcher", "Other") },
#elif !defined(Q_OS_WIN)
    // $TERMINAL, or one that is installed
    { "auto", QT_TRANSLATE_NOOP("Launcher", "Automatic") },
    { "other", QT_TRANSLATE_NOOP("Launcher", "Other") },
#else
    { "wt", "Windows Terminal" },
#endif
};

QStringList Launcher::terminals() const
{
    QStringList names;
    for (const terminal_kind &kind : TERMINALS)
        names << tr(kind.name);
    return names;
}

int Launcher::terminal() const
{
    QString id = launcher_settings().value("terminal").toString();
    for (int i = 0; i < int(std::size(TERMINALS)); ++i) {
        if (id == TERMINALS[i].id)
            return i;
    }
    return 0;
}

QString Launcher::terminalCommand() const
{
    return launcher_settings().value("terminalCommand").toString();
}

void Launcher::setTerminal(int index, const QString &command)
{
    if (index < 0 || index >= int(std::size(TERMINALS)))
        return;
    QSettings settings = launcher_settings();
    if (index == 0)
        settings.remove("terminal");
    else
        settings.setValue("terminal", TERMINALS[index].id);
    if (command.trimmed().isEmpty())
        settings.remove("terminalCommand");
    else
        settings.setValue("terminalCommand", command.trimmed());
    emit terminalChanged();
}

#if !defined(Q_OS_WIN)
// 's' quoted for sh
static QString sh_quote(QString s)
{
    return "'" + s.replace("'", "'\\''") + "'";
}
#endif

#if defined(Q_OS_MACOS)
// whether iTerm2 is installed
static bool has_iterm()
{
    for (const QString &dir : { QString("/Applications"),
                                QDir::homePath() + "/Applications" }) {
        if (QFileInfo(dir + "/iTerm.app").isDir())
            return true;
    }
    return false;
}

// a script that removes itself and runs 'command', which Terminal and
// iTerm2 run in a new window when they open it; its path, or "" if it
// cannot be written; it clears the screen and the scrollback, where
// Terminal shows the login and the path of the script typed in its shell
static QString command_script(const QString &command)
{
    QTemporaryFile file(QDir::tempPath() + "/powwow-XXXXXX.command");
    file.setAutoRemove(false);
    if (!file.open())
        return QString();
    QByteArray script = "#!/bin/sh\nrm -f -- \"$0\"\n"
        "printf '\\033[H\\033[2J\\033[3J'\n"
        + command.toUtf8() + "\n";
    if (file.write(script) != script.size()
        || !file.setPermissions(QFileDevice::ReadOwner
                                | QFileDevice::WriteOwner
                                | QFileDevice::ExeOwner)) {
        file.remove();
        return QString();
    }
    return file.fileName();
}
#endif

bool Launcher::connectFile(const QString &path)
{
    QFileInfo info(path);
    QString dir = info.absolutePath();
    QString def = info.absoluteFilePath();
#if defined(Q_OS_WIN)
    // the profile of powwow in Windows Terminal, which the installer
    // writes, runs powwow-start, and that powwow --config
    QString wt = QStandardPaths::findExecutable("wt");
    if (wt.isEmpty()) {
        wt = qEnvironmentVariable("LOCALAPPDATA")
            + "/Microsoft/WindowsApps/wt.exe";
    }
    if (!QProcess::startDetached(wt, { "-p", "powwow",
                                       "--appendCommandLine",
                                       QDir::toNativeSeparators(def) },
                                 dir)) {
        return fail(tr("Cannot start Windows Terminal."));
    }
    return true;
#else
    if (m_powwow.isEmpty())
        return fail(notFound());
    QString command = "cd " + sh_quote(dir) + " && exec "
        + sh_quote(m_powwow) + " --config " + sh_quote(def);
    QString kind = TERMINALS[terminal()].id;

    // a command of the user, with %c for the command that runs powwow
    if (kind == "other") {
        QString other = terminalCommand();
        if (other.isEmpty())
            return fail(tr("Give the command of the terminal in the"
                           " settings."));
        QString run = "/bin/sh -c " + sh_quote(command);
        if (other.contains("%c"))
            other.replace("%c", run);
        else
            other += " " + run;
        if (!QProcess::startDetached("/bin/sh", { "-c", other }, dir))
            return fail(tr("Cannot run %1.").arg(other));
        return true;
    }
#  if defined(Q_OS_MACOS)
    bool iterm = kind == "iterm" || (kind == "auto" && has_iterm());
    QString app = iterm ? "com.googlecode.iterm2" : "com.apple.Terminal";
    QString name = iterm ? "iTerm2" : "Terminal";
    // opened rather than told what to run with AppleScript, for which
    // macOS would ask the user to let the launcher control the terminal
    QString script = command_script(command);
    if (script.isEmpty())
        return fail(tr("Cannot write the script that starts powwow in"
                       " %1.").arg(name));
    // in the background, and then the terminal in front with the window
    // alone, rather than all its windows; waited for, as it returns at
    // once, and the launcher may exit when this returns
    QProcess opener;
    opener.start("open", { "-g", "-b", app, script });
    if (!opener.waitForFinished()
        || opener.exitStatus() != QProcess::NormalExit
        || opener.exitCode() != 0) {
        QFile::remove(script);
        return fail(tr("Cannot start %1.").arg(name));
    }
    activate_application(app.toUtf8().constData());
    return true;
#  else
    // the terminal of the user, or one that is there
    QStringList terminals = { "x-terminal-emulator", "gnome-terminal",
                              "konsole", "xfce4-terminal", "xterm" };
    QString chosen = qEnvironmentVariable("TERMINAL");
    if (!chosen.isEmpty())
        terminals.prepend(chosen);
    for (const QString &terminal : terminals) {
        QString program = QStandardPaths::findExecutable(terminal);
        if (program.isEmpty())
            continue;
        QStringList args = { terminal == "gnome-terminal" ? "--" : "-e",
                             m_powwow, "--config", def };
        if (QProcess::startDetached(program, args, dir))
            return true;
    }
    return fail(tr("No terminal was found: set TERMINAL to one, or choose"
                   " one in the settings."));
#  endif
#endif
}

// the profile of powwow for Windows Terminal, in the fragment that the
// installer writes for the user; one installed for all users, in
// ProgramData, cannot be changed
static QString fragment_path()
{
    return QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA"))
        + "/Microsoft/Windows Terminal/Fragments/powwow/powwow.json";
}

// the profiles of the fragment, empty if it cannot be read
static QJsonObject read_fragment()
{
    QFile f(fragment_path());
    if (!f.open(QIODevice::ReadOnly))
        return QJsonObject();
    return QJsonDocument::fromJson(f.readAll()).object();
}

bool Launcher::hasScrollback() const
{
#if defined(Q_OS_WIN)
    QFileInfo info(fragment_path());
    return info.isFile() && info.isWritable();
#else
    return false;
#endif
}

int Launcher::scrollback() const
{
    const QJsonArray profiles = read_fragment().value("profiles").toArray();
    for (const QJsonValue &profile : profiles) {
        int lines = profile.toObject().value("historySize").toInt();
        if (lines > 0)
            return lines;
    }
    return 10000;               // what the installer gives the profile
}

// the lines of scrollback in the profile itself: Windows Terminal does not
// take a fragment of another folder that updates it, and drops the
// command of the profile instead
bool Launcher::setScrollback(int lines)
{
    QString path = fragment_path();
    QJsonObject fragment = read_fragment();
    QJsonArray profiles = fragment.value("profiles").toArray();
    if (profiles.isEmpty())
        return fail(tr("Cannot read %1.").arg(shown_path(path)));
    for (QJsonValueRef profile : profiles) {
        QJsonObject p = profile.toObject();
        if (p.value("name").toString() == "powwow") {
            p["historySize"] = lines;
            profile = p;
        }
    }
    fragment["profiles"] = profiles;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)
        || f.write(QJsonDocument(fragment).toJson()) < 0 || !f.commit()) {
        return fail(tr("Cannot write %1.").arg(shown_path(path)));
    }
    return true;
}
