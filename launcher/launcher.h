// what the interface of the launcher uses: the definition files of the
// MUDs, the settings read and written through powwow --get and --set, and
// the starting of powwow in a terminal

#ifndef LAUNCHER_H
#define LAUNCHER_H

#include <QFileSystemWatcher>
#include <QFont>
#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

class Launcher : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // the folder of the definition files, one per MUD
    Q_PROPERTY(QString folder READ folder CONSTANT)
    // the names of the MUDs, those of their files without ".powwow"
    Q_PROPERTY(QStringList muds READ muds NOTIFY mudsChanged)
    // their hosts, as #host takes them, empty if powwow is not usable
    Q_PROPERTY(QStringList hosts READ hosts NOTIFY hostsChanged)
    // the program powwow, found or chosen in the settings
    Q_PROPERTY(QString powwow READ powwow WRITE setPowwow
               NOTIFY powwowChanged)
    // whether the settings can choose powwow: not on Windows, where the
    // launcher is installed with it and always runs that one
    Q_PROPERTY(bool powwowChoosable READ powwowChoosable CONSTANT)
    // whether powwow is there and takes --get and --set
    Q_PROPERTY(bool usable READ usable NOTIFY powwowChanged)
    // the terminals that powwow can run in, by name, and the one chosen;
    // the last is "Other", which runs 'terminalCommand'
    Q_PROPERTY(QStringList terminals READ terminals CONSTANT)
    Q_PROPERTY(int terminal READ terminal NOTIFY terminalChanged)
    Q_PROPERTY(QString terminalCommand READ terminalCommand
               NOTIFY terminalChanged)
    // whether the lines of scrollback can be set: in Windows Terminal
    Q_PROPERTY(bool hasScrollback READ hasScrollback CONSTANT)
    // the version of the launcher, that of powwow, and that of Qt
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT)
    // the style of the controls, such as FluentWinUI3 or macOS
    Q_PROPERTY(QString style READ style CONSTANT)
    // the fixed-width font of the system, for the lines of files
    Q_PROPERTY(QFont fixedFont READ fixedFont CONSTANT)
    // what went wrong in the last call that failed
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit Launcher(QObject *parent = nullptr);

    // as people write paths, such as %LOCALAPPDATA%\... or ~/...
    QString folder() const;
    QStringList muds() const { return m_muds; }
    QStringList hosts() const { return m_hosts; }
    QString powwow() const;
    void setPowwow(const QString &chosen);
    // the powwow chosen, as a file dialog takes it: empty if it is not
    // there
    Q_INVOKABLE QUrl powwowUrl() const;
    Q_INVOKABLE QUrl powwowFolderUrl() const;
    // the powwow chosen in a file dialog
    Q_INVOKABLE void choosePowwow(const QUrl &url)
    {
        setPowwow(url.toLocalFile());
    }
    bool usable() const { return m_usable; }
    bool powwowChoosable() const;
    // what to say when powwow is not there
    Q_INVOKABLE QString notFound() const;
    QStringList terminals() const;
    int terminal() const;
    QString terminalCommand() const;
    Q_INVOKABLE void setTerminal(int index, const QString &command);
    bool hasScrollback() const;
    QString error() const { return m_error; }
    static QString version();
    QFont fixedFont() const;
    QString style() const;
    QString qtVersion() const { return qVersion(); }
    // what powwow --version prints first, empty if it fails
    Q_INVOKABLE QString powwowVersion();

    // the setting 'name' of the MUD 'mud', or the user-wide one if 'mud' is
    // empty; a null string if powwow fails or is not usable
    Q_INVOKABLE QString get(const QString &name,
                            const QString &mud = QString());
    Q_INVOKABLE bool set(const QString &name, const QString &value,
                         const QString &mud = QString());
    // the shortcut 'shortcut' of an Action, a key sequence or a
    // StandardKey, as the system shows it, such as ⌘E on macOS
    Q_INVOKABLE static QString shortcutText(const QVariant &shortcut);
    // all the settings of the MUD 'mud', as --list-settings prints them, or
    // the defaults if 'mud' is empty; empty if powwow fails
    Q_INVOKABLE QVariantMap settings(const QString &mud);

    // a new MUD 'name', with 'host' as #host takes it, which sends
    // #identify on connecting, with #option +identify, if 'identify', for
    // MUME
    Q_INVOKABLE bool add(const QString &name, const QString &host,
                         bool identify);
    // the MUD 'mud' renamed 'name', with 'host' as #host takes it
    // whether the MUD 'mud' can be edited: not if a powwow has it open,
    // which would write over the changes, having said so
    Q_INVOKABLE bool editable(const QString &mud);
    // and removed, as powwow would write it again
    Q_INVOKABLE bool removable(const QString &mud);
    // the MUD 'mud' renamed 'name', with 'host' as #host takes it, and
    // #option identify on if 'identify'
    Q_INVOKABLE bool edit(const QString &mud, const QString &name,
                          const QString &host, bool identify);
    // a copy of the MUD 'mud' called 'name': its file, or empty if it
    // cannot be made
    Q_INVOKABLE QString duplicate(const QString &mud, const QString &name);
    // a copy of the definition file at 'url' as a new MUD, named after
    // it, and converted if older: the file of the MUD, empty if it cannot
    // be imported
    Q_INVOKABLE QString importFile(const QUrl &url);
    Q_INVOKABLE bool remove(const QString &mud);
    // the definition file of 'mud': "path", "size" and "modified", as
    // people read them, and "sections", a list of the lines of each kind
    // of definition, as "title", "count", "text", and "none", what to say
    // if there are none
    Q_INVOKABLE QVariantMap info(const QString &mud) const;
    // the file manager with the definition file of 'mud' selected
    Q_INVOKABLE void reveal(const QString &mud) const;
    // and with the file 'path' selected
    Q_INVOKABLE void revealPath(const QString &path) const;
    // the path of the user-wide settings file, as people write paths, empty
    // if powwow does not say
    Q_INVOKABLE QString configFile();
    // the text of the manual, in Markdown, empty if it cannot be read
    Q_INVOKABLE QString manualText();
    // 'markdown' as HTML, with code in the fixed-width font on a shade of
    // its own, darker or lighter for 'dark' mode
    Q_INVOKABLE QString markdownHtml(const QString &markdown,
                                     bool dark) const;
    Q_INVOKABLE bool connect(const QString &mud);
    // powwow in a terminal with the definition file 'path'
    bool connectFile(const QString &path);

    // the MUDs in the order 'muds', which the user dragged them into
    Q_INVOKABLE void setOrder(const QStringList &muds);

    // read the MUDs and their hosts again, such as after powwow saved
    Q_INVOKABLE void refresh();

    Q_INVOKABLE int scrollback() const;
    Q_INVOKABLE bool setScrollback(int lines);

signals:
    void mudsChanged();
    void hostsChanged();
    void powwowChanged();
    void terminalChanged();
    void errorChanged();

private:
    QString file(const QString &mud) const;
    bool notInUse(const QString &mud, const QString &message);
    bool checkName(const QString &name);
    QString renamed(const QString &mud, const QString &name);
    void placeAfter(const QString &mud, const QString &after);
    bool run(const QStringList &args, QString *out, QString *error);
    bool run(const QStringList &args, QString *out);
    void check();
    bool fail(const QString &message);

    QString m_folder;
    QStringList m_muds;
    QStringList m_hosts;
    QString m_powwow;
    bool m_usable = false;
    QString m_error;
    QFileSystemWatcher m_watcher;
};

#endif // LAUNCHER_H
