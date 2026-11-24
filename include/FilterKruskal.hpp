#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include "Dsu.hpp"
#include "Graph.hpp"
#include "Kruskal.hpp"

namespace kruskal {


    inline constexpr std::size_t kDefaultFilterThreshold = 8192;

    struct FilterKruskalStats {
        std::size_t sortedEdges = 0;   // скільки ребер пройшло через sortRange
    };

    namespace detail {
        template<EdgeWeight W>
        [[nodiscard]] W medianOfThree(W a, W b, W c) {
            if (b < a) std::swap(a, b);   
            if (c < b) {
                b = (c < a) ? a : c;      
            }
            return b;
        }

        template<EdgeWeight W>
        [[nodiscard]] W choosePivot(std::span<const Edge<W>> edges) {
            constexpr std::size_t kMaxSample = 128;
            const std::size_t n = edges.size();

            const auto sqrtN = static_cast<std::size_t>(std::sqrt(static_cast<double>(n)));
            const std::size_t count = std::min(sqrtN, kMaxSample);
            const std::size_t step = n / count;

            std::array<W, kMaxSample> sample{};
            for (std::size_t i = 0; i < count; ++i) {
                sample[i] = edges[i * step].weight;
            }

            const auto middle = sample.begin() + static_cast<std::ptrdiff_t>(count / 2);
            std::nth_element(sample.begin(), middle, sample.begin() + static_cast<std::ptrdiff_t>(count));
            return *middle;
        }

        // Тришляхове розбиття Дейкстри на місці за один прохід
        template<EdgeWeight W>
        [[nodiscard]] std::pair<std::size_t, std::size_t> partition3(std::span<Edge<W>> edges, const W& pivot) {
            std::size_t lt = 0;
            std::size_t i = 0;
            std::size_t gt = edges.size();

            while (i < gt) {
                if (edges[i].weight < pivot) {
                    std::swap(edges[lt], edges[i]);
                    ++lt;
                    ++i;
                }
                else if (pivot < edges[i].weight) {
                    --gt;
                    std::swap(edges[i], edges[gt]);
                }
                else {
                    ++i;
                }
            }
            return { lt, gt };
        }

        // Фільтр
        template<bool Concurrent = false, EdgeWeight W>
        [[nodiscard]] std::size_t filterEdges(std::span<Edge<W>> edges, Dsu& dsu) {
            std::size_t kept = 0;
            for (std::size_t i = 0; i < edges.size(); ++i) {
                if (dsu.find<Concurrent>(edges[i].u) != dsu.find<Concurrent>(edges[i].v)) {
                    edges[kept++] = edges[i];
                }
            }
            return kept;
        }

        // Крок рекурсії
        template<EdgeWeight W>
        bool filterKruskalStep(std::span<Edge<W>> edges, std::size_t threshold, Dsu& dsu, 
            MstResult<W>& result, std::size_t& sortedEdges) {
            if (dsu.componentCount() <= 1) {
                return true;
            }
            if (edges.empty()) {
                return false;
            }

            // Базовий випадок
            if (edges.size() <= threshold) {
                sortedEdges += edges.size();
                sortRange(edges.begin(), edges.end());
                return uniteRange(dsu, edges.begin(), edges.end(), result);
            }

            // Вибір опорного елементу та розподілення ребер за опорою
            const W pivot = choosePivot<W>(edges);
            const auto [lightEnd, heavyBegin] = partition3(edges, pivot);

            // Рекурсія по легшій частині ребер
            if (filterKruskalStep(edges.first(lightEnd), threshold, dsu, result, sortedEdges)) {
                return true;
            }

            // Додавання до остову ребер з однаковами вагами
            const auto equal = edges.subspan(lightEnd, heavyBegin - lightEnd);
            if (uniteRange(dsu, equal.begin(), equal.end(), result)) {
                return true;
            }

            // Фільтрація частини важких ребер перед рекурсією по важкчій частині ребер
            const auto heavy = edges.subspan(heavyBegin);
            const std::size_t kept = filterEdges(heavy, dsu); // усі невідкинуті ребра находяться на початку
            return filterKruskalStep(heavy.first(kept), threshold, dsu, result, sortedEdges);
        }

    } // namespace detail

    template<EdgeWeight W>
    [[nodiscard]] MstResult<W> filterKruskalInPlace(Vertex vertexCount, std::span<Edge<W>> edges,
                                                    std::size_t threshold = kDefaultFilterThreshold,
                                                    FilterKruskalStats* stats = nullptr) {
        MstResult<W> result{};
        result.componentCount = vertexCount;
        if (stats) {
            *stats = {};
        }

        if (vertexCount == 0) {
            return result;
        }

        Dsu dsu(vertexCount);
        result.edges.reserve(static_cast<std::size_t>(vertexCount - 1));

        std::size_t sortedEdges = 0;
        detail::filterKruskalStep(edges, threshold, dsu, result, sortedEdges);

        result.componentCount = dsu.componentCount();
        if (stats) {
            stats->sortedEdges = sortedEdges;
        }
        return result;
    }

    template<EdgeWeight W>
    [[nodiscard]] MstResult<W> filterKruskal(Graph<W> graph, std::size_t threshold = kDefaultFilterThreshold,
                                             FilterKruskalStats* stats = nullptr) {
        return filterKruskalInPlace(graph.vertexCount, std::span<Edge<W>>(graph.edges), threshold, stats);
    }

} // namespace kruskal
