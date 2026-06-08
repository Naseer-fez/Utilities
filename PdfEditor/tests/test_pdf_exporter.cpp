#include <gtest/gtest.h>

struct QmlToPdfCoords {
    double pdfX;
    double pdfY;
    double pdfW;
    double pdfH;
};

QmlToPdfCoords convertCoords(double qmlX, double qmlY, double qmlW, double qmlH, double pageWidth, double pageHeight) {
    double pdfX = qmlX * pageWidth;
    double pdfW = qmlW * pageWidth;
    double pdfH = qmlH * pageHeight;
    double pdfY = (1.0 - qmlY - qmlH) * pageHeight;
    return {pdfX, pdfY, pdfW, pdfH};
}

TEST(PdfExporterTest, CoordinateTranslation) {
    // Canvas size (standard Letter size in points: 612 x 792)
    double pageWidth = 612.0;
    double pageHeight = 792.0;

    // Test a rectangle at the top-left corner
    // QML: top-left is (0, 0), width 20% of page, height 10% of page
    auto coords = convertCoords(0.0, 0.0, 0.2, 0.1, pageWidth, pageHeight);
    
    EXPECT_DOUBLE_EQ(coords.pdfX, 0.0);
    EXPECT_DOUBLE_EQ(coords.pdfW, 122.4);
    EXPECT_DOUBLE_EQ(coords.pdfH, 79.2);
    // PDF Y-origin is bottom-left, so QML Y=0, H=0.1 top-left means
    // PDF Y starts at 792 - 79.2 = 712.8
    EXPECT_DOUBLE_EQ(coords.pdfY, 712.8);

    // Test a rectangle at the bottom-right corner
    // QML: bottom-right is at x=0.8, y=0.8, w=0.2, h=0.2
    coords = convertCoords(0.8, 0.8, 0.2, 0.2, pageWidth, pageHeight);
    EXPECT_DOUBLE_EQ(coords.pdfX, 489.6);
    EXPECT_DOUBLE_EQ(coords.pdfW, 122.4);
    EXPECT_DOUBLE_EQ(coords.pdfH, 158.4);
    // PDF Y should start at 0 (since 1.0 - 0.8 - 0.2 = 0)
    EXPECT_NEAR(coords.pdfY, 0.0, 1e-9);
}
