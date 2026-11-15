pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Powwow

// the definition file of the MUD 'mud': where it is, and its definitions
// by kind, as lines of the file that can be copied
PowwowDialog {
    id: dialog

    // the definition file
    property string mud
    // what Launcher.info() says of it
    property var info: ({})
    readonly property var sections: info.sections ?? []
    // the kind of definitions shown
    property int current: 0
    readonly property var section: (current < sections.length
                                    ? sections[current] : null)

    width: Math.min(ApplicationWindow.window.width - 80, 760)
    height: Math.min(ApplicationWindow.window.height - 80, 560)
    title: mud.replace(/\.powwow$/, "")
    standardButtons: Dialog.Close

    onOpened: {
        info = Launcher.info(mud)
        current = 0
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        SelectableText {
            Layout.fillWidth: true
            text: dialog.info.path ?? ""
            wrapMode: TextEdit.WrapAnywhere
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Label {
                Layout.fillWidth: true
                text: qsTr("%1, saved %2").arg(dialog.info.size ?? "")
                    .arg(dialog.info.modified ?? "")
                opacity: 0.65
                elide: Text.ElideRight
            }
            Button {
                text: (Qt.platform.os === "osx"
                       ? qsTr("Show in Finder")
                       : Qt.platform.os === "windows"
                       ? qsTr("Show in Explorer")
                       : qsTr("Show the folder"))
                onClicked: Launcher.reveal(dialog.mud)

                FocusRing {}
            }
        }

        // the kinds of definitions, as a segmented control as macOS draws
        // one: the tabs of Qt do not show which one is chosen in the macOS
        // style, nor that they are tabs in the Fluent style of Windows, on
        // the white of a dialog
        // a rounded track with the choices, the one shown raised in it
        Rectangle {
            // as wide as the segments, but no wider than the dialog, where
            // they wrap
            Layout.preferredWidth: {
                let width = 2
                for (const child of segments.children)
                    width += child.implicitWidth + segments.spacing
                return width + 2
            }
            Layout.maximumWidth: dialog.availableWidth
            implicitHeight: segments.implicitHeight + 4
            radius: 7
            color: (dialog.dark ? Qt.rgba(1, 1, 1, 0.08)
                                : Qt.rgba(0, 0, 0, 0.06))

            Flow {
                id: segments

                x: 2
                y: 2
                width: parent.width - 4
                spacing: 2

                Repeater {
                    model: dialog.sections

                    AbstractButton {
                        id: segment

                        required property var modelData
                        required property int index
                        readonly property bool chosen: dialog.current === index

                        text: "%1 (%2)".arg(modelData.title)
                            .arg(modelData.count)
                        padding: 4
                        leftPadding: 12
                        rightPadding: 12
                        onClicked: dialog.current = index

                        contentItem: Label {
                            text: segment.text
                            horizontalAlignment: Text.AlignHCenter
                        }
                        background: Rectangle {
                            radius: 5
                            color: (!segment.chosen ? "transparent"
                                    : dialog.dark ? Qt.rgba(1, 1, 1, 0.22)
                                                  : "white")
                            border.width: (segment.chosen && !dialog.dark
                                           ? 1 : 0)
                            border.color: Qt.rgba(0, 0, 0, 0.12)
                        }

                        FocusRing {}
                    }
                }
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: (dialog.section?.count ?? 0) > 0

            SelectableText {
                text: dialog.section?.text ?? ""
                wrapMode: TextEdit.NoWrap
                font: Launcher.fixedFont
            }
        }
        // or what to say if the file has none of the kind
        Label {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !(dialog.section?.count > 0)
            text: dialog.section?.none ?? ""
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignTop
            topPadding: 24
            opacity: 0.65
        }
    }
}
