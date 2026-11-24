#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

#include "Graph.hpp"

namespace kruskal {

    class Dsu {
    public:
        explicit Dsu(Vertex n) : parent_(n), rank_(n, 0), components_(n) {
            for (Vertex i = 0; i < n; ++i) {
                parent_[i] = i;
            }
        }

        template<bool Concurrent = false>
        [[nodiscard]] Vertex find(Vertex x) noexcept {
            Vertex* data = parent_.data();

            // Знаходимо корінь компоненти в якій дана вершина
            Vertex root = x;
            while (true) {
                const Vertex p = loadParent<Concurrent>(data, root);
                if (p == root) {
                    break;
                }
                root = p;
            }

            // Підвішуємо вершини на шляху до кореня компоненти (стиснення шляху)
            while (true) {
                const Vertex next = loadParent<Concurrent>(data, x);
                if (next == root) {
                    break;
                }
                storeParent<Concurrent>(data, x, root);
                x = next;
            }

            return root;
        }

       //  Повертає корінь без зміни батьківських посилань
        [[nodiscard]] Vertex findReadOnly(Vertex x) const noexcept {
            while (true) {
                const Vertex p = parent_[x];
                if (p == x) {
                    break;
                }
                x = p;
            }
            return x;
        }

        // Злиття компонент (викликається лише в послідовній фазі)
        bool unite(Vertex a, Vertex b) noexcept {
            a = find(a);
            b = find(b);

            if (a == b) {
                return false;
            }

            if (rank_[a] < rank_[b]) {
                std::swap(a, b);
            }

            parent_[b] = a;

            if (rank_[a] == rank_[b]) {
                ++rank_[a];
            }

            --components_;
            return true;
        }

        [[nodiscard]] Vertex componentCount() const noexcept {
            return components_;
        }

        [[nodiscard]] Vertex size() const noexcept {
            return static_cast<Vertex>(parent_.size());
        }

    private:
        std::vector<Vertex> parent_;
        std::vector<std::uint8_t> rank_;
        Vertex components_;

        template<bool Concurrent>
        [[nodiscard]] static Vertex loadParent(const Vertex* data, Vertex v) noexcept {
            if constexpr (Concurrent) {
                return std::atomic_ref<const Vertex>(data[v]).load(std::memory_order_relaxed);
            }
            else {
                return data[v];
            }
        }

        template<bool Concurrent>
        static void storeParent(Vertex* data, Vertex v, Vertex newParent) noexcept {
            if constexpr (Concurrent) {
                std::atomic_ref<Vertex>(data[v]).store(newParent, std::memory_order_relaxed);
            }
            else {
                data[v] = newParent;
            }
        }
    };

    //class dsu {
    //public:
    //    explicit dsu(vertex n): parent_(n), rank_(n, 0), components_(n)
    //    {
    //        std::iota(parent_.begin(), parent_.end(), vertex{0});
    //    }

        //// знайти корінь компоненти та стискає шлях до нього
        //[[nodiscard]] vertex find(vertex x) {
        //    assert(x < parent_.size());

        //    //  знаходимо корінь компоненти, в якій дана вершина
        //    vertex root = x;

        //    while (parent_[root] != root) {
        //        root = parent_[root];
        //    }

        //    // підвішуємо вершини на шляху до кореня компоненти (стиснення шляху)
        //    while (parent_[x] != root) {
        //        const vertex next = parent_[x];
        //        parent_[x] = root;
        //        x = next;
        //    }

        //    return root;
        //}

    //    // повертає корінь без зміни батьківських посилань
    //    [[nodiscard]] vertex findreadonly(vertex x) const {
    //        assert(x < parent_.size());

    //        while (parent_[x] != x) {
    //            x = parent_[x];
    //        }

    //        return x;
    //    }

    //    // злиття компонент
    //    bool unite(Vertex a, Vertex b) {
    //        a = find(a);
    //        b = find(b);

    //        if (a == b) {
    //            return false;
    //        }

    //        // Коренем a має бути корінь із більшим
    //        // або рівним рангом
    //        if (rank_[a] < rank_[b]) {
    //            std::swap(a, b);
    //        }

    //        parent_[b] = a;

    //        if (rank_[a] == rank_[b]) {
    //            ++rank_[a];
    //        }

    //        --components_;
    //        return true;
    //    }

    //    [[nodiscard]] Vertex componentCount() const {
    //        return components_;
    //    }

    //    [[nodiscard]] Vertex size() const {
    //        return static_cast<Vertex>(parent_.size());
    //    }

    //private:
    //    std::vector<Vertex> parent_;
    //    std::vector<std::uint8_t> rank_;
    //    Vertex components_;
    //};


} // namespace kruskal