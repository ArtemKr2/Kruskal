#include "pch.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <random>
#include <span>
#include <stdexcept>
#include <tuple>
#include <vector>

#include "Graph.hpp"
#include "GraphGenerator.hpp"
#include "Kruskal.hpp"
#include "KruskalParallel.hpp"

namespace {

    using kruskal::Edge;
    using kruskal::Graph;
    using kruskal::GraphParams;
    using kruskal::Vertex;

    const unsigned kThreadCounts[] = { 1, 2, 3, 4, 7, 12, 16 };

    // Допоміжні функції

    std::vector<Edge<int>> randomEdges(std::size_t n, std::uint32_t seed, int maxWeight = 49) {
        std::mt19937 rng(seed);
        std::uniform_int_distribution<Vertex> vertex(0, 999);
        std::uniform_int_distribution<int> weight(0, maxWeight);
        std::vector<Edge<int>> edges(n);
        for (auto& e : edges) {
            e = {vertex(rng), vertex(rng), weight(rng)};
        }
        return edges;
    }

    // компаратор за вагою та ребрами щоб однозначно впорядкувати ребра в тестах.
    template<kruskal::EdgeWeight W>
    bool fullKeyLess(const Edge<W>& a, const Edge<W>& b) {
        return std::tie(a.weight, a.u, a.v) < std::tie(b.weight, b.u, b.v);
    }


    template<kruskal::EdgeWeight W>
    bool sameEdges(std::vector<Edge<W>> a, std::vector<Edge<W>> b) {
        std::sort(a.begin(), a.end(), fullKeyLess<W>);
        std::sort(b.begin(), b.end(), fullKeyLess<W>);
        return a == b;
    }

    // Перевірка результату паралельного сортування відсортовано за вагою і ті самі ребра
    void expectSortedPermutation(const std::vector<Edge<int>>& sorted, const std::vector<Edge<int>>& original) {
        EXPECT_TRUE(std::is_sorted(sorted.begin(), sorted.end(), kruskal::ByWeight{}));
        EXPECT_TRUE(sameEdges(sorted, original));
    }
     
    // Паралельне сортування з буфером. Повертає копію результату (він може лежати і в edges, і в buffer).
    std::vector<Edge<int>> sortWithBuffer(std::vector<Edge<int>> edges, unsigned p) {
        std::vector<Edge<int>> buffer(edges.size());
        const auto sorted = kruskal::parallelSort(std::span<Edge<int>>(edges), std::span<Edge<int>>(buffer), p);
        return { sorted.begin(), sorted.end() };
    }

    Graph<int> exampleGraph10() {
        return Graph<int>{ 10, {
            {0, 1, 1}, {0, 2, 3}, {0, 3, 7}, {1, 3, 7}, {1, 5, 3}, {2, 3, 8},
            {2, 4, 3}, {3, 4, 9}, {3, 5, 2}, {4, 6, 6}, {4, 9, 5}, {5, 6, 4},
            {5, 7, 5}, {6, 7, 9}, {6, 8, 2}, {6, 9, 10}, {7, 8, 8}, {8, 9, 2} } };
    }

    
    // 1. Межі кусків
    TEST(KruskalParallelTests, ChunkBoundsExample) {
        const std::vector<std::size_t> expected{ 0, 3, 6, 10 };
        EXPECT_EQ(kruskal::detail::chunkBounds(10, 3), expected);
    }

    // 2. перевірка chunkBounds на корекнтість
    TEST(KruskalParallelTests, ChunkBoundsCoverArrayEvenly) {
        const std::size_t sizes[] = {0, 1, 2, 5, 10, 11, 100, 1001};
        for (const std::size_t n : sizes) {
            for (const unsigned p : kThreadCounts) {
                SCOPED_TRACE(::testing::Message() << "n=" << n << ", p=" << p);
                const auto b = kruskal::detail::chunkBounds(n, p);

                ASSERT_EQ(b.size(), static_cast<std::size_t>(p) + 1);
                EXPECT_EQ(b.front(), 0u);
                EXPECT_EQ(b.back(), n);

                std::size_t minSize = n, maxSize = 0;
                for (unsigned i = 0; i < p; ++i) {
                    ASSERT_LE(b[i], b[i + 1]);
                    minSize = std::min(minSize, b[i + 1] - b[i]);
                    maxSize = std::max(maxSize, b[i + 1] - b[i]);
                }
                EXPECT_LE(maxSize - minSize, 1u);
            }
        }
    }
    
    // 3–4. Паралельне сортування цілком

    // сітка розмірів (n < p) і кількостей потоків.
    TEST(KruskalParallelTests, SortA1RandomData) {
        const std::size_t sizes[] = {0, 1, 2, 5, 100, 100003};
        for (const std::size_t n : sizes) {
            const auto original = randomEdges(n, static_cast<std::uint32_t>(n));
            for (const unsigned p : kThreadCounts) {
                SCOPED_TRACE(::testing::Message() << "n=" << n << ", p=" << p);
                expectSortedPermutation(sortWithBuffer(original, p), original);
            }
        }
    }

    TEST(KruskalParallelTests, SortA1SpecialData) {
        const std::size_t n = 10'007;   // не ділиться на жодне p з тесту

        auto sortedInput = randomEdges(n, 1);
        std::sort(sortedInput.begin(), sortedInput.end(), fullKeyLess<int>);

        auto reversedInput = sortedInput;
        std::reverse(reversedInput.begin(), reversedInput.end());

        const auto allEqual = randomEdges(n, 2, 0);    // усі ваги 0
        const auto fewValues = randomEdges(n, 3, 2);   // ваги лише 0, 1, 2

        const std::vector<Edge<int>>* inputs[] = { &sortedInput, &reversedInput, &allEqual, &fewValues };
        const char* names[] = { "sorted", "reversed", "all equal", "few values" };

        for (std::size_t k = 0; k < 4; ++k) {
            for (const unsigned p : kThreadCounts) {
                SCOPED_TRACE(::testing::Message() << names[k] << ", p=" << p);
                expectSortedPermutation(sortWithBuffer(*inputs[k], p), *inputs[k]);
            }
        }
    }

    
    // 5–6. Порівняння паралельного із послідовним
    
    // збігаються сума ваг, компоненти й кількість ребер.
    TEST(KruskalParallelTests, A1MatchesSequentialOnRandomGraphs) {
        for (std::uint64_t seed = 0; seed < 50; ++seed) {
            const auto graph = kruskal::generateSparseGraph(GraphParams<int>{
                .vertexCount = 300, .edgeCount = 1500, .seed = seed, .minWeight = 1, .maxWeight = 10 });
            const auto expected = kruskal::kruskalSequential(graph);

            for (const unsigned p : kThreadCounts) {
                SCOPED_TRACE(::testing::Message() << "seed=" << seed << ", p=" << p);
                const auto result = kruskal::kruskalParallel(graph, p);
                EXPECT_EQ(result.totalWeight, expected.totalWeight);
                EXPECT_EQ(result.componentCount, expected.componentCount);
                EXPECT_EQ(result.edges.size(), expected.edges.size());
            }
        }
    }

    // Усі ваги різні, тож набір ребер має збігтися точно.
    TEST(KruskalParallelTests, A1SameTreeWhenWeightsDistinct) {
        for (std::uint64_t seed = 0; seed < 20; ++seed) {
            auto graph = kruskal::generateSparseGraph(GraphParams<int>{
                .vertexCount = 300, .edgeCount = 1500, .seed = seed, .minWeight = 1, .maxWeight = 10 });

            // Ваги 1..E у випадковому порядку гарантовано різні.
            std::vector<int> weights(graph.edges.size());
            std::iota(weights.begin(), weights.end(), 1);
            std::shuffle(weights.begin(), weights.end(), std::mt19937(static_cast<std::uint32_t>(seed)));
            for (std::size_t i = 0; i < weights.size(); ++i) graph.edges[i].weight = weights[i];

            const auto expected = kruskal::kruskalSequential(graph);
            for (const unsigned p : kThreadCounts) {
                SCOPED_TRACE(::testing::Message() << "seed=" << seed << ", p=" << p);
                const auto result = kruskal::kruskalParallel(graph, p);
                EXPECT_TRUE(sameEdges(result.edges, expected.edges));
            }
        }
    }

    
    // 7. Крайні й малі графи
    TEST(KruskalParallelTests, A1ExampleGraphHasWeight25) {
        for (const unsigned p : kThreadCounts) {
            SCOPED_TRACE(::testing::Message() << "p=" << p);
            const auto result = kruskal::kruskalParallel(exampleGraph10(), p);
            EXPECT_EQ(result.totalWeight, 25);
            EXPECT_EQ(result.componentCount, 1u);
            EXPECT_EQ(result.edges.size(), 9u);
        }
    }

    TEST(KruskalParallelTests, A1EmptyAndSingleVertexGraphs) {
        for (const unsigned p : kThreadCounts) {
            SCOPED_TRACE(::testing::Message() << "p=" << p);

            const auto empty = kruskal::kruskalParallel(Graph<int>{}, p);
            EXPECT_TRUE(empty.edges.empty());
            EXPECT_EQ(empty.componentCount, 0u);

            const auto single = kruskal::kruskalParallel(Graph<int>{ 1, {} }, p);
            EXPECT_TRUE(single.edges.empty());
            EXPECT_EQ(single.componentCount, 1u);
        }
    }

    TEST(KruskalParallelTests, A1DisconnectedGraphProducesForest) {
        const Graph<int> graph{ 6, { {0, 1, 1}, {1, 2, 2}, {0, 2, 10}, {3, 4, 4} } };
        for (const unsigned p : kThreadCounts) {
            SCOPED_TRACE(::testing::Message() << "p=" << p);
            const auto result = kruskal::kruskalParallel(graph, p);
            EXPECT_EQ(result.componentCount, 3u);
            EXPECT_EQ(result.totalWeight, 7);
        }
    }

    TEST(KruskalParallelTests, A1HandlesNegativeAndFractionalWeights) {
        const Graph<double> graph{ 3, {{0, 1, -2.5}, {1, 2, 0.75}, {0, 2, 4.0}} };
        for (const unsigned p : kThreadCounts) {
            SCOPED_TRACE(::testing::Message() << "p=" << p);
            const auto result = kruskal::kruskalParallel(graph, p);
            EXPECT_DOUBLE_EQ(result.totalWeight, -1.75);
            EXPECT_EQ(result.componentCount, 1u);
        }
    }

    TEST(KruskalParallelTests, A1AccumulatesIntegerWeightsIn64Bits) {
        const Graph<int> graph{ 3, { {0, 1, 2000000000}, {1, 2, 2000000000} } };
        for (const unsigned p : kThreadCounts) {
            const auto result = kruskal::kruskalParallel(graph, p);
            EXPECT_EQ(result.totalWeight, std::int64_t{4000000000});
        }
    }

    
    // 8. Перевірка коли потоків більше ніж ядер
    TEST(KruskalParallelTests, A1StressRepeated) {
        const auto graph = kruskal::generateSparseGraph(GraphParams<int>{
            .vertexCount = 4000, .edgeCount = 50000, .seed = 77, .minWeight = 1, .maxWeight = 10 });
        const auto expected = kruskal::kruskalSequential(graph);

        for (int repeat = 0; repeat < 20; ++repeat) {
            SCOPED_TRACE(::testing::Message() << "repeat=" << repeat);
            const auto result = kruskal::kruskalParallel(graph, 16);   // потоків більше, ніж ядер
            ASSERT_EQ(result.totalWeight, expected.totalWeight);
            ASSERT_EQ(result.componentCount, expected.componentCount);
        }
    }

    
    // 9. Неправильна кількість потоків
    

    TEST(KruskalParallelTests, A1RejectsZeroThreads) {
        std::vector<Edge<int>> edges{ {0, 1, 1} };
        std::vector<Edge<int>> buffer(edges.size());
        EXPECT_THROW(kruskal::parallelSort(std::span<Edge<int>>(edges), std::span<Edge<int>>(buffer), 0),
                     std::invalid_argument);

        // буфер, менший за масив ребер, при p > 1
        std::vector<Edge<int>> many(10), small(5);
        EXPECT_THROW(kruskal::parallelSort(std::span<Edge<int>>(many), std::span<Edge<int>>(small), 2),
                     std::invalid_argument);
    }

    // Великий незвʼязний граф: два випадкові графи без ребер між ними.
    TEST(KruskalParallelTests, A1ForestOfTwoRandomGraphs) {
        for (std::uint64_t seed = 0; seed < 20; ++seed) {
            const auto a = kruskal::generateSparseGraph(GraphParams<int>{
                .vertexCount = 200, .edgeCount = 800, .seed = seed, .minWeight = 1, .maxWeight = 10 });
            const auto b = kruskal::generateSparseGraph(GraphParams<int>{
                .vertexCount = 150, .edgeCount = 500, .seed = seed + 1000, .minWeight = 1, .maxWeight = 10 });

            // Вершини другого графа зсуваємо на кількість вершин першого.
            Graph<int> both{ a.vertexCount + b.vertexCount, a.edges };
            for (const auto& e : b.edges) {
                both.edges.push_back({ e.u + a.vertexCount, e.v + a.vertexCount, e.weight });
            }
            // Перемішуємо, щоб ребра двох частин потрапили в різні куски вперемішку.
            std::shuffle(both.edges.begin(), both.edges.end(), std::mt19937(static_cast<std::uint32_t>(seed)));

            const auto expected = kruskal::kruskalSequential(both);
            ASSERT_EQ(expected.componentCount, 2u);

            for (const unsigned p : kThreadCounts) {
                SCOPED_TRACE(::testing::Message() << "seed=" << seed << ", p=" << p);
                const auto result = kruskal::kruskalParallel(both, p);
                EXPECT_EQ(result.componentCount, 2u);
                EXPECT_EQ(result.edges.size(), 348u);   // 350 вершин - 2 дерева
                EXPECT_EQ(result.totalWeight, expected.totalWeight);
            }
        }
    }

} // namespace
