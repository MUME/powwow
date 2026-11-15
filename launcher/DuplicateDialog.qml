import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Powwow

// a copy of the MUD 'mud', with all its definitions, under a new name
PowwowDialog {
    id: dialog

    // the definition file copied
    property string mud
    readonly property string mudName: mud.replace(/\.powwow$/, "")

    // the definition file of the copy
    signal done(string file)

    title: qsTr("Duplicate %1").arg(mudName)

    onOpened: {
        name.text = qsTr("%1 copy").arg(mudName)
        name.forceActiveFocus()
        name.selectAll()
    }

    onAccepted: {
        const file = Launcher.duplicate(mud, name.text.trim())
        if (file !== "")
            done(file)
    }

    RowLayout {
        anchors.fill: parent
        spacing: 12

        Label {
            text: qsTr("Name")
        }
        EntryField {
            id: name

            Layout.fillWidth: true
            Layout.minimumWidth: 240
            onAccepted: {
                if (text.trim() !== "")
                    dialog.accept()
            }
        }
    }

    footer: DialogButtonBox {
        Button {
            text: qsTr("Duplicate")
            highlighted: true
            enabled: name.text.trim() !== ""
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
        Button {
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }
}
