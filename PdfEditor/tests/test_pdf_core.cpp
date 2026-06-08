#include <gtest/gtest.h>
#include "../src/core/PdfCore.h"

using namespace PdfEditor;

TEST(PdfCoreTest, InitAndDestroy) {
    PdfCore core;
    EXPECT_EQ(core.getPageCount(), 0);
}
