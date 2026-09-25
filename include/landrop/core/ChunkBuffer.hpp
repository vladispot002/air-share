#pragma once

#include <vector>
#include <mutex>
#include <stdexcept>
#include <string>
#include <cstddef>
#include <utility>

namespace landrop {

template <typename T>
class ChunkBuffer {
public:
    explicit ChunkBuffer(size_t capacity = 64, std::string tag = "DefaultBuffer")
        : m_capacity(capacity), m_tag(std::move(tag)) {
        m_items.reserve(capacity);
    }

    ~ChunkBuffer() = default;

    ChunkBuffer(const ChunkBuffer&) = delete;
    ChunkBuffer& operator=(const ChunkBuffer&) = delete;

    ChunkBuffer(ChunkBuffer&& other) noexcept {
        std::lock_guard<std::mutex> lock(other.m_mutex);
        m_items = std::move(other.m_items);
        m_capacity = other.m_capacity;
        m_tag = std::move(other.m_tag);
    }

    ChunkBuffer& operator=(ChunkBuffer&& other) noexcept {
        if (this != &other) {
            std::scoped_lock lock(m_mutex, other.m_mutex);
            m_items = std::move(other.m_items);
            m_capacity = other.m_capacity;
            m_tag = std::move(other.m_tag);
        }
        return *this;
    }

    bool push(const T& item) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_items.size() >= m_capacity) {
            return false;
        }
        m_items.push_back(item);
        return true;
    }

    bool push(T&& item) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_items.size() >= m_capacity) {
            return false;
        }
        m_items.push_back(std::move(item));
        return true;
    }

    bool pop(T& outItem) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_items.empty()) {
            return false;
        }
        outItem = std::move(m_items.front());
        m_items.erase(m_items.begin());
        return true;
    }

    T& operator[](size_t index) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (index >= m_items.size()) {
            throw std::out_of_range("ChunkBuffer index out of range");
        }
        return m_items[index];
    }

    const T& operator[](size_t index) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (index >= m_items.size()) {
            throw std::out_of_range("ChunkBuffer index out of range");
        }
        return m_items[index];
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_items.size();
    }

    size_t capacity() const {
        return m_capacity;
    }

    bool isEmpty() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_items.empty();
    }

    bool isFull() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_items.size() >= m_capacity;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_items.clear();
    }

    void resizeCapacity(size_t newCapacity) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_capacity = newCapacity;
        m_items.reserve(newCapacity);
    }

    const std::string& getTag() const {
        return m_tag;
    }

private:
    std::vector<T> m_items;
    size_t m_capacity;
    std::string m_tag;
    mutable std::mutex m_mutex;
};

}
