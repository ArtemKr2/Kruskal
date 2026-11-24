#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>
#include <limits>

#include "Graph.hpp"

namespace kruskal {

// Параметри розрідженого графа.
// Приклад: GraphParams<int>{ .vertexCount = 1000, .edgeCount = 4000,
//                                  .seed = 1, .minWeight = 1, .maxWeight = 1000000 }
template<EdgeWeight W>
struct GraphParams {
    Vertex vertexCount = 0;
    std::uint64_t edgeCount = 0;
    std::uint64_t seed = 0;
    W minWeight{};
    W maxWeight{};
};

// Параметри решітки rows x cols. Вершина (r, c) має номер r * cols + c.
template<EdgeWeight W>
struct GridGraphParams {
    Vertex rows = 0;
    Vertex cols = 0;
    std::uint64_t seed = 0;
    W minWeight{};
    W maxWeight{};
};

namespace detail {

    // Ребро запаковуєм як 64-бітне число: (менша вершина << 32) | більша.
    inline std::uint64_t edgeKey(Vertex a, Vertex b) {
        if (a > b) std::swap(a, b);
        return (std::uint64_t{a} << 32) | b;
    }

    inline Vertex keyFirst(std::uint64_t key)  { return static_cast<Vertex>(key >> 32); }
    inline Vertex keySecond(std::uint64_t key) { return static_cast<Vertex>(key & 0xFFFFFFFFu); }

    // Максимальна кількість ребер простого графа V*(V−1)/2
    // Для V < 2^32 добуток V*(V−1) не переповнить uint64_t
    inline std::uint64_t maxSimpleEdges(Vertex v) {
        return v < 2 ? 0 : std::uint64_t{v} * (v - 1) / 2;
    }

    // Рівномірний розподіл ваг: цілий для цілих типів, дійсний для дробових.
    template<EdgeWeight W>
    auto makeWeightDistribution(W lo, W hi) {
        if constexpr (std::is_integral_v<W>) {
            return std::uniform_int_distribution<W>(lo, hi);
        }
        else if constexpr (std::is_floating_point_v<W>) {
            return std::uniform_real_distribution<W>(lo, hi);
        }
        else {
            static_assert(std::is_arithmetic_v<W>, "Generator supports only arithmetic weights.");
        }
    }

    // Друге зерно для ваг, щоб ваги не залежали від того
    // скільки випадкових чисел пішло на топологію; зерно ваг утворюється через XOR із константою (магічним число)
    inline std::uint64_t weightSeed(std::uint64_t seed) {
        return seed ^ 0x9E3779B97F4A7C15ull;
    }

    // відсортовані ключі ребер випадкового кістякового дерева (V−1 ребро)
    // Вершини в випадковому порядку, кожна наступна приєднується до
    // випадкової вершини, яка вже в дереві.
    inline std::vector<std::uint64_t> randomTreeKeys(Vertex n, std::mt19937_64& rng) {
        std::vector<Vertex> order(n);
        for (Vertex i = 0; i < n; ++i) 
            order[i] = i;

        std::shuffle(order.begin(), order.end(), rng);

        std::vector<std::uint64_t> keys;
        if (n > 0) 
            keys.reserve(n - 1);

        for (Vertex i = 1; i < n; ++i) {
            std::uniform_int_distribution<Vertex> pickEarlier(0, i - 1);
            keys.push_back(edgeKey(order[i], order[pickEarlier(rng)]));
        }
        std::sort(keys.begin(), keys.end());   // ребра дерева різні, дублікатів немає
        return keys;
    }

    template<EdgeWeight W>
    Graph<W> buildGraph(Vertex n, std::vector<std::uint64_t>& keys, const GraphParams<W>& p, std::mt19937_64& rng) {
        std::shuffle(keys.begin(), keys.end(), rng);

        std::mt19937_64 weightRng(weightSeed(p.seed));
        auto weight = makeWeightDistribution(p.minWeight, p.maxWeight);

        Graph<W> graph;
        graph.vertexCount = n;
        graph.edges.reserve(keys.size());
        for (const std::uint64_t key : keys) {
            graph.edges.push_back({keyFirst(key), keySecond(key), weight(weightRng)});
        }
        return graph;
    }

} // namespace detail

// генератор зв'язних простих розріджених графів (без петель і дублікатів)
// рівно з vertexCount вершин і edgeCount ребер.
// Обмеження:
//   vertexCount == 0  та  edgeCount == 0
//   vertexCount >= 1  та  vertexCount − 1 <= edgeCount <= max(V − 1, V*(V−1)/4) (половина повного графа)
//   minWeight <= maxWeight
template<EdgeWeight W>
[[nodiscard]] Graph<W> generateSparseGraph(const GraphParams<W>& p) {
    const Vertex n = p.vertexCount;
    const std::uint64_t m = p.edgeCount;

    if (n == 0 && m != 0)
        throw std::invalid_argument("generateSparseGraph: no vertices but edgeCount > 0");
    if (n > 0 && m < std::uint64_t{n} - 1)
        throw std::invalid_argument("generateSparseGraph: edgeCount < vertexCount - 1, graph cannot be connected");
    // Дерево дозволене завжди 
    if (m > std::max<std::uint64_t>(n > 0 ? n - 1 : 0, detail::maxSimpleEdges(n) / 2))
        throw std::invalid_argument("generateSparseGraph: too many edges for a sparse graph");
    if (p.maxWeight < p.minWeight)
        throw std::invalid_argument("generateSparseGraph: minWeight > maxWeight");

    Graph<W> graph;
    graph.vertexCount = n;
    if (n == 0) return graph;

    std::mt19937_64 rng(p.seed);

    // Крок 1. Створити кістякове дерево (V − 1 ребро)
    // Вершини в випадковому порядку. кожна наступна приєднується
    // до випадкової вершини яка вже в дереві.

    std::vector<std::uint64_t> keys = detail::randomTreeKeys(n, rng);
    keys.reserve(static_cast<std::size_t>(m));   // місце під решту ребер

    // Крок 2. Додаткові випадкові ребра без дублікатів
    // Генерується кандидати пачками і відкидаються петлі та повтори всередині пачки
    // і ребра, які вже є в графі. Виконуємо доки ребер не буде m.
    std::uniform_int_distribution<Vertex> pickVertex(0, n - 1);
    std::uniform_int_distribution<Vertex> pickOther(0, n - 2);   // n >= 2, якщо ребра ще потрібні

    std::vector<std::uint64_t> candidates;
    std::vector<std::uint64_t> fresh;

    while (keys.size() < m) {
        const std::uint64_t need = m - keys.size();
        const std::uint64_t batch = need + need / 4 + 16;   // взяти із запасом на повтори

        candidates.clear();
        candidates.reserve(static_cast<std::size_t>(batch));
        for (std::uint64_t i = 0; i < batch; ++i) {
            const Vertex u = pickVertex(rng);
            Vertex v = pickOther(rng);
            if (v >= u) ++v;                 
            candidates.push_back(detail::edgeKey(u, v));
        }

        std::sort(candidates.begin(), candidates.end());
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end()); 

        // fresh = унікальні елементи яких немає в keys
        fresh.clear();
        std::set_difference(candidates.begin(), candidates.end(),
                            keys.begin(), keys.end(),
                            std::back_inserter(fresh));

        // беремо рівно need нових значень
        if (fresh.size() > need) {
            std::shuffle(fresh.begin(), fresh.end(), rng);
            fresh.resize(static_cast<std::size_t>(need));
            std::sort(fresh.begin(), fresh.end());
        }

        // Зливаємо два відсортовані масиви, keys лишається відсортованим.
        const auto middle = static_cast<std::ptrdiff_t>(keys.size());
        keys.insert(keys.end(), fresh.begin(), fresh.end());
        std::inplace_merge(keys.begin(), keys.begin() + middle, keys.end());
    }

    // Крок 3. Ребра у випадковому порядку 
    return detail::buildGraph(n, keys, p, rng);
}

// Генератор зв'язних простих графів із перебором усіх пар вершин.
// V == 0 та E == 0.
// V >= 1 та V - 1 <= E <= V*(V - 1)/2.
template<EdgeWeight W>
[[nodiscard]] Graph<W> generateDenseGraph(const GraphParams<W>& p) {
    const Vertex n = p.vertexCount;
    const std::uint64_t m = p.edgeCount;
    const std::uint64_t maxEdges = detail::maxSimpleEdges(n);

    if (n == 0 && m != 0) {
        throw std::invalid_argument("generateDenseGraph: no vertices but edgeCount > 0");
    }
    if (n > 0 && m < std::uint64_t{ n } - 1) {
        throw std::invalid_argument("generateDenseGraph: edgeCount < vertexCount - 1");
    }
    if (m > maxEdges) {
        throw std::invalid_argument("generateDenseGraph: more edges than in a complete graph");
    }
    if (p.maxWeight < p.minWeight) {
        throw std::invalid_argument("generateDenseGraph: minWeight > maxWeight");
    }

    if (n == 0) {
        return Graph<W>{};
    }

    std::vector<std::uint64_t> keys;

    if (m > keys.max_size()) {
        throw std::length_error(
            "generateDenseGraph: too many edges to store"
        );
    }

    keys.reserve(static_cast<std::size_t>(m));

    std::mt19937_64 rng(p.seed);

    // Крок 1. Відсортовані ключі кістякового дерева.
    const auto tree = detail::randomTreeKeys(n, rng);

    // Лічильники стосуються лише пар, які не належать дереву.
    std::uint64_t remaining = maxEdges - static_cast<std::uint64_t>(tree.size());

    std::uint64_t need = m - static_cast<std::uint64_t>(tree.size());

    // Індекс наступного ребра дерева.
    std::size_t t = 0;

    // Крок 2. Кожну можливу пару розглядаємо рівно один раз.
    for (Vertex a = 0; a < n - 1; ++a) {
        for (Vertex b = a + 1; b < n; ++b) {
            const auto key = detail::edgeKey(a, b);

            // Ребра дерева додаємо обов'язково.
            if (t < tree.size() && tree[t] == key) {
                keys.push_back(key);
                ++t;
                continue;
            }

            // Для решти пар — вибір з імовірністю need/remaining.
            std::uniform_int_distribution<std::uint64_t> pick(0, remaining - 1);

            if (pick(rng) < need) {
                keys.push_back(key);
                --need;
            }

            --remaining;
        }
    }

    // Кроки 3 і 4. Перемішування ребер та призначення ваг.
    return detail::buildGraph(n, keys, p, rng);
}

// Генератор решітки rows x cols. кожна вершина з'єднана з правим і нижнім сусідом.
// V = rows * cols, E = rows * (cols - 1) + cols * (rows - 1).
template<EdgeWeight W>
[[nodiscard]] Graph<W> generateGridGraph(const GridGraphParams<W>& p) {
    if (p.maxWeight < p.minWeight)
        throw std::invalid_argument("generateGridGraph: minWeight > maxWeight");

    const std::uint64_t vertices = std::uint64_t{p.rows} * p.cols;
    if (vertices > std::numeric_limits<Vertex>::max())
        throw std::invalid_argument("generateGridGraph: too many vertices for Vertex type");

    const Vertex n = static_cast<Vertex>(vertices);
    if (n == 0) return Graph<W>{};

    std::vector<std::uint64_t> keys;
    keys.reserve(static_cast<std::size_t>(
        std::uint64_t{ p.rows } *(p.cols - 1) + std::uint64_t{ p.cols } *(p.rows - 1)));

    for (Vertex r = 0; r < p.rows; ++r) {
        for (Vertex c = 0; c < p.cols; ++c) {
            const Vertex v = r * p.cols + c;
            if (c + 1 < p.cols) keys.push_back(detail::edgeKey(v, v + 1));        // праворуч
            if (r + 1 < p.rows) keys.push_back(detail::edgeKey(v, v + p.cols));   // вниз
        }
    }

    
    const GraphParams<W> common{.vertexCount = n, .edgeCount = keys.size(), .seed = p.seed,
                                 .minWeight = p.minWeight, .maxWeight = p.maxWeight};
    std::mt19937_64 rng(p.seed);
    return detail::buildGraph(n, keys, common, rng);
}

} // namespace kruskal
