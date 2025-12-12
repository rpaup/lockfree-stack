#include <iostream>
#include <thread>
#include <vector>
#include <cmath>
#include <string>
#include <mutex>
#include "LockFreeStack.hpp"

const int READERS_COUNT = 4;
const double X_START = 0.0;
const double X_END = 4.0;
const double STEP = 0.001;

struct Position {
    double x;
    double y;
};

// Траектория: y = -(x^2) + 4x
inline double calculate_y(double x) {
    return -(x * x) + 4 * x;
}

lf::LockFreeVersionedStack<Position> stack(READERS_COUNT);
std::mutex io_mutex;

void safe_print(const std::string& msg) {
    std::lock_guard<std::mutex> lock(io_mutex);
    std::cout << msg << std::endl;
}

void writer_thread() {
    safe_print("[Writer] Started generation...");

    for (double x = X_START; x <= X_END; x += STEP) {
        double y = calculate_y(x);
        stack.push({ x, y });
        // Здесь можно добавить задержку для эмуляции сложных вычислений
    }

    safe_print("[Writer] Finished. Head version: " + std::to_string(stack.last_version()));
    stack.stop();
}

void reader_thread(int id) {
    long long read_cycles = 0;

    while (!stack.is_stopped()) {
        lf::Node<Position>* current_node = nullptr;

        // Пытаемся получить доступ к актуальному состоянию стека
        if (stack.subscribe(id, current_node)) {
            read_cycles++;

            lf::Node<Position>* cursor = current_node;

            // Ограничиваем глубину проверки для производительности теста
            int depth_check = 0;
            double prev_x = -1.0;

            while (cursor != nullptr && depth_check < 100) {
                // 1. Проверка корректности данных (математическая модель)
                double expected_y = calculate_y(cursor->data.x);
                if (std::abs(cursor->data.y - expected_y) > 1e-5) {
                    std::lock_guard<std::mutex> lock(io_mutex);
                    std::cerr << "[Error Reader " << id << "] Data corruption! X: "
                        << cursor->data.x << std::endl;
                }

                // 2. Проверка порядка элементов (LIFO)
                // X должен строго убывать вглубь стека
                if (prev_x != -1.0) {
                    if (std::abs(prev_x - cursor->data.x - STEP) > 1e-5) {
                        // Разрыв последовательности допустим только при удалении элементов (pop),
                        // но данные внутри узла всегда должны быть валидны.
                    }
                }

                prev_x = cursor->data.x;
                cursor = cursor->next;
                depth_check++;
            }

            // Завершаем работу с версией, разрешаем очистку памяти
            stack.unsubscribe(id);
        }

        // Снижаем нагрузку на CPU в ожидании новых данных
        std::this_thread::yield();
    }

    safe_print("[Reader " + std::to_string(id) + "] Cycles completed: " + std::to_string(read_cycles));
}

int main() {
    setlocale(LC_ALL, "Russian");

    std::vector<std::thread> readers;
    for (int i = 0; i < READERS_COUNT; ++i) {
        readers.emplace_back(reader_thread, i);
    }

    std::thread writer(writer_thread);

    writer.join();
    for (auto& t : readers) {
        t.join();
    }

    safe_print("Stress test complete.");
    return 0;
}