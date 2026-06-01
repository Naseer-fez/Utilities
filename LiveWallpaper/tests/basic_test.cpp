#include <gtest/gtest.h>
#include "../src/utils.h"
#include "../src/spsc_ring_buffer.h"
#include "../src/playlist_manager.h"
#include "../src/render_state_machine.h"
#include <string>
#include <vector>

TEST(LiveWallpaperTests, FilePathValidation) {
    EXPECT_FALSE(Utils::ValidateFilePath(L""));
    EXPECT_FALSE(Utils::ValidateFilePath(L"not_a_file_path"));
    EXPECT_FALSE(Utils::ValidateFilePath(L"C:\\invalid_file_that_doesnt_exist.mp4"));
    EXPECT_FALSE(Utils::ValidateFilePath(L"C:\\Windows\\System32\\cmd.exe")); // Invalid extension
}

TEST(LiveWallpaperTests, DirectoryTraversalRejection) {
    EXPECT_FALSE(Utils::ValidateFilePath(L"C:\\Windows\\System32\\..\\System32\\invalid.mp4"));
}

class MockRelease {
public:
    int* pReleaseCount;
    MockRelease(int* counter) : pReleaseCount(counter) {}
    void Release() {
        if (pReleaseCount) {
            (*pReleaseCount)++;
        }
        delete this;
    }
};

TEST(LiveWallpaperTests, SPSCRingBufferBasic) {
    SPSCRingBuffer<MockRelease*, 4> queue;
    int releaseCount = 0;

    MockRelease* m1 = new MockRelease(&releaseCount);
    MockRelease* m2 = new MockRelease(&releaseCount);
    MockRelease* m3 = new MockRelease(&releaseCount);
    MockRelease* m4 = new MockRelease(&releaseCount);

    EXPECT_TRUE(queue.IsEmpty());
    EXPECT_EQ(queue.Size(), 0);

    EXPECT_TRUE(queue.Push(m1));
    EXPECT_TRUE(queue.Push(m2));
    EXPECT_TRUE(queue.Push(m3));

    EXPECT_EQ(queue.Size(), 3);
    EXPECT_FALSE(queue.IsEmpty());

    EXPECT_EQ(queue.Peek(), m1);

    MockRelease* popped = nullptr;
    EXPECT_TRUE(queue.Pop(popped));
    EXPECT_EQ(popped, m1);
    EXPECT_EQ(queue.Size(), 2);

    // Test pushing up to capacity (4)
    EXPECT_TRUE(queue.Push(m4));
    EXPECT_EQ(queue.Size(), 3);

    MockRelease* m5 = new MockRelease(&releaseCount);
    EXPECT_TRUE(queue.Push(m5));
    EXPECT_EQ(queue.Size(), 4);

    MockRelease* m6 = new MockRelease(&releaseCount);
    EXPECT_FALSE(queue.Push(m6)); // Queue is full, should fail
    delete m6;

    // Discard remaining elements
    queue.Clear();
    EXPECT_EQ(queue.Size(), 0);
    EXPECT_TRUE(queue.IsEmpty());

    // m2, m3, m4, m5 were cleared by Clear() which calls Release()
    EXPECT_EQ(releaseCount, 4);

    // Release m1 manually
    m1->Release();
    EXPECT_EQ(releaseCount, 5);
}

TEST(LiveWallpaperTests, PlaylistManagerBasic) {
    PlaylistManager pm;
    EXPECT_TRUE(pm.IsEmpty());
    EXPECT_EQ(pm.GetCurrentTrack(), L"");

    std::vector<std::wstring> tracks = { L"video1.mp4", L"shader1.hlsl", L"video2.mp4" };
    pm.SetPlaylist(tracks, 0);

    EXPECT_FALSE(pm.IsEmpty());
    EXPECT_EQ(pm.GetCurrentIndex(), 0);
    EXPECT_EQ(pm.GetCurrentTrack(), L"video1.mp4");

    // Test Next
    EXPECT_TRUE(pm.Next());
    EXPECT_EQ(pm.GetCurrentIndex(), 1);
    EXPECT_EQ(pm.GetCurrentTrack(), L"shader1.hlsl");

    EXPECT_TRUE(pm.Next());
    EXPECT_EQ(pm.GetCurrentIndex(), 2);
    EXPECT_EQ(pm.GetCurrentTrack(), L"video2.mp4");

    // Loop Next
    EXPECT_TRUE(pm.Next());
    EXPECT_EQ(pm.GetCurrentIndex(), 0);
    EXPECT_EQ(pm.GetCurrentTrack(), L"video1.mp4");

    // Test Previous
    EXPECT_TRUE(pm.Previous());
    EXPECT_EQ(pm.GetCurrentIndex(), 2);
    EXPECT_EQ(pm.GetCurrentTrack(), L"video2.mp4");

    // Test Clear
    pm.Clear();
    EXPECT_TRUE(pm.IsEmpty());
    EXPECT_EQ(pm.GetCurrentTrack(), L"");
}

TEST(LiveWallpaperTests, PlaylistManagerTimings) {
    PlaylistManager pm;
    std::vector<std::wstring> tracks = { L"video1.mp4", L"shader1.hlsl" };
    pm.SetPlaylist(tracks, 0);
    pm.SetRotationInterval(1); // 1 minute = 60,000 ms

    // Under the threshold (59 seconds)
    EXPECT_FALSE(pm.Update(59000.0));
    EXPECT_EQ(pm.GetCurrentIndex(), 0);

    // Over the threshold (1 more second = 60 seconds total)
    EXPECT_TRUE(pm.Update(1000.0));
    EXPECT_EQ(pm.GetCurrentIndex(), 1);
    EXPECT_EQ(pm.GetCurrentTrack(), L"shader1.hlsl");
}

TEST(LiveWallpaperTests, RenderStateMachineTransitions) {
    RenderStateMachine rsm;
    EXPECT_EQ(rsm.GetState(), RenderState::Uninitialized);

    // Stopped transition
    RenderState s = rsm.DetermineNextState(
        false, // isRunning
        false, // isPaused
        true,  // hasHWnd
        false, // isShader
        false, // isDecoderLoaded
        false  // isDeviceLost
    );
    EXPECT_EQ(s, RenderState::Stopped);

    // Uninitialized when no HWnd
    s = rsm.DetermineNextState(
        true,  // isRunning
        false, // isPaused
        false, // hasHWnd
        false, // isShader
        false, // isDecoderLoaded
        false  // isDeviceLost
    );
    EXPECT_EQ(s, RenderState::Uninitialized);

    // Recreating when device lost
    s = rsm.DetermineNextState(
        true,  // isRunning
        false, // isPaused
        true,  // hasHWnd
        false, // isShader
        false, // isDecoderLoaded
        true   // isDeviceLost
    );
    EXPECT_EQ(s, RenderState::Recreating);

    // Paused
    s = rsm.DetermineNextState(
        true,  // isRunning
        true,  // isPaused
        true,  // hasHWnd
        false, // isShader
        false, // isDecoderLoaded
        false  // isDeviceLost
    );
    EXPECT_EQ(s, RenderState::Paused);

    // RenderingShader
    s = rsm.DetermineNextState(
        true,  // isRunning
        false, // isPaused
        true,  // hasHWnd
        true,  // isShader
        false, // isDecoderLoaded
        false  // isDeviceLost
    );
    EXPECT_EQ(s, RenderState::RenderingShader);

    // DecodingVideo
    s = rsm.DetermineNextState(
        true,  // isRunning
        false, // isPaused
        true,  // hasHWnd
        false, // isShader
        true,  // isDecoderLoaded
        false  // isDeviceLost
    );
    EXPECT_EQ(s, RenderState::DecodingVideo);

    // Idle state
    s = rsm.DetermineNextState(
        true,  // isRunning
        false, // isPaused
        true,  // hasHWnd
        false, // isShader
        false, // isDecoderLoaded
        false  // isDeviceLost
    );
    EXPECT_EQ(s, RenderState::Idle);
}
