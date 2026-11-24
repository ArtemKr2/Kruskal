#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

#include "Dsu.hpp"
#include "Graph.hpp"

namespace kruskal {
    // різні допомжні функції для тестування

    // коректність графа: кожне ребро має обидва кінці < vertexCount і не є петлею.
    template<EdgeWeight W>
    [[nodiscard]] bool isValid(const Graph<W>& g) {
        for (const Edge<W>& e : g.edges) {
            if (e.u >= g.vertexCount || e.v >= g.vertexCount) return false;
            if (e.u == e.v) return false;
        }
        return true;
    }

    // перевірка на простий граф коректний (isValid) і без паралельних ребер.
    // Ребра {u, v} і {v, u} вважаються однаковими, ваги не враховуються.
    template<EdgeWeight W>
    [[nodiscard]] bool isSimple(const Graph<W>& g) {
        if (!isValid(g)) return false;

        
        std::vector<std::uint64_t> keys;
        keys.reserve(g.edges.size());
        for (const Edge<W>& e : g.edges) {
            const auto [a, b] = std::minmax(e.u, e.v);

            keys.push_back((std::uint64_t{a} << 32) | b);
        }
        std::sort(keys.begin(), keys.end());
        // Після сортування якщо є однакові ключі то вони поряд і тоді це не простий граф
        return std::adjacent_find(keys.begin(), keys.end()) == keys.end();
    }

    // Перевірка зв'язності,тобто усі вершини в одній компоненті
    // Передумова isValid(g) (перевіряється assert у Debug)
    // Граф з 0 вершин вважаємо зв'язним немає двох вершин, які б не з'єднувались
    template<EdgeWeight W>
    [[nodiscard]] bool isConnected(const Graph<W>& g) {
        assert(isValid(g));
        if (g.vertexCount <= 1) return true;

        Dsu dsu(g.vertexCount);
        for (const Edge<W>& e : g.edges) {
            dsu.unite(e.u, e.v);
            if (dsu.componentCount() == 1) return true;   
        }
        return false;
    }

} // namespace kruskal
