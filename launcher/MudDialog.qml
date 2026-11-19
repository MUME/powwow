pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Powwow

// a MUD to add, or the MUD 'mud' to edit: its name and what #host sets in
// its definition file
PowwowDialog {
    id: dialog

    // the definition file of the MUD edited, empty to add one
    property string mud
    // the TLS options of #host other than no-verify, such as host=NAME,
    // kept as they were
    property var tlsOptions: []

    // the options of the MUD that "Advanced" sets, with what they do;
    // identify is "MUME handshake", and save is user-wide, in Settings
    readonly property var optionGroups: [
        { title: qsTr("Typing"), options: [
            { name: "autoclear",
              text: qsTr("Clear the input line before running commands") },
            { name: "speedwalk",
              text: qsTr("Walk with speedwalk, such as \u201c3en\u201d") },
            { name: "reprint",
              text: qsTr("When typing ahead, show each command again at the"
                         + " prompt it runs at") },
        ] },
        { title: qsTr("What powwow shows"), options: [
            { name: "echo",
              text: qsTr("Print the commands that actions run") },
            { name: "keyecho",
              text: qsTr("Print the command of a key pressed") },
            { name: "info", text: qsTr("Print what commands have done") },
            { name: "autoprint",
              text: qsTr("Print the lines that actions match") },
            { name: "compact",
              text: qsTr("Remove the prompt when the MUD sends a message") },
            { name: "wrap",
              text: qsTr("Wrap the lines at the edge of words") },
            { name: "debug",
              text: qsTr("Show every command as it runs, to debug aliases"
                         + " and actions") },
        ] },
        { title: qsTr("Connection"), keepalive: true, options: [
            { name: "sendsize",
              text: qsTr("Send the window size on connecting") },
            { name: "exit",
              text: qsTr("Quit powwow when the connection closes") },
        ] },
        { title: qsTr("Saved with the MUD"), options: [
            { name: "history", text: qsTr("Save the command history") },
            { name: "words",
              text: qsTr("Save the command word history, which Tab"
                         + " completes") },
        ] },
    ]
    readonly property var optionList: optionGroups.reduce(
        (all, g) => all.concat(g.options), [])
    // the checkboxes of "Advanced", by the names of their options
    property var optionBoxes: ({})
    // those options as the MUD has them, or as powwow sets them for a MUD
    // added, and as chosen in "Advanced", by name, on or off
    property var mudOptions: ({})
    property var chosen: ({})
    // #hilite input and messages: the MUD's own, as --get prints them,
    // "default" for none, and the user-wide ones of Settings, "" for none
    readonly property var hilites: [
        { name: "mud-hilite-input", user: "hilite-input",
          text: qsTr("Typed text"), sample: qsTr("get the sword") },
        { name: "mud-hilite-messages", user: "hilite-messages",
          text: qsTr("Messages"), sample: qsTr("#connected to the MUD") },
    ]
    property var mudHilites: ({})
    // #setvar keepalive: the MUD's own, as --get prints it, "default" for
    // none, and the user-wide one of Settings, "0" for none; and the
    // checkbox and field of "Advanced" that choose it
    property string mudKeepalive: "default"
    property string userKeepalive: "0"
    property var keepaliveBox: null
    property var keepaliveField: null
    property var chosenHilites: ({})
    property var userHilites: ({})

    // whether what is filled in can be saved
    readonly property bool valid: (name.text.trim() !== ""
                                   && /^[^ ]+$/.test(address.text.trim())
                                   && port.acceptableInput)

    // the definition file of the MUD added or edited
    signal done(string file)

    // the MUD's own #hilite 'name' chosen as 'value'
    function chooseHilite(name, value) {
        const values = Object.assign({}, chosenHilites)
        values[name] = value
        chosenHilites = values
    }

    // the MUD's own keepalive as chosen in "Advanced", or "default"; as it
    // was if the field has no number
    function chosenKeepalive() {
        if (keepaliveBox === null || keepaliveField === null
            || keepaliveBox.checked)
            return "default"
        const text = keepaliveField.text.trim()
        if (text === "")
            return "0"
        return keepaliveField.acceptableInput ? String(parseInt(text, 10))
                                              : mudKeepalive
    }

    // the keepalive in "Advanced" as the MUD has it
    function showKeepalive() {
        if (keepaliveBox === null || keepaliveField === null)
            return
        const own = mudKeepalive !== "default"
        keepaliveBox.checked = !own
        const value = own ? mudKeepalive : userKeepalive
        keepaliveField.text = value === "0" ? "" : value
    }

    // Return, in a text field, as the button Add or Save
    function acceptIfValid() {
        if (valid)
            accept()
    }

    // the TLS word of #host, such as --tls=no-verify
    function tlsWord() {
        if (!tls.checked)
            return "--no-tls"
        const options = (verify.checked ? [] : ["no-verify"])
            .concat(tlsOptions)
        return options.length > 0 ? "--tls=" + options.join(":") : "--tls"
    }

    // the host for #host
    function host() {
        return [address.text.trim(), port.text, tlsWord(),
                charset.currentIndex === 1 ? "--latin1" : "--utf8"].join(" ")
    }

    title: mud === "" ? qsTr("Add MUD") : qsTr("Edit MUD")

    onOpened: {
        const words = mud === "" ? [] : Launcher.get("host", mud).split(" ")
        name.text = mud.replace(/\.powwow$/, "")
        address.text = words.length > 0 ? words[0] : ""
        port.text = words.length > 1 ? words[1] : "23"
        const word = words.find(w => w.startsWith("--tls=")) ?? ""
        const options = word.substring("--tls=".length).split(":")
            .filter(o => o !== "")
        tlsOptions = options.filter(o => o !== "no-verify")
        verify.checked = !options.includes("no-verify")
        tls.checked = !words.includes("--no-tls")
        charset.currentIndex = words.includes("--latin1") ? 1 : 0
        identify.checked = mud !== "" && Launcher.get("identify", mud) === "on"
        const settings = Launcher.settings(mud)
        const values = {}
        for (const o of optionList)
            values[o.name] = settings[o.name] === "on"
        mudOptions = values
        chosen = Object.assign({}, values)
        const own = {}
        const user = {}
        for (const h of hilites) {
            own[h.name] = settings[h.name] ?? "default"
            user[h.name] = settings[h.user] ?? ""
        }
        mudHilites = own
        chosenHilites = Object.assign({}, own)
        userHilites = user
        mudKeepalive = settings["mud-keepalive"] ?? "default"
        userKeepalive = settings["keepalive"] || "0"
        showKeepalive()
        name.forceActiveFocus()
        name.selectAll()
    }

    onAccepted: {
        const n = name.text.trim()
        const ok = (mud === ""
                    ? Launcher.add(n, host(), identify.checked)
                    : Launcher.edit(mud, n, host(), identify.checked))
        if (!ok)
            return
        const file = mud === "" || mud.endsWith(".powwow") ? n + ".powwow" : n
        // only the options changed in "Advanced", each a run of powwow
        for (const o of optionList) {
            if (chosen[o.name] !== mudOptions[o.name]
                && !Launcher.set(o.name, chosen[o.name] ? "on" : "off", file))
                break
        }
        for (const h of hilites) {
            if (chosenHilites[h.name] !== mudHilites[h.name]
                && !Launcher.set(h.name, chosenHilites[h.name], file))
                break
        }
        const keepalive = chosenKeepalive()
        if (keepalive !== mudKeepalive)
            Launcher.set("mud-keepalive", keepalive, file)
        done(file)
    }

    GridLayout {
        anchors.fill: parent
        columns: 2
        columnSpacing: 12
        rowSpacing: 8

        Label {
            text: qsTr("Name")
        }
        EntryField {
            id: name

            Layout.fillWidth: true
            Layout.minimumWidth: 240
            onAccepted: dialog.acceptIfValid()
        }

        Label {
            text: qsTr("Address")
        }
        EntryField {
            id: address

            Layout.fillWidth: true
            placeholderText: "mud.example.org"
            onAccepted: dialog.acceptIfValid()
        }

        Label {
            text: qsTr("Port")
        }
        // a text field, as a SpinBox takes what is typed only when it
        // loses the focus, which clicking a button does not take
        EntryField {
            id: port

            Layout.preferredWidth: 100
            validator: IntValidator {
                bottom: 1
                top: 65535
            }
            onAccepted: dialog.acceptIfValid()
        }

        Label {
            text: qsTr("Character encoding")
        }
        ComboBox {
            id: charset

            // in the window: the macOS style shows it as a window of its own,
            // which clicking the item chosen does not close
            popup.popupType: Popup.Item

            Layout.fillWidth: true
            model: ["UTF-8", "ISO-8859-1"]

            FocusRing {}
        }

        Item {
            implicitWidth: 1
        }
        CheckBox {
            id: tls

            text: qsTr("Encrypt connection (TLS)")

            FocusRing {}
        }

        Item {
            implicitWidth: 1
        }
        CheckBox {
            id: verify

            enabled: tls.checked
            text: qsTr("Verify server certificate")

            FocusRing {}
        }

        Item {
            implicitWidth: 1
        }
        CheckBox {
            id: identify

            text: qsTr("MUME handshake")

            PowwowToolTip {
                visible: identify.hovered
                text: qsTr("Tell MUME on connecting that powwow can edit"
                           + " texts locally")
            }
            FocusRing {}
        }

        Item {
            implicitWidth: 1
        }
        RowLayout {
            spacing: 8

            Button {
                text: qsTr("Advanced…")
                onClicked: advanced.open()

                FocusRing {}
            }
            Button {
                text: qsTr("Highlighting…")
                onClicked: highlighting.open()

                FocusRing {}
            }
        }
    }

    // the MUD's own #hilite input and messages, or those of Settings
    PowwowDialog {
        id: highlighting

        title: qsTr("Highlighting")
        standardButtons: Dialog.Ok

        // scrolled where the window is too low for both, as the dialog
        // is made as low as the window then, and its buttons would cover
        // the rest
        ScrollView {
            anchors.fill: parent
            implicitWidth: hiliteGrid.implicitWidth
            implicitHeight: hiliteGrid.implicitHeight
            contentWidth: hiliteGrid.implicitWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            clip: true

            GridLayout {
                id: hiliteGrid

                columns: 2
                columnSpacing: 12
                rowSpacing: 8

                Repeater {
                    model: dialog.hilites

                    delegate: Label {
                        id: kindLabel

                        required property var modelData
                        required property int index

                        Layout.row: index * 2
                        Layout.column: 0
                        Layout.topMargin: index > 0 ? 12 : 0
                        text: modelData.text
                        font.bold: true
                    }
                }
                Repeater {
                    model: dialog.hilites

                    delegate: CheckBox {
                        id: same

                        required property var modelData
                        required property int index

                        Layout.row: index * 2
                        Layout.column: 1
                        Layout.topMargin: index > 0 ? 12 : 0
                        text: qsTr("Use defaults from Settings")
                        checked: (dialog.chosenHilites[modelData.name]
                                  === "default")
                        // from the user-wide one, or back to it; and bound to
                        // the value again, which clicking undoes
                        onToggled: {
                            const user = dialog.userHilites[modelData.name]
                            dialog.chooseHilite(modelData.name,
                                                checked ? "default"
                                                        : user || "none")
                            checked = Qt.binding(
                                () => (dialog.chosenHilites[modelData.name]
                                       === "default"))
                        }

                        FocusRing {}
                    }
                }
                Repeater {
                    model: dialog.hilites

                    delegate: HiliteEditor {
                        id: editor

                        required property var modelData
                        required property int index

                        readonly property string own:
                            dialog.chosenHilites[modelData.name] ?? "default"

                        Layout.row: index * 2 + 1
                        Layout.column: 1
                        Layout.preferredWidth: Math.max(420, colorsWidth)
                        enabled: own !== "default"
                        value: (own === "default"
                                ? dialog.userHilites[modelData.name] ?? ""
                                : own)
                        sample: modelData.sample
                        onEdited: v => dialog.chooseHilite(modelData.name,
                                                           v || "none")
                    }
                }
            }
        }
    }

    // the other options of the MUD, saved with it
    PowwowDialog {
        id: advanced

        title: qsTr("Advanced")
        standardButtons: Dialog.Ok

        // the checkboxes as chosen, also after the MUD dialog shows another
        onAboutToShow: {
            for (const name in dialog.optionBoxes)
                dialog.optionBoxes[name].checked = dialog.chosen[name] === true
        }

        ColumnLayout {
            spacing: 4

            Repeater {
                model: dialog.optionGroups

                ColumnLayout {
                    id: group

                    required property var modelData
                    required property int index

                    Layout.topMargin: index > 0 ? 8 : 0
                    spacing: 4

                    Label {
                        text: group.modelData.title
                        font.bold: true
                    }
                    Repeater {
                        model: group.modelData.options

                        CheckBox {
                            id: box

                            required property var modelData

                            text: modelData.text
                            onToggled: dialog.chosen[modelData.name] = checked
                            Component.onCompleted:
                                dialog.optionBoxes[modelData.name] = box

                            FocusRing {}
                        }
                    }
                    // #setvar keepalive: that of Settings, or the MUD's own
                    CheckBox {
                        id: keepaliveBox

                        visible: group.modelData.keepalive === true
                        text: qsTr("Use the keepalive from Settings")
                        onToggled: {
                            if (checked)
                                keepaliveField.text = (dialog.userKeepalive
                                                       === "0"
                                                       ? ""
                                                       : dialog.userKeepalive)
                        }
                        Component.onCompleted: {
                            if (group.modelData.keepalive === true) {
                                dialog.keepaliveBox = keepaliveBox
                                dialog.showKeepalive()
                            }
                        }

                        FocusRing {}
                    }
                    RowLayout {
                        visible: group.modelData.keepalive === true
                        Layout.leftMargin: 28
                        spacing: 8

                        Label {
                            enabled: !keepaliveBox.checked
                            text: qsTr("Send a keepalive after")
                        }
                        EntryField {
                            id: keepaliveField

                            Layout.preferredWidth: 100
                            enabled: !keepaliveBox.checked
                            placeholderText: qsTr("never")
                            validator: IntValidator {
                                bottom: 0
                                top: 86400
                            }
                            // 0 is never, which the empty field shows
                            onEditingFinished: {
                                if (acceptableInput && parseInt(text, 10) === 0)
                                    text = ""
                            }
                            Component.onCompleted: {
                                if (group.modelData.keepalive === true) {
                                    dialog.keepaliveField = keepaliveField
                                    dialog.showKeepalive()
                                }
                            }
                        }
                        Label {
                            enabled: !keepaliveBox.checked
                            text: qsTr("idle seconds")
                        }
                    }
                }
            }
        }
    }

    footer: DialogButtonBox {
        Button {
            text: dialog.mud === "" ? qsTr("Add") : qsTr("Save")
            highlighted: true
            enabled: dialog.valid
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
        Button {
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }
}
