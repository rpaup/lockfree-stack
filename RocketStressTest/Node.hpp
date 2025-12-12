#pragma once
#include <atomic>

namespace lf {
    template<typename T>
    struct Node {
        T data;
        Node* next;

        // Версия нужна для отслеживания времени жизни узла
        // и безопасного освобождения памяти
        uint64_t version;

        Node() : next(nullptr), version(0) {}
        Node(T val) : data(val), next(nullptr), version(0) {}
    };
}