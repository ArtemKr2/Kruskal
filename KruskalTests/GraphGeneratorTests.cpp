#include "pch.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
#include <utility>

#include "Graph.hpp"
#include "GraphGenerator.hpp"
#include "GraphUtils.hpp"

namespace {

    using kruskal::Graph;
    using kruskal::GraphParams;
    using kruskal::Vertex;
    using kruskal::GridGraphParams;
    using kruskal::generateSparseGraph;
    using kruskal::generateDenseGraph;
    using kruskal::generateGridGraph;

    GraphParams<int> intParams(Vertex n, std::uint64_t m, std::uint64_t seed) {
        return { .vertexCount = n, .edgeCount = m, .seed = seed, .minWeight = 1, .maxWeight = 1000 };
    }

    // Головна перевірка: рівно V вершин, рівно E ребер, простий, зв'язний
    template<kruskal::EdgeWeight W>
    void expectGoodGraph(const Graph<W>& g, Vertex n, std::uint64_t m) {
        EXPECT_EQ(g.vertexCount, n);
        EXPECT_EQ(g.edges.size(), m);
        EXPECT_TRUE(kruskal::isSimple(g));
        EXPECT_TRUE(kruskal::isConnected(g));
    }

    // --- Тестування генератора розріджених графів ---
    
    // 1. Різні розміри й густини, кілька seed
    TEST(GraphGeneratorTests, ProducesSimpleConnectedGraphWithExactSizes) {
        struct Case { Vertex n; std::uint64_t m; };
        const Case cases[] = {
            {1, 0}, {2, 1}, {3, 2}, {5, 4}, {5, 5},
            {10, 9}, {10, 15}, {10, 22},           // 22 = 45 / 2, межа розрідженого
            {100, 99}, {100, 400}, {100, 800},
            {1000, 4000}, {1000, 8000},
        };
        for (const auto& c : cases) {
            for (std::uint64_t seed = 0; seed < 20; ++seed) {
                SCOPED_TRACE("n = " + std::to_string(c.n) + ", m = " + std::to_string(c.m) +
                             ", seed = " + std::to_string(seed));
                expectGoodGraph(generateSparseGraph(intParams(c.n, c.m, seed)), c.n, c.m);
            }
        }
    }

    // 2. Порожній граф
    TEST(GraphGeneratorTests, EmptyGraph) {
        const auto g = generateSparseGraph(intParams(0, 0, 1));
        EXPECT_EQ(g.vertexCount, 0u);
        EXPECT_TRUE(g.edges.empty());
    }

    // 3. E = V − 1: має вийти дерево.
    TEST(GraphGeneratorTests, MinimalEdgeCountGivesTree) {
        const auto g = generateSparseGraph(intParams(500, 499, 7));
        expectGoodGraph(g, 500, 499);
    }

    // 4. Цілі ваги в заданих межах, і межі справді досяжні
    TEST(GraphGeneratorTests, IntWeightsInRange) {
        const auto g = generateSparseGraph(GraphParams<int>{
            .vertexCount = 1000, .edgeCount = 5000, .seed = 3, .minWeight = -5, .maxWeight = 5 });

        bool sawMin = false, sawMax = false;
        for (const auto& e : g.edges) {
            EXPECT_GE(e.weight, -5);
            EXPECT_LE(e.weight, 5);
            sawMin = sawMin || e.weight == -5;
            sawMax = sawMax || e.weight == 5;
        }
        EXPECT_TRUE(sawMin);
        EXPECT_TRUE(sawMax);
    }

    // 5. Дробові ваги в межах
    TEST(GraphGeneratorTests, DoubleWeightsInRange) {
        const auto g = generateSparseGraph(GraphParams<double>{
            .vertexCount = 1000, .edgeCount = 5000, .seed = 4, .minWeight = 0.0, .maxWeight = 1.0 });

        expectGoodGraph(g, 1000, 5000);
        for (const auto& e : g.edges) {
            EXPECT_GE(e.weight, 0.0);
            EXPECT_LE(e.weight, 1.0);
        }
    }

    // 6. Однаковий seed дає однаковий граф
    TEST(GraphGeneratorTests, SameSeedGivesSameGraph) {
        const auto a = generateSparseGraph(intParams(1000, 4000, 42));
        const auto b = generateSparseGraph(intParams(1000, 4000, 42));
        EXPECT_EQ(a.vertexCount, b.vertexCount);
        EXPECT_EQ(a.edges, b.edges);
    }

    // 7. Різний seed дає інший граф
    TEST(GraphGeneratorTests, DifferentSeedGivesDifferentGraph) {
        const auto a = generateSparseGraph(intParams(1000, 4000, 1));
        const auto b = generateSparseGraph(intParams(1000, 4000, 2));
        EXPECT_NE(a.edges, b.edges);
    }

    // 8. Ребра перемішані: не відсортовані за вершинами,
    //    і перші V − 1 з них не утворюють дерево (дерево не стоїть на початку).
    TEST(GraphGeneratorTests, EdgesAreShuffled) {
        const Vertex n = 1000;
        const auto full = generateSparseGraph(intParams(n, 4000, 5));

        // Перевірка 1: ребра не відсортовані за (u, v).
        const bool sorted = std::is_sorted(full.edges.begin(), full.edges.end(),
            [](const auto& a, const auto& b) { return std::pair{a.u, a.v} < std::pair{b.u, b.v}; });
        EXPECT_FALSE(sorted);

        // Перевірка 2: перші V − 1 ребер не з'єднують граф.
        auto prefix = full;
        prefix.edges.resize(n - 1);
        EXPECT_FALSE(kruskal::isConnected(prefix));
    }

    // 9. Неправильні параметри.
    TEST(GraphGeneratorTests, RejectsInvalidParams) {
        // Немає вершин, але є ребра.
        EXPECT_THROW((void)generateSparseGraph(intParams(0, 1, 0)), std::invalid_argument);
        // Замало ребер для зв'язності.
        EXPECT_THROW((void)generateSparseGraph(intParams(10, 8, 0)), std::invalid_argument);
        // Забагато ребер для розрідженого графа: 10 вершин, межа 22.
        EXPECT_THROW((void)generateSparseGraph(intParams(10, 23, 0)), std::invalid_argument);
        // Одна вершина, але є ребро.
        EXPECT_THROW((void)generateSparseGraph(intParams(1, 1, 0)), std::invalid_argument);
        // minWeight > maxWeight.
        EXPECT_THROW((void)generateSparseGraph(GraphParams<int>{
            .vertexCount = 5, .edgeCount = 4, .seed = 0, .minWeight = 10, .maxWeight = 1 }),
            std::invalid_argument);
    }

    // 10. Великий граф: E/V = 8.
    TEST(GraphGeneratorTests, LargeGraph) {
        const Vertex n = 100000;
        const std::uint64_t m = 800000;
        expectGoodGraph(generateSparseGraph(intParams(n, m, 11)), n, m);
    }

    // --- Тестування генератора плотних графів ---

    // 1. Різні розміри, щільності та seed.
    TEST(DenseGraphGeneratorTests, ProducesSimpleConnectedGraphWithExactSizes) {
        struct Case {
            Vertex n;
            std::uint64_t m;
        };

        const Case cases[] = {
            {1, 0},
            {2, 1},
            {3, 2},
            {3, 3},
            {5, 4},
            {5, 10},
            {10, 30},
            {10, 44},
            {10, 45},
            {100, 99},
            {100, 2500},
            {100, 4950}
        };

        for (const auto& c : cases) {
            for (std::uint64_t seed = 0; seed < 20; ++seed) {
                SCOPED_TRACE(
                    ::testing::Message()
                    << "n=" << c.n
                    << ", m=" << c.m
                    << ", seed=" << seed
                );

                const auto graph = generateDenseGraph(intParams(c.n, c.m, seed));

                expectGoodGraph(graph, c.n, c.m);
            }
        }
    }

    // 2. Порожній граф.
    TEST(DenseGraphGeneratorTests, EmptyGraph) {
        const auto graph = generateDenseGraph(intParams(0, 0, 1));

        EXPECT_EQ(graph.vertexCount, 0u);
        EXPECT_TRUE(graph.edges.empty());
    }

    // 3. Повний граф: кожна вершина має ступінь V - 1.
    TEST(DenseGraphGeneratorTests, CompleteGraphContainsEveryPair) {
        constexpr Vertex n = 100;
        constexpr std::uint64_t m = 4950;

        const auto graph = generateDenseGraph(intParams(n, m, 7));

        expectGoodGraph(graph, n, m);

        std::vector<Vertex> degree(n, 0);

        for (const auto& edge : graph.edges) {
            // Перевіряємо індекси перед зверненням до degree.
            ASSERT_LT(edge.u, n);
            ASSERT_LT(edge.v, n);

            ++degree[edge.u];
            ++degree[edge.v];
        }

        for (Vertex v = 0; v < n; ++v) {
            EXPECT_EQ(degree[v], n - 1) << "vertex=" << v;
        }
    }

    // 4. Мінімальна кількість ребер дає дерево.
    TEST(DenseGraphGeneratorTests, MinimalEdgeCountGivesTree) {
        const auto graph = generateDenseGraph(intParams(500, 499, 7));

        expectGoodGraph(graph, 500, 499);
    }

    // 5. Однаковий seed відтворює весь граф.
    TEST(DenseGraphGeneratorTests, SameSeedGivesSameGraph) {
        const auto a = generateDenseGraph(intParams(100, 3500, 42));

        const auto b = generateDenseGraph(intParams(100, 3500, 42));

        EXPECT_EQ(a.vertexCount, b.vertexCount);
        EXPECT_EQ(a.edges, b.edges);
    }

    // 6. Для вибраних різних seed графи відрізняються.
    TEST(DenseGraphGeneratorTests, DifferentSeedGivesDifferentGraph) {
        const auto a = generateDenseGraph(intParams(100, 3500, 1));

        const auto b = generateDenseGraph(intParams(100, 3500, 2));

        EXPECT_NE(a.edges, b.edges);
    }

    // 7. Цілі ваги лежать у межах; обидві межі зустрічаються.
    TEST(DenseGraphGeneratorTests, IntWeightsInRange) {
        const auto graph = generateDenseGraph(GraphParams<int>{.vertexCount = 200, .edgeCount = 15000,
            .seed = 3, .minWeight = -5, .maxWeight = 5
        });

        expectGoodGraph(graph, 200, 15000);

        bool sawMin = false;
        bool sawMax = false;

        for (const auto& edge : graph.edges) {
            EXPECT_GE(edge.weight, -5);
            EXPECT_LE(edge.weight, 5);

            sawMin = sawMin || edge.weight == -5;
            sawMax = sawMax || edge.weight == 5;
        }

        EXPECT_TRUE(sawMin);
        EXPECT_TRUE(sawMax);
    }

    // 8. Від'ємні та дробові ваги лежать у заданих межах.
    TEST(DenseGraphGeneratorTests, DoubleWeightsInRange) {
        const auto graph = generateDenseGraph(GraphParams<double>{.vertexCount = 200, .edgeCount = 15000,
            .seed = 4, .minWeight = -2.5, .maxWeight = 7.25
        });

        expectGoodGraph(graph, 200, 15000);

        for (const auto& edge : graph.edges) {
            EXPECT_GE(edge.weight, -2.5);
            EXPECT_LE(edge.weight, 7.25);
        }
    }

    // 9. Однакові межі ваг дозволені.
    TEST(DenseGraphGeneratorTests, EqualWeightBoundsGiveConstantWeights) {
        const auto integerGraph = generateDenseGraph(GraphParams<int>{.vertexCount = 20,
            .edgeCount = 100, .seed = 1, .minWeight = -7, .maxWeight = -7
        });

        expectGoodGraph(integerGraph, 20, 100);

        for (const auto& edge : integerGraph.edges) {
            EXPECT_EQ(edge.weight, -7);
        }

        const auto doubleGraph = generateDenseGraph(GraphParams<double>{.vertexCount = 20, .edgeCount = 100, 
            .seed = 1, .minWeight = 7.25, .maxWeight = 7.25
        });

        expectGoodGraph(doubleGraph, 20, 100);

        for (const auto& edge : doubleGraph.edges) {
            EXPECT_DOUBLE_EQ(edge.weight, 7.25);
        }
    }

    // 10. Ребра перемішані, дерево не залишене на початку.
    TEST(DenseGraphGeneratorTests, EdgesAreShuffled) {
        constexpr Vertex n = 200;

        const auto graph =
            generateDenseGraph(intParams(n, 15000, 5));

        const bool sorted = std::is_sorted(
            graph.edges.begin(),
            graph.edges.end(),
            [](const auto& a, const auto& b) {
                return std::pair{ a.u, a.v } < std::pair{ b.u, b.v };
            }
        );

        EXPECT_FALSE(sorted);

        auto prefix = graph;
        prefix.edges.resize(n - 1);

        // Для цього сценарію перші V - 1 ребер
        // не повинні утворювати зв'язний граф.
        EXPECT_FALSE(kruskal::isConnected(prefix));
    }

    // 11. Некоректні параметри.
    TEST(DenseGraphGeneratorTests, RejectsInvalidParams) {
        // Немає вершин, але є ребро.
        EXPECT_THROW(
            static_cast<void>(generateDenseGraph(intParams(0, 1, 0))),
            std::invalid_argument
        );

        // Замало ребер для зв'язності.
        EXPECT_THROW(
            static_cast<void>(generateDenseGraph(intParams(10, 8, 0))),
            std::invalid_argument
        );

        // У повному графі на 10 вершинах лише 45 ребер.
        EXPECT_THROW(
            static_cast<void>(generateDenseGraph(intParams(10, 46, 0))),
            std::invalid_argument
        );

        // Одна вершина не може мати ребра без петлі.
        EXPECT_THROW(
            static_cast<void>(generateDenseGraph(intParams(1, 1, 0))),
            std::invalid_argument
        );

        // Неправильні межі ваг.
        EXPECT_THROW(
            static_cast<void>(generateDenseGraph(GraphParams<int>{ .vertexCount = 5, .edgeCount = 8,
                    .seed = 0, .minWeight = 10, .maxWeight = 1})
                ), 
            std::invalid_argument
        );
    }

    // 12. Великий граф 
    TEST(DenseGraphGeneratorTests, LargeGraph) {
        constexpr Vertex n = 1000;
        constexpr std::uint64_t m = 375000;

        const auto graph = generateDenseGraph(intParams(n, m, 11));

        expectGoodGraph(graph, n, m);
    }

    // --- Тестування генератора решітки ---


    GridGraphParams<int> gridParams(Vertex rows, Vertex cols, std::uint64_t seed) {
        return { .rows = rows, .cols = cols, .seed = seed, .minWeight = 1, .maxWeight = 1000 };
    }

    // 1. Розміри: V = rows * cols, E = rows * (cols - 1) + cols * (rows - 1); простий і зв'язний.
    TEST(GridGraphGeneratorTests, HasExpectedSizes) {
        struct Case { Vertex rows, cols; };
        const Case cases[] = { {1, 1}, {1, 2}, {1, 10}, {10, 1}, {2, 2}, {3, 5}, {30, 40}, {100, 100} };

        for (const auto& c : cases) {
            SCOPED_TRACE("rows=" + std::to_string(c.rows) + ", cols=" + std::to_string(c.cols));
            const std::uint64_t edges = std::uint64_t{c.rows} *(c.cols - 1) + std::uint64_t{c.cols} * (c.rows - 1);
            expectGoodGraph(generateGridGraph(gridParams(c.rows, c.cols, 1)), c.rows * c.cols, edges);
        }
    }

    // 2. Кожне ребро з'єднує сусідів: праворуч (в одному рядку) або вниз (різниця cols).
    TEST(GridGraphGeneratorTests, EdgesConnectOnlyNeighbours) {
        constexpr Vertex rows = 20;
        constexpr Vertex cols = 30;
        const auto graph = generateGridGraph(gridParams(rows, cols, 2));

        for (const auto& e : graph.edges) {
            const Vertex lo = std::min(e.u, e.v);
            const Vertex hi = std::max(e.u, e.v);
            const bool right = hi - lo == 1 && lo / cols == hi / cols;
            const bool down = hi - lo == cols;
            EXPECT_TRUE(right || down) << "edge " << e.u << " - " << e.v;
        }
    }

    // 3. Порожня решітка, відтворюваність за seed, некоректні параметри.
    TEST(GridGraphGeneratorTests, EmptySameSeedAndInvalidParams) {
        EXPECT_EQ(generateGridGraph(gridParams(0, 5, 1)).vertexCount, 0u);
        EXPECT_TRUE(generateGridGraph(gridParams(5, 0, 1)).edges.empty());

        EXPECT_EQ(generateGridGraph(gridParams(20, 20, 7)).edges, generateGridGraph(gridParams(20, 20, 7)).edges);
        EXPECT_NE(generateGridGraph(gridParams(20, 20, 7)).edges, generateGridGraph(gridParams(20, 20, 8)).edges);

        // Забагато вершин для Vertex (100000 * 100000 > 2^32).
        EXPECT_THROW((void)generateGridGraph(gridParams(100000, 100000, 1)), std::invalid_argument);
        // minWeight > maxWeight.
        EXPECT_THROW((void)generateGridGraph(GridGraphParams<int>{.rows = 3, .cols = 3, .seed = 0,
            .minWeight = 10, .maxWeight = 1 }), std::invalid_argument);
    }

} // namespace
