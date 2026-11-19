import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Powwow

// the settings of all MUDs, in the user-wide settings file of powwow
Page {
    id: page

    // the command options of powwow, by the fields that edit them, and for
    // a pager the checkbox that disables it, as "-"
    readonly property var commands: [
        { field: editor, name: "editor" },
        { field: pager, name: "pager", off: pagerOff },
        { field: helpPager, name: "help-pager", off: helpPagerOff },
        { field: shell, name: "shell" },
    ]
    property bool loaded: false
    // the values saved, by the names of the command options, so that a
    // field is saved only when it changed
    property var saved: ({})
    // #setvar keepalive as saved, "0" for off
    property string savedKeepalive: "0"
    // whether powwow runs with --windows, which has defaults of its own and
    // writes environment variables as %NAME%
    readonly property bool windows: Qt.platform.os === "windows"
    // the user-wide attributes of #hilite, by the editors that edit them
    readonly property var hilites: [
        { editor: hiliteInput, name: "hilite-input" },
        { editor: hiliteMessages, name: "hilite-messages" },
    ]

    function load() {
        configFile.text = Launcher.configFile()
        const values = {}
        for (const c of commands) {
            values[c.name] = Launcher.get(c.name) ?? ""
            const off = c.off !== undefined && values[c.name] === "-"
            if (c.off !== undefined)
                c.off.checked = off
            c.field.text = off ? "" : values[c.name]
        }
        saved = values
        for (const h of hilites)
            h.editor.value = Launcher.get(h.name) ?? ""
        save.checked = Launcher.get("save") !== "off"
        savedKeepalive = Launcher.get("keepalive") || "0"
        keepalive.text = savedKeepalive === "0" ? "" : savedKeepalive
        terminal.currentIndex = Launcher.terminal
        terminalCommand.text = Launcher.terminalCommand
        if (Launcher.hasScrollback)
            scrollback.text = Launcher.scrollback()
        loaded = true
    }

    // the value of the command option of 'c', of commands: "-" for a pager
    // disabled
    // the environment variables 'names' and then the default 'last', as
    // powwow tries them, for the placeholder of a command field
    function lookup(names, last) {
        return names.map(n => windows ? "%" + n + "%" : "$" + n)
            .concat(last).join(", ")
    }

    function commandText(c) {
        return c.off !== undefined && c.off.checked ? "-" : c.field.text
    }

    // each setting is saved when it changes, without an Apply button: a
    // field when editing it ends, with Enter or as it loses the focus
    function saveCommand(name, text) {
        const value = text.trim()
        if (!loaded || value === saved[name])
            return
        if (Launcher.set(name, value)) {
            saved[name] = value
            Launcher.refresh()
        }
    }

    // the attribute of #hilite 'name', of 'editor', as changed there, or
    // as saved if powwow cannot save it
    function saveHilite(editor, name, value) {
        if (!loaded)
            return
        if (Launcher.set(name, value))
            editor.value = value
        else
            editor.value = Launcher.get(name) ?? ""
    }

    function saveTerminal() {
        if (loaded)
            Launcher.setTerminal(terminal.currentIndex, terminalCommand.text)
    }

    // #setvar keepalive, empty or 0 for off
    function saveKeepalive() {
        const text = keepalive.text.trim()
        if (!loaded || (text !== "" && !keepalive.acceptableInput))
            return
        const value = text === "" ? "0" : String(parseInt(text, 10))
        if (value !== savedKeepalive && Launcher.set("keepalive", value))
            savedKeepalive = value
        // 0 is never, which the empty field shows
        if (value === "0")
            keepalive.text = ""
    }

    function saveScrollback() {
        if (loaded && Launcher.hasScrollback && scrollback.acceptableInput
            && parseInt(scrollback.text) !== Launcher.scrollback())
            Launcher.setScrollback(parseInt(scrollback.text))
    }

    // also what is being typed in a field when the page is left or the
    // window closed, which need not take the focus from it first
    function saveAll() {
        for (const c of commands)
            saveCommand(c.name, commandText(c))
        if (loaded && terminalCommand.text !== Launcher.terminalCommand)
            saveTerminal()
        saveScrollback()
        saveKeepalive()
    }

    // the settings as powwow and the launcher have them without any: the
    // commands of the environment or of powwow, the definition file saved
    // on exit, the first terminal, and the scrollback that the installer
    // gives the profile of Windows Terminal
    function restoreDefaults() {
        for (const c of commands) {
            if (!Launcher.set(c.name, ""))
                return
        }
        if (!Launcher.set("save", "on") || !Launcher.set("keepalive", "0"))
            return
        for (const h of hilites) {
            if (!Launcher.set(h.name, "default"))
                return
        }
        Launcher.setTerminal(0, "")
        if (Launcher.hasScrollback)
            Launcher.setScrollback(10000)
        Launcher.refresh()
        load()
    }

    // the control with the keyboard focus scrolled into view, as Tab
    // moves it: a scroll view does not follow it itself
    function showFocus() {
        const item = Window.activeFocusItem
        const flick = scroll.contentItem as Flickable
        if (!visible || item === null || flick === null)
            return
        let inside = item
        while (inside !== null && inside !== content)
            inside = inside.parent
        if (inside === null)
            return
        const margin = 12
        const top = item.mapToItem(flick.contentItem, 0, 0).y - margin
        const bottom = top + item.height + 2 * margin
        if (top < flick.contentY)
            flick.contentY = Math.max(0, top)
        else if (bottom > flick.contentY + flick.height)
            flick.contentY = Math.min(flick.contentHeight - flick.height,
                                      bottom - flick.height)
    }

    Connections {
        target: page.Window.window

        function onActiveFocusItemChanged() {
            page.showFocus()
        }

        function onClosing() {
            page.saveAll()
        }
    }

    // read the settings when the page is first shown, and save what is
    // being typed when it is left
    onVisibleChanged: {
        if (visible && !loaded)
            load()
        else if (!visible)
            saveAll()
    }

    FileDialog {
        id: picker

        title: qsTr("Choose powwow")
        onAccepted: {
            Launcher.choosePowwow(selectedFile)
            page.load()
        }
    }

    ScrollView {
        id: scroll

        anchors.fill: parent
        contentWidth: availableWidth
        // stop at the ends, without bouncing back, as with a mouse wheel
        Component.onCompleted: {
            (contentItem as Flickable).boundsBehavior = Flickable.StopAtBounds
        }

        // as wide as the page, not as the room beside the scroll bar,
        // which macOS shows only while scrolling with a mouse: the text
        // would wrap anew as it comes and goes, and the page jump; the
        // scroll bar is in the margin
        ColumnLayout {
            id: content

            x: 24
            width: scroll.width - 48
            spacing: 12

            Item {
                implicitHeight: 12
            }
            Label {
                text: qsTr("Settings")
                font.pixelSize: 28
            }

            // the user-wide settings file, which they are saved in
            RowLayout {
                Layout.fillWidth: true
                visible: configFile.text !== ""
                spacing: 12

                Label {
                    text: qsTr("Saved in")
                    opacity: 0.65
                }
                SelectableText {
                    id: configFile

                    Layout.fillWidth: true
                    wrapMode: TextEdit.WrapAnywhere
                    opacity: 0.65
                }
                Button {
                    text: (Qt.platform.os === "osx"
                           ? qsTr("Show in Finder")
                           : Qt.platform.os === "windows"
                           ? qsTr("Show in Explorer")
                           : qsTr("Show the folder"))
                    onClicked: Launcher.revealPath(configFile.text)

                    FocusRing {}
                }
            }

            Label {
                text: qsTr("Commands")
                font.pixelSize: 18
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Left empty, powwow takes the command from the"
                           + " environment, or its default.\nA"
                           + " \u201cDisabled\u201d pager prints the text in"
                           + " the terminal.")
                wrapMode: Text.Wrap
                opacity: 0.65
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                rowSpacing: 8

                Label {
                    text: qsTr("Editor")
                }
                EntryField {
                    id: editor

                    Layout.fillWidth: true
                    onEditingFinished: page.saveCommand("editor", text)
                    placeholderText: page.lookup(
                                         ["POWWOWEDITOR", "EDITOR"],
                                         page.windows ? "Notepad" : "vi")
                }

                Label {
                    text: qsTr("Pager")
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    EntryField {
                        id: pager

                        Layout.fillWidth: true
                        enabled: !pagerOff.checked
                        onEditingFinished: page.saveCommand("pager", text)
                        placeholderText: page.lookup(
                                             ["POWWOWPAGER", "PAGER"],
                                             page.windows ? "Notepad"
                                                          : "more")
                    }
                    CheckBox {
                        id: pagerOff

                        text: qsTr("Disable")
                        onToggled: page.saveCommand("pager",
                                                    page.commandText(
                                                        page.commands[1]))

                        FocusRing {}
                    }
                }

                Label {
                    text: qsTr("Pager of #help")
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    EntryField {
                        id: helpPager

                        Layout.fillWidth: true
                        enabled: !helpPagerOff.checked
                        onEditingFinished: page.saveCommand("help-pager",
                                                            text)
                        placeholderText: page.lookup(
                                             ["POWWOWHELPPAGER"],
                                             qsTr("the pager"))
                    }
                    CheckBox {
                        id: helpPagerOff

                        text: qsTr("Disable")
                        onToggled: page.saveCommand("help-pager",
                                                    page.commandText(
                                                        page.commands[2]))

                        FocusRing {}
                    }
                }

                Label {
                    text: qsTr("Shell")
                }
                EntryField {
                    id: shell

                    Layout.fillWidth: true
                    onEditingFinished: page.saveCommand("shell", text)
                    placeholderText: page.lookup(
                                         ["POWWOWSHELL"],
                                         page.windows ? "cmd" : "sh")
                }
            }

            Label {
                text: qsTr("Highlighting")
                font.pixelSize: 18
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("For all MUDs, unless a MUD has its own #hilite."
                           + " Colors that the terminal lacks show as the"
                           + " nearest it has.")
                wrapMode: Text.Wrap
                opacity: 0.65
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                rowSpacing: 16

                Label {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 6
                    text: qsTr("Typed text")
                }
                HiliteEditor {
                    id: hiliteInput

                    Layout.fillWidth: true
                    sample: qsTr("get the sword")
                    onEdited: v => page.saveHilite(hiliteInput,
                                                   "hilite-input", v)
                }

                Label {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 6
                    text: qsTr("Messages")
                }
                HiliteEditor {
                    id: hiliteMessages

                    Layout.fillWidth: true
                    sample: qsTr("#connected to the MUD")
                    onEdited: v => page.saveHilite(hiliteMessages,
                                                   "hilite-messages", v)
                }
            }

            Label {
                visible: Launcher.terminals.length > 1
                text: qsTr("Terminal")
                font.pixelSize: 18
            }
            ComboBox {
                id: terminal

                // in the window: the macOS style shows it as a window of
                // its own, which clicking the item chosen does not close
                popup.popupType: Popup.Item

                visible: Launcher.terminals.length > 1
                model: Launcher.terminals
                // as wide as the longest name, not as the one chosen
                implicitContentWidthPolicy: ComboBox.WidestText
                onActivated: page.saveTerminal()

                FocusRing {}
            }
            EntryField {
                id: terminalCommand

                Layout.fillWidth: true
                onEditingFinished: page.saveTerminal()
                visible: (Launcher.terminals.length > 1
                          && terminal.currentIndex === terminal.count - 1)
                placeholderText: (Qt.platform.os === "osx"
                                  ? "open -na Ghostty --args -e %c"
                                  : "kitty %c")
            }
            Label {
                Layout.fillWidth: true
                visible: terminalCommand.visible
                text: qsTr("%c is the command that runs powwow; it is added"
                           + " at the end if missing.")
                wrapMode: Text.Wrap
                opacity: 0.65
            }

            Label {
                text: qsTr("Sessions")
                font.pixelSize: 18
            }
            Switch {
                id: save

                text: qsTr("Save the definition file on exit")
                onToggled: {
                    if (page.loaded)
                        Launcher.set("save", checked ? "on" : "off")
                }

                FocusRing {}
            }
            RowLayout {
                spacing: 12

                Label {
                    text: qsTr("Send a keepalive after")
                }
                EntryField {
                    id: keepalive

                    Layout.preferredWidth: 100
                    placeholderText: qsTr("never")
                    onEditingFinished: page.saveKeepalive()
                    validator: IntValidator {
                        bottom: 0
                        top: 86400
                    }
                }
                Label {
                    text: qsTr("idle seconds")
                }
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("A telnet no-op, sent when nothing was sent to the"
                           + " MUD for that long, keeps routers that forget"
                           + " idle connections, often after a few minutes,"
                           + " from cutting it. Advanced can give a MUD its"
                           + " own.")
                wrapMode: Text.Wrap
                opacity: 0.65
            }
            RowLayout {
                visible: Launcher.hasScrollback
                spacing: 12

                Label {
                    text: qsTr("Lines of scrollback")
                }
                // a text field, saved as the others, rather than a SpinBox,
                // whose arrows would save each step
                EntryField {
                    id: scrollback

                    Layout.preferredWidth: 100
                    onEditingFinished: page.saveScrollback()
                    validator: IntValidator {
                        bottom: 1
                        top: 32767
                    }
                }
            }

            Label {
                visible: Launcher.powwowChoosable
                text: qsTr("Program")
                font.pixelSize: 18
            }
            RowLayout {
                Layout.fillWidth: true
                visible: Launcher.powwowChoosable
                spacing: 12

                Label {
                    text: qsTr("powwow")
                }
                EntryField {
                    id: program

                    Layout.fillWidth: true
                    text: Launcher.powwow
                    placeholderText: qsTr("not found")
                    onEditingFinished: {
                        if (text !== Launcher.powwow) {
                            Launcher.powwow = text
                            page.load()
                        }
                    }
                }
                Button {
                    text: qsTr("Choose…")
                    // in the folder of the powwow chosen, with it selected
                    onClicked: {
                        const current = Launcher.powwowUrl()
                        if (current.toString() !== "") {
                            picker.currentFolder = Launcher.powwowFolderUrl()
                            picker.selectedFile = current
                        }
                        picker.open()
                    }

                    FocusRing {}
                }
            }

            Button {
                Layout.topMargin: 12
                text: qsTr("Restore Defaults…")
                onClicked: restoreDialog.open()

                FocusRing {}
            }
            Item {
                implicitHeight: 12
            }
        }
    }

    PowwowDialog {
        id: restoreDialog

        // a width of its own, as one from the buttons makes a binding loop
        width: Math.min(420, page.width - 48)
        title: qsTr("Restore the default settings?")
        standardButtons: Dialog.Yes | Dialog.No

        // selectable, to copy it, and as a list, whose items wrap under
        // their text, not under the bullets
        SelectableText {
            // from the page, not from the dialog, whose height would then
            // depend on itself through the lines of the text
            width: Math.min(420, page.width - 48) - restoreDialog.leftPadding
                   - restoreDialog.rightPadding
            textFormat: TextEdit.RichText
            text: {
                const items = [
                    qsTr("clear the editor, the pagers and the shell, so that"
                         + " powwow chooses them itself"),
                    qsTr("save the definition file on exit again"),
                    qsTr("send no keepalives, except to the MUDs that have"
                         + " their own"),
                    qsTr("highlight typed text in bold and messages in"
                         + " italic bright blue again, except where a MUD"
                         + " has its own"),
                    qsTr("set the terminal back to \u201c%1\u201d")
                        .arg(Launcher.terminals[0] ?? ""),
                ]
                if (Launcher.hasScrollback)
                    items.push(qsTr("set the scrollback back to 10000 lines"))
                const html = t => t.replace(/&/g, "&amp;")
                    .replace(/</g, "&lt;").replace(/>/g, "&gt;")
                return html(qsTr("This will:"))
                    // less than the indent of Qt, about 40 pixels
                    + "<ul style=\"margin-top: 4px; margin-left: -24px\">"
                    + items.map(t => "<li>" + html(t) + "</li>").join("")
                    + "</ul>"
            }
        }

        onAccepted: page.restoreDefaults()
    }
}
