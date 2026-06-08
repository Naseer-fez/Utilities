import QtQuick
import QtQuick.Controls
import PdfEditor


Item {
    id: root

    required property var overlayObject
    required property var pdfView

    function syncToModel() {
        var relPos = pdfView.screenToPdf(overlayObject.pageIndex, Qt.point(x, y))
        var relSize = pdfView.screenToPdf(overlayObject.pageIndex, Qt.point(width, height))
        
        overlayObject.x = relPos.x
        overlayObject.y = relPos.y
        overlayObject.width = relSize.x
        overlayObject.height = relSize.y
    }

    x: pdfView.pdfToScreen(overlayObject.pageIndex, Qt.point(overlayObject.x, overlayObject.y)).x
    y: pdfView.pdfToScreen(overlayObject.pageIndex, Qt.point(overlayObject.x, overlayObject.y)).y
    width: pdfView.pdfToScreen(overlayObject.pageIndex, Qt.point(overlayObject.width, overlayObject.height)).x
    height: pdfView.pdfToScreen(overlayObject.pageIndex, Qt.point(overlayObject.width, overlayObject.height)).y

    Rectangle {
        anchors.fill: parent
        color: overlayObject.color

        MouseArea {
            id: dragArea
            anchors.fill: parent
            drag.target: root
            drag.axis: Drag.XAndYAxis
            
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
