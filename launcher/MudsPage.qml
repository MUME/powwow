pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Powwow

// the MUDs: their definition files, to connect to, add, edit, duplicate,
// look at and remove
Page {
    id: page

    // whether a MUD is selected, for the buttons and keys that act on it
    readonly property bool selected: list.currentIndex >= 0
    // whether the keys of the page act: not while a dialog is open
    readonly property bool keys: (visible && !mudDialog.opened
                                  && !removeDialog.opened
                                  && !duplicateDialog.opened
                                  && !infoDialog.opened)

    // the place in the list of the MUD being dragged, -1 if none is
    property int dragged: -1

    // the definition file of the MUD selected, empty if none is
    function currentMud() {
        return (list.currentIndex >= 0 && list.currentIndex < muds.count
                ? muds.get(list.currentIndex).file : "")
    }

    // the MUDs and their hosts, as the launcher has them, in a model of
    // the list's own, which they move in while one is dragged
    function sync() {
        const current = currentMud()
        muds.clear()
        for (let i = 0; i < Launcher.muds.length; ++i)
            muds.append({ file: Launcher.muds[i],
                          host: Launcher.hosts[i] ?? "" })
        select(current !== "" ? current : (muds.count > 0
                                           ? muds.get(0).file : ""))
    }

    function select(file) {
        for (let i = 0; i < muds.count; ++i) {
            if (muds.get(i).file === file) {
                list.currentIndex = i
                return
            }
        }
        list.currentIndex = Math.min(list.currentIndex, muds.count - 1)
    }

    // the order of the list after a drag, which the launcher keeps
    function saveOrder() {
        const order = []
        for (let i = 0; i < muds.count; ++i)
            order.push(muds.get(i).file)
        Launcher.setOrder(order)
    }

    // copies of the definition files at 'urls' as new MUDs, the last of
    // them selected
    function importFiles(urls) {
        let last = ""
        for (const url of urls) {
            const file = Launcher.importFile(url)
            if (file === "")
                break
            last = file
        }
        if (last !== "")
            select(last)
    }

    function connectMud() {
        if (selected)
            Launcher.connect(currentMud())
    }

    function addMud() {
        mudDialog.mud = ""
        mudDialog.open()
    }

    function editMud() {
        if (selected && Launcher.editable(currentMud())) {
            mudDialog.mud = currentMud()
            mudDialog.open()
        }
    }

    function duplicateMud() {
        if (selected) {
            duplicateDialog.mud = currentMud()
            duplicateDialog.open()
        }
    }

    function infoMud() {
        if (selected) {
            infoDialog.mud = currentMud()
            infoDialog.open()
        }
    }

    // the keys to the list, not from the keyboard
    function focusList() {
        list.forceActiveFocus()
        list.byKeys = false
    }

    // whether the list takes the keys when the page is shown
    property bool takeFocus: true

    // the list has the keys when the page is shown, and after a dialog
    onVisibleChanged: {
        if (visible && takeFocus)
            page.focusList()
    }
    Component.onCompleted: {
        sync()
        page.focusList()
    }

    ListModel {
        id: muds
    }

    Connections {
        target: Launcher

        function onMudsChanged() {
            page.sync()
        }
        function onHostsChanged() {
            page.sync()
        }
    }

    // the commands on a MUD, with their keys, which the buttons and the
    // menu of the MUDs show
    Action {
        id: connectAction

        text: qsTr("Connect")
        shortcut: StandardKey.Open
        enabled: page.keys && page.selected
        onTriggered: page.connectMud()
    }
    Action {
        id: addAction

        text: qsTr("Add…")
        shortcut: StandardKey.New
        enabled: page.keys
        onTriggered: page.addMud()
    }
    Action {
        id: importAction

        text: qsTr("Import…")
        enabled: page.keys
        onTriggered: importPicker.open()
    }
    Action {
        id: editAction

        text: qsTr("Edit…")
        shortcut: "Ctrl+E"
        enabled: page.keys && page.selected
        onTriggered: page.editMud()
    }
    Action {
        id: duplicateAction

        text: qsTr("Duplicate…")
        shortcut: "Ctrl+D"
        enabled: page.keys && page.selected
        onTriggered: page.duplicateMud()
    }
    Action {
        id: infoAction

        text: qsTr("Info…")
        shortcut: "Ctrl+I"
        enabled: page.keys && page.selected
        onTriggered: page.infoMud()
    }
    // ⌘⌫ on macOS, as Finder moves a file to the trash
    Action {
        id: removeAction

        text: qsTr("Remove…")
        shortcut: Qt.platform.os === "osx" ? "Ctrl+Backspace" : "Delete"
        enabled: page.keys && page.selected
        onTriggered: {
            if (Launcher.removable(page.currentMud()))
                removeDialog.open()
        }
    }
    // more keys: F2 edits, as it renames in the file managers, and the
    // other key of removing
    Shortcut {
        sequences: ["F2"]
        enabled: editAction.enabled
        onActivated: editAction.trigger()
    }
    Shortcut {
        sequences: [Qt.platform.os === "osx" ? "Delete" : "Ctrl+Backspace"]
        enabled: removeAction.enabled
        onActivated: removeAction.trigger()
    }

    // the commands on the MUD selected: from the button ⋯, or by
    // right-clicking a MUD, or the key Menu or Shift+F10 in the list
    Menu {
        id: menu

        // when it last closed, in milliseconds
        property double closedAt: 0

        onClosed: closedAt = Date.now()

        ShortcutMenuItem {
            action: connectAction
        }
        ShortcutMenuItem {
            action: addAction
        }
        ShortcutMenuItem {
            action: importAction
        }
        MenuSeparator {}
        ShortcutMenuItem {
            action: editAction
        }
        ShortcutMenuItem {
            action: duplicateAction
        }
        ShortcutMenuItem {
            action: infoAction
        }
        MenuSeparator {}
        ShortcutMenuItem {
            action: removeAction
        }
    }

    // the name of a MUD, that of its file without .powwow
    function mudName(file) {
        return file.replace(/\.powwow$/, "")
    }

    // the host of #host, "mume.org 4242 --tls --utf8", as people read it
    function hostText(host) {
        const words = host.split(" ")
        if (words.length < 2)
            return qsTr("no host")
        let text = words[0] + " " + words[1]
        if (!words.includes("--no-tls"))
            text += " · TLS"
        if (words.includes("--latin1"))
            text += " · ISO-8859-1"
        return text
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12

        Label {
            text: qsTr("MUDs")
            font.pixelSize: 28
        }

        RowLayout {
            spacing: 8

            Button {
                action: connectAction
                highlighted: true

                FocusRing {}
            }
            Button {
                action: addAction

                FocusRing {}
            }
            Button {
                id: more

                text: "⋯"
                Accessible.name: qsTr("More")

                PowwowToolTip {
                    visible: more.hovered
                    text: qsTr("More commands")
                }
                // a click on the button closes the menu if it is open:
                // pressing the button closed it, as a press outside it
                onClicked: {
                    if (Date.now() - menu.closedAt > 300)
                        menu.popup(more, 0, more.height)
                }

                FocusRing {}
            }
        }

        Label {
            Layout.fillWidth: true
            visible: !Launcher.usable
            text: (Launcher.powwow === ""
                   ? Launcher.notFound()
                   : Launcher.powwowChoosable
                   ? qsTr("%1 is too old for the launcher: choose a newer"
                          + " powwow in the settings.").arg(Launcher.powwow)
                   : qsTr("%1 is too old for the launcher: install powwow"
                          + " again.").arg(Launcher.powwow))
            wrapMode: Text.Wrap
            color: "#c42b1c"
        }

        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 1

            // files dropped on the list are imported
            DropArea {
                anchors.fill: parent
                onEntered: function(drag) {
                    drag.accepted = drag.hasUrls
                }
                onDropped: function(drop) {
                    if (drop.hasUrls) {
                        page.importFiles(drop.urls)
                        drop.accept(Qt.CopyAction)
                    }
                }
            }

            ListView {
                id: list

                anchors.fill: parent
                // whether the keys came to it from the keyboard
                property bool byKeys: false

                clip: true
                focus: true
                activeFocusOnTab: true
                // by Tab, as the launcher clears it when it moves the keys
                // here itself
                onActiveFocusChanged: byKeys = activeFocus
                Keys.onReturnPressed: page.connectMud()
                Keys.onEnterPressed: page.connectMud()
                Keys.onPressed: function(event) {
                    byKeys = true
                    if (event.key === Qt.Key_Menu
                        || (event.key === Qt.Key_F10
                            && event.modifiers & Qt.ShiftModifier)) {
                        menu.popup(currentItem ?? list, 0,
                                   currentItem?.height ?? 0)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Home && count > 0) {
                        currentIndex = 0
                        event.accepted = true
                    } else if (event.key === Qt.Key_End && count > 0) {
                        currentIndex = count - 1
                        event.accepted = true
                    }
                }
                model: muds
                // the mouse drags the MUDs, not the list, which the wheel
                // scrolls; Qt 6.8 has no acceptedButtons, where the MUDs
                // keep the drag from the list
                Component.onCompleted: {
                    if ("acceptedButtons" in list)
                        list.acceptedButtons = Qt.NoButton
                }
                // the MUDs make room for the one dragged
                moveDisplaced: Transition {
                    NumberAnimation {
                        properties: "y"
                        duration: 150
                    }
                }

                // a place in the list, which a MUD dragged over takes
                delegate: Item {
                    id: slot

                    required property string file
                    required property string host
                    required property int index

                    width: ListView.view.width
                    height: mud.implicitHeight

                    DropArea {
                        anchors.fill: parent
                        // only MUDs dragged, not files dropped to import
                        keys: ["powwow-mud"]
                        // the place, which the move gives another MUD
                        onEntered: {
                            const to = slot.index
                            if (page.dragged >= 0 && page.dragged !== to) {
                                muds.move(page.dragged, to, 1)
                                page.dragged = to
                            }
                        }
                    }

                    ListRow {
                        id: mud

                        width: slot.width
                        anchors.horizontalCenter: slot.horizontalCenter
                        anchors.verticalCenter: slot.verticalCenter
                        highlighted: slot.ListView.isCurrentItem
                        listFocused: list.activeFocus
                        byKeys: list.byKeys
                        Drag.active: mouse.drag.active
                        Drag.keys: ["powwow-mud"]
                        Drag.hotSpot.x: width / 2
                        Drag.hotSpot.y: height / 2

                        // clicks select, a double click connects, the right
                        // button opens the menu, and dragging moves the MUD
                        // (a MouseArea, as a DragHandler stops when the MUD
                        // leaves its place while dragged)
                        MouseArea {
                            id: mouse

                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            drag.target: mud
                            drag.axis: Drag.YAxis
                            preventStealing: true
                            onPressed: function(event) {
                                list.byKeys = false
                                list.currentIndex = slot.index
                                page.dragged = slot.index
                                if (event.button === Qt.RightButton)
                                    menu.popup()
                            }
                            onDoubleClicked: function(event) {
                                if (event.button === Qt.LeftButton)
                                    Launcher.connect(slot.file)
                            }
                            onReleased: {
                                const moved = drag.active
                                page.dragged = -1
                                if (moved)
                                    page.saveOrder()
                            }
                        }

                        // while dragged, above the list rather than in its
                        // place
                        states: State {
                            when: mouse.drag.active

                            ParentChange {
                                target: mud
                                parent: list
                            }
                            AnchorChanges {
                                target: mud
                                anchors.horizontalCenter: undefined
                                anchors.verticalCenter: undefined
                            }
                            PropertyChanges {
                                mud.z: 2
                                mud.opacity: 0.85
                            }
                        }

                        contentItem: ColumnLayout {
                            spacing: 2

                            Label {
                                Layout.fillWidth: true
                                text: page.mudName(slot.file)
                                color: mud.textColor
                                elide: Text.ElideRight
                            }
                            Label {
                                Layout.fillWidth: true
                                text: page.hostText(slot.host)
                                color: mud.textColor
                                visible: Launcher.usable
                                opacity: 0.65
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    text: qsTr("No MUDs yet: add one.")
                    opacity: 0.65
                }
            }
        }

        Label {
            Layout.fillWidth: true
            text: Launcher.folder
            opacity: 0.65
            font.pixelSize: 12
            elide: Text.ElideMiddle
        }
    }

    MudDialog {
        id: mudDialog

        onClosed: page.focusList()

        onDone: function(file) {
            page.select(file)
        }
    }

    DuplicateDialog {
        id: duplicateDialog

        onDone: function(file) {
            page.select(file)
        }
        onClosed: page.focusList()
    }

    // definition files to import, such as those of an older powwow
    FileDialog {
        id: importPicker

        title: qsTr("Import definition files")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("All files (*)"),
                      qsTr("Definition files (*.powwow)")]
        onAccepted: page.importFiles(selectedFiles)
    }

    InfoDialog {
        id: infoDialog

        onClosed: page.focusList()
    }

    PowwowDialog {
        id: removeDialog

        // a width of its own, as one from the buttons makes a binding loop
        width: Math.min(420, page.width - 48)
        title: qsTr("Remove %1?")
            .arg(page.mudName(page.currentMud()))
        standardButtons: Dialog.Yes | Dialog.No

        onClosed: page.focusList()

        Label {
            text: qsTr("Its definition file goes to the trash.")
        }

        onAccepted: {
            Launcher.remove(page.currentMud())
        }
    }
}
