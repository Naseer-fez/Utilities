#pragma once
#include <atomic>
#include <cstddef>

template <typename T, size_t Capacity>
class SPSCRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
public:
    SPSCRingBuffer() : m_writeIndex(0), m_readIndex(0) {
        for (size_t i = 0; i < Capacity; ++i) {
            m_buffer[i] = nullptr;
        }
    }

    ~SPSCRingBuffer() {
        Clear();
    }

    // Push an item. Returns false if the queue is full.
    // Memory order release ensures that the written data in the buffer is visible
    // to the reader thread after it acquires the updated write index.
    bool Push(T item) {
        const size_t writeIdx = m_writeIndex.load(std::memory_order_relaxed);
        const size_t readIdx = m_readIndex.load(std::memory_order_acquire);

        if ((writeIdx - readIdx) >= Capacity) {
            return false; // Queue is full
        }

        m_buffer[writeIdx & (Capacity - 1)] = item;
        m_writeIndex.store(writeIdx + 1, std::memory_order_release);
        return true;
    }

    // Peek at the front item without popping it.
    // Returns nullptr if the queue is empty.
    T Peek() const {
        const size_t readIdx = m_readIndex.load(std::memory_order_relaxed);
        const size_t writeIdx = m_writeIndex.load(std::memory_order_acquire);

        if (readIdx == writeIdx) {
            return nullptr; // Queue is empty
        }

        return m_buffer[readIdx & (Capacity - 1)];
    }

    // Pop the front item. Returns false if empty.
    // OutItem takes ownership of the popped pointer.
    bool Pop(T& outItem) {
        const size_t readIdx = m_readIndex.load(std::memory_order_relaxed);
        const size_t writeIdx = m_writeIndex.load(std::memory_order_acquire);

        if (readIdx == writeIdx) {
            return false; // Queue is empty
        }

        const size_t offset = readIdx & (Capacity - 1);
        outItem = m_buffer[offset];
        m_buffer[offset] = nullptr;
        m_readIndex.store(readIdx + 1, std::memory_order_release);
        return true;
    }

    // Pop and discard the front item, releasing it.
    bool PopAndDiscard() {
        T item = nullptr;
        if (Pop(item)) {
            if (item) {
                item->Release();
            }
            return true;
        }
        return false;
    }

    // Returns the exact size of the queue.
    size_t Size() const {
        const size_t writeIdx = m_writeIndex.load(std::memory_order_relaxed);
        const size_t readIdx = m_readIndex.load(std::memory_order_relaxed);
        return (writeIdx >= readIdx) ? (writeIdx - readIdx) : 0;
    }

    bool IsEmpty() const {
        return Size() == 0;
    }

    // Safely clears the queue, releasing all stored elements.
    void Clear() {
        T item = nullptr;
        while (Pop(item)) {
            if (item) {
                item->Release();
            }
        }
    }

private:
    // Place index variables on separate cache lines to avoid false sharing.
    // MSVC supports alignas(64) on member variables.
    alignas(64) std::atomic<size_t> m_writeIndex;
    alignas(64) std::atomic<size_t> m_readIndex;
    
    // Store elements inside the buffer.
    alignas(64) T m_buffer[Capacity];
};
