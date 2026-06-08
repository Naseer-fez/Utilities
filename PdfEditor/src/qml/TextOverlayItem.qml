import QtQuick
import QtQuick.Controls
import PdfEditor

Item {
    id: root

    required property var overlayObject
    required property var pdfView

    // These update from C++ when zoom changes because pdfToScreen depends on zoom
    // We bind the QML item's x/y/width/height to the computed screen coordinates.
    // However, when the user drags, we want to update the relative coordinates in C++.
    
    // We use a helper function to sync back to C++
    function syncToModel() {
        var relPos = pdfView.screenToPdf(overlayObject.pageIndex, Qt.point(x, y))
        var relSize = pdfView.screenToPdf(overlayObject.pageIndex, Qt.point(width, height))
        
        // Disconnect bindings temporarily? No, QML handles breaking bindings if we assign, 
        // but since we rely on getters, we just set the C++ model and let bindings re-evaluate.
        overlayObject.x = relPos.x
        overlayObject.y = relPos.y
        overlayObject.width = relSize.x
        overlayObject.height = relSize.y
    }

    // Bind to model properties via pdfToScreen
    x: pdfView.pdfToScreen(overlayObject.pageIndex, Qt.point(overlayObject.x, overlayObject.y)).x
    y: pdfView.pdfToScreen(overlayObject.pageIndex, Qt.point(overlayObject.x, overlayObject.y)).y
    width: pdfView.pdfToScreen(overlayObject.pageIndex, Qt.point(overlayObject.width, overlayObject.height)).x
    height: pdfView.pdfToScreen(overlayObject.pageIndex, Qt.point(overlayObject.width, overlayObject.height)).y

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: dragArea.pressed ? "blue" : "lightgray"
        border.width: 1

        TextEdit {
            anchors.fill: parent
            anchors.margins: 4
            text: overlayObject.text
            color: overlayObject.color
            wrapMode: TextEdit.Wrap
            font.pixelSize: 16 * pdfView.zoom // Scale font with zoom

            onTextChanged: {
                if (overlayObject.text !== text) {
                    overlayObject.text = text;
                }
            }
        }

        MouseArea {
            id: dragArea
            anchors.fill: parent
            drag.target: root
            drag.axis: Drag.XAndYAxis
            
            // Only drag when clicking the background, not the text edit?
            // Since TextEdit fills parent, this is tricky. We'll let it be dragged from edges.
            anchors.margins: 10 
            
            onPositionChanged: {
                if (drag.active) {
                    syncToModel();
                }
            }
            onReleased: {
                syncToModel();
            }
        }
    }
}
