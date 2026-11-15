import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Powwow

// what the launcher and powwow are, their versions and license, in one
// text, so that any part of it can be selected and copied
Page {
    id: page

    // what powwow --version prints, read when the page is shown
    property string powwowVersion

    // 's' in HTML
    function html(s) {
        return s.replace(/&/g, "&amp;").replace(/</g, "&lt;")
            .replace(/>/g, "&gt;")
    }

    onVisibleChanged: {
        if (visible)
            powwowVersion = Launcher.powwowVersion()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12

        Label {
            text: "powwow"
            font.pixelSize: 28
        }

        SelectableText {
            id: about

            Layout.fillWidth: true
            textFormat: TextEdit.RichText
            text: [
                "<p>" + page.html(qsTr("A client for MUDs, multi-user games"
                                       + " played over telnet.")) + "</p>",
                "<p>" + page.html(qsTr("Launcher version %1, built with Qt"
                                       + " %2").arg(Launcher.version)
                                  .arg(Launcher.qtVersion)) + "<br>"
                + page.html(page.powwowVersion !== ""
                            ? page.powwowVersion
                            : qsTr("powwow was not found, or did not run."))
                + "<br><small>" + page.html(Launcher.powwow)
                + "</small></p>",
                "<p>" + page.html(qsTr("powwow is free software, under the"
                                       + " terms of the GNU General Public"
                                       + " License, version 2 or later; Qt"
                                       + " is under the GNU Lesser General"
                                       + " Public License, version 3."))
                + "</p>",
                // in the link colour of the system, which suits dark mode
                "<p><a href=\"https://github.com/MUME/powwow\">"
                + "github.com/MUME/powwow</a></p>",
            ].join("")
            onLinkActivated: function(link) {
                Qt.openUrlExternally(link)
            }

            HoverHandler {
                cursorShape: (about.hoveredLink !== ""
                              ? Qt.PointingHandCursor : Qt.IBeamCursor)
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
