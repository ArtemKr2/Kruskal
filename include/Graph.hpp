#pragma once

#include <concepts>
#include <cstdint>
#include <vector>

namespace kruskal {

    // вершини нумеруються від 0 до vertexCount-1 і є індексами в масивах Dsu
    using Vertex = std::uint32_t;

    // обмеження типу (тільки ціле чи дійсне число) ваги ребра через концепт
    template<typename W> 
    concept EdgeWeight = (std::integral<W> || std::floating_point<W>) && !std::same_as<W, bool>;

    template<EdgeWeight W>
    struct Edge {
        Vertex u = 0;
        Vertex v = 0;
        W weight{};

        // оператор порівняння за замовчуванням
        friend bool operator==(const Edge&, const Edge&) = default;
    };

    // граф
    template<EdgeWeight W>
    struct Graph {
        Vertex vertexCount = 0;
        std::vector<Edge<W>> edges;
    };

    // компаратор
    struct ByWeight {
        template<EdgeWeight W>
        constexpr bool operator()(const Edge<W>& a, const Edge<W>& b) const {
            return a.weight < b.weight;
        }
    };

} // namespace kruskal