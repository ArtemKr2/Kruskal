#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <type_traits>
#include <vector>

#include <numeric>
#include "Dsu.hpp"
#include "Graph.hpp"

namespace kruskal {

    // Тип для накопичення суми ваг
    template<EdgeWeight W>
    using WeightSum = std::conditional_t<std::integral<W>, std::int64_t, double>; // будь-які цілі типи стають int64_t, а дробові double.

    // Результат побудови мінімального остовного дерева або лісу.
    template<EdgeWeight W>
    struct MstResult {
        std::vector<Edge<W>> edges;   
        WeightSum<W> totalWeight{};   
        Vertex componentCount = 0;   
    };


    template<std::random_access_iterator It>
    void sortRange(It first, It last) {
        std::sort(first, last, ByWeight{});
    }

    // Сортування всього списку ребер
    template<EdgeWeight W>
    void sortEdges(std::vector<Edge<W>>& edges) {
        sortRange(edges.begin(), edges.end());
    }

    // Прохід по ребрах [first, last) із DSU
    // Спільний для звичайного Крускала і Filter-Kruskal.
    template<std::input_iterator It, EdgeWeight W>
    bool uniteRange(Dsu& dsu, It first, It last, MstResult<W>& result) {
        for (; first != last; ++first) {
            const Edge<W>& edge = *first;
            if (dsu.unite(edge.u, edge.v)) {
                result.edges.push_back(edge);
                result.totalWeight += static_cast<WeightSum<W>>(edge.weight);

                if (dsu.componentCount() == 1) {
                    return true;
                }
            }
        }
        return false;
    }

    // Прохід по відсортованих ребрах з об'єднанням компонент Dsu для визначення мінімального остову
    template<EdgeWeight W>
    [[nodiscard]] MstResult<W> kruskalFromSorted(Vertex vertexCount, std::span<const Edge<W>> sortedEdges) {
        MstResult<W> result;
        result.componentCount = vertexCount;

        // Порожній граф, немає ребер і компонент
        if (vertexCount == 0) {
            return result;
        }

        // Спочатку кожна вершина є окремою компонентою
        Dsu dsu(vertexCount);

        result.edges.reserve(static_cast<std::size_t>(vertexCount - 1)); 

        // Додаємо ребра, які з'єднують різні компоненти
        uniteRange(dsu, sortedEdges.begin(), sortedEdges.end(), result);

        result.componentCount = dsu.componentCount();
        return result;
    }

    template<EdgeWeight W>
    [[nodiscard]] MstResult<W> kruskalSequential(Graph<W> graph) {
        sortEdges(graph.edges);
        return kruskalFromSorted(graph.vertexCount, std::span<const Edge<W>>(graph.edges));
    }

} // namespace kruskal
