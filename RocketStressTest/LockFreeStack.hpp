#pragma once
#include <vector>
#include <atomic>
#include <limits>
#include <algorithm>
#include "Node.hpp"

namespace lf {

    template<typename T>
    class LockFreeVersionedStack {
    public:
        using NodePtr = Node<T>*;
        using AtomicNodePtr = std::atomic<Node<T>*>;
        using AtomicVersion = std::atomic_uint64_t;

        struct VersionedHead {
            AtomicVersion version;
            AtomicNodePtr head;
        };

        LockFreeVersionedStack(size_t subscribers_num) : subs_num_(subscribers_num) {
            stop_flag_.store(false);

            // Массив для отслеживания активных читателей
            // 0 означает, что поток не читает стек
            subscribers_ = new AtomicVersion[subscribers_num];
            for (size_t i = 0; i < subscribers_num; i++) {
                subscribers_[i].store(0);
            }

            stack_.head.store(nullptr);
            stack_.version.store(0); // Глобальный счетчик версий
        }

        ~LockFreeVersionedStack() {
            while (pop());

            for (auto& node : trash_) {
                delete node;
            }
            delete[] subscribers_;
        }

        void push(T value) {
            NodePtr new_node = new Node<T>(value);

            // Связываем новый узел с текущей головой
            new_node->next = stack_.head.load();

            // Каждая вставка увеличивает версию. Это гарантирует уникальность состояния.
            new_node->version = stack_.version.load() + 1;

            // Публикуем изменения.
            // Так как Writer один, достаточно атомарной записи без CAS-цикла.
            stack_.head.store(new_node);
            stack_.version.fetch_add(1);
        }

        bool pop() {
            NodePtr old_node = stack_.head.load();
            if (old_node == nullptr) {
                return false;
            }

            NodePtr new_head = old_node->next;

            // Поддерживаем целостность версий при удалении
            if (new_head != nullptr) {
                new_head->version = stack_.version.load() + 1;
            }

            // Логически исключаем узел из стека
            stack_.head.store(new_head);
            stack_.version.fetch_add(1);

            // Физическое удаление откладываем до проверки читателей
            update_trash(old_node);
            return true;
        }

        // Регистрируем читателя, чтобы сборщик мусора не удалил используемые данные
        bool subscribe(const unsigned int& id, NodePtr& stack_ptr) {
            if (stop_flag_.load()) return false;

            // Фиксируем версию, которую собираемся читать
            uint64_t current_ver = stack_.version.load(std::memory_order_relaxed);
            subscribers_[id].store(current_ver);

            // Получаем снимок данных
            stack_ptr = stack_.head.load();
            return true;
        }

        void unsubscribe(const unsigned int& id) {
            subscribers_[id].store(0);
        }

        void stop() {
            stop_flag_.store(true);
        }

        bool is_stopped() const {
            return stop_flag_.load();
        }

        uint64_t last_version() const {
            return stack_.version.load();
        }

    private:
        VersionedHead stack_;
        AtomicVersion* subscribers_;
        size_t subs_num_;

        std::vector<NodePtr> trash_; // Очередь на удаление
        std::atomic_bool stop_flag_;

        // Безопасная очистка памяти
        void update_trash(NodePtr old_node) {
            trash_.push_back(old_node);

            // Находим самую старую версию, которую сейчас кто-то читает
            uint64_t min_active_version = std::numeric_limits<uint64_t>::max();

            for (size_t i = 0; i < subs_num_; ++i) {
                uint64_t ver = subscribers_[i].load();
                if (ver == 0) continue;
                min_active_version = std::min(min_active_version, ver);
            }

            // Удаляем только те узлы, которые гарантированно никто не видит
            auto it = std::remove_if(trash_.begin(), trash_.end(),
                [min_active_version](NodePtr node) {
                    if (node->version < min_active_version) {
                        delete node;
                        return true;
                    }
                    return false;
                });

            trash_.erase(it, trash_.end());
        }
    };
}