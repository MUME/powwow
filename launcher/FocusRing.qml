import QtQuick
import QtQuick.Templates as T
import Powwow

// the outline of the keyboard focus around the control that it is in,
// which Tab has given the focus, for the styles whose own does not stand
// out, such as Fusion and Basic, where it looks like that of a default
// button; those of macOS and Windows 11 have one of their own
Rectangle {
    // a control of any style, which derives from the template, not from
    // the Control of the style
    readonly property T.Control control: parent as T.Control

    objectName: "focusRing"
    anchors.fill: parent
    anchors.margins: -3
    z: 10
    visible: (Launcher.style !== "macOS"
              && Launcher.style !== "FluentWinUI3"
              && (control?.visualFocus ?? false))
    color: "transparent"
    radius: 5
    border.width: 2
    border.color: control?.palette.highlight ?? "transparent"
}
