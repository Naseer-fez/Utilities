#pragma once

enum class RenderState {
    Uninitialized,
    Idle,
    DecodingVideo,
    RenderingShader,
    Paused,
    Recreating,
    Stopped
};

class RenderStateMachine {
public:
    RenderStateMachine() : m_state(RenderState::Uninitialized) {}

    RenderState GetState() const { return m_state; }

    void TransitionTo(RenderState newState) {
        m_state = newState;
    }

    RenderState DetermineNextState(bool isRunning, bool isPaused, bool hasHWnd, bool isShader, bool isDecoderLoaded, bool isDeviceLost) {
        if (!isRunning) {
            return RenderState::Stopped;
        }
        if (!hasHWnd) {
            return RenderState::Uninitialized;
        }
        if (isDeviceLost) {
            return RenderState::Recreating;
        }
        if (isPaused) {
            return RenderState::Paused;
        }
        if (isShader) {
            return RenderState::RenderingShader;
        }
        if (isDecoderLoaded) {
            return RenderState::DecodingVideo;
        }
        return RenderState::Idle;
    }

private:
    RenderState m_state = RenderState::Uninitialized;
};
