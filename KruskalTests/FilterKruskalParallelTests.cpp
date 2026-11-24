#include "pch.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "Dsu.hpp"
#include "FilterKruskal.hpp"
#include "FilterKruskalParallel.hpp"
#include "Graph.hpp"
#include "GraphGenerator.hpp"
#include "Kruskal.hpp"

// окремо перевіряються частини (runner, розбиття, фільтр), та квіксорт
// а весь алгоритм порівнянням зі звичайним Крускалом

namespace {

    using kruskal::Edge;
    using kruskal::Graph;
    using kruskal::GraphParams;
    using kruskal::ParallelFilterParams;
    using kruskal::TbbRunner;
    using kruskal::Vertex;

    const unsigned kThreads[] = { 1, 2, 3, 8 };
    const std::size_t kBlockSizes[] = { 1, 3, 64, 100000 };   

    // Маленькі параметри
    const ParallelFilterParams kSmallParams[] = {
        { .threads = 1, .partitionBlock = 7,  .filterBlock = 5,  .parallelCutoff = 32, .threshold = 0 },
        { .threads = 1, .partitionBlock = 64, .filterBlock = 16, .parallelCutoff = 64, .threshold = 16 },
        // Варіант Б: parallelCutoff <= threshold, базові випадки від 64 ребер сортуються паралельним quicksort
        { .threads = 1, .partitionBlock = 16, .filterBlock = 8, .parallelCutoff = 128, .threshold = 256,
          .parallelSortCutoff = 64, .quicksortCutoff = 8 },
    };

    template<kruskal::EdgeWeight W>
    bool fullKeyLess(const Edge<W>& a, const Edge<W>& b) {
        return std::tie(a.weight, a.u, a.v) < std::tie(b.weight, b.u, b.v);
    }

    template<kruskal::EdgeWeight W>
    std::vector<Edge<W>> sortedByFullKey(std::vector<Edge<W>> edges) {
        std::sort(edges.begin(), edges.end(), fullKeyLess<W>);
        return edges;
    }

    std::vector<Edge<int>> randomEdges(std::size_t n, std::uint64_t seed, int maxWeight) {
        std::mt19937_64 rng(seed);
        std::uniform_int_distribution<Vertex> vertex(0, 99);
        std::uniform_int_distribution<int> weight(1, maxWeight);
        std::vector<Edge<int>> edges(n);
        for (auto& e : edges) {
            e = {vertex(rng),vertex(rng), weight(rng)};
        }
        return edges;
    }

    Graph<int> sparseGraph(Vertex n, std::uint64_t m, std::uint64_t seed, int maxWeight) {
        return kruskal::generateSparseGraph(GraphParams<int>{
            .vertexCount = n, .edgeCount = m, .seed = seed, .minWeight = 1, .maxWeight = maxWeight });
    }

    // зробити граф з однаковими вагами, щоб було єдиний мінімальний остов.
    Graph<int> withDistinctWeights(Graph<int> graph, std::uint64_t seed) {
        std::vector<int> weights(graph.edges.size());
        std::iota(weights.begin(), weights.end(), 1);
        std::shuffle(weights.begin(), weights.end(), std::mt19937_64(seed));
        for (std::size_t i = 0; i < graph.edges.size(); ++i) {
            graph.edges[i].weight = weights[i];
        }
        return graph;
    }

    template<kruskal::EdgeWeight W>
    void expectSameAsKruskal(const Graph<W>& graph, bool sameEdges = false) {
        const auto expected = kruskal::kruskalSequential(graph);

        for (ParallelFilterParams params : kSmallParams) {
            for (const unsigned threads : kThreads) {
                params.threads = threads;
                SCOPED_TRACE("threads=" + std::to_string(threads) + " partitionBlock=" +
                             std::to_string(params.partitionBlock) + " threshold=" + std::to_string(params.threshold));

                kruskal::FilterKruskalStats stats;
                const auto actual = kruskal::filterKruskalParallel(graph, params, &stats);

                EXPECT_EQ(actual.totalWeight, expected.totalWeight);
                EXPECT_EQ(actual.componentCount, expected.componentCount);
                EXPECT_EQ(actual.edges.size(), expected.edges.size());
                EXPECT_LE(stats.sortedEdges, graph.edges.size());
                if (sameEdges) {
                    EXPECT_EQ(sortedByFullKey(actual.edges), sortedByFullKey(expected.edges));
                }
            }
        }
    }

    // Частини алгоритму
    TEST(FilterKruskalParallelTests, RunnerExecutesEachBlockExactlyOnce) {
        for (const unsigned threads : { 1u, 4u }) {
            TbbRunner runner(threads);
            for (const std::size_t blocks : { 0u, 1u, 7u, 1000u }) {
                std::vector<std::atomic<int>> calls(blocks);
                runner.forEachBlock(blocks, [&](std::size_t b) { calls[b].fetch_add(1); });
                for (std::size_t b = 0; b < blocks; ++b) {
                    EXPECT_EQ(calls[b].load(), 1) << "threads=" << threads << " blocks=" << blocks << " b=" << b;
                }
            }
        }
    }

    TEST(FilterKruskalParallelTests, ParallelPartitionGroupsEdgesCorrectly) {
        TbbRunner runner(4);
        for (const std::size_t blockSize : kBlockSizes) {
            for (const int pivot : { 1, 5, 10 }) {          // найменша, середня й найбільша вага
                SCOPED_TRACE("blockSize=" + std::to_string(blockSize) + " pivot=" + std::to_string(pivot));
                const auto original = randomEdges(1000, 7, 10);
                auto src = original;
                std::vector<Edge<int>> dst(src.size());

                const auto [lt, gt] = kruskal::detail::parallelPartition3(std::span<Edge<int>>(src), 
                    std::span<Edge<int>>(dst), pivot, blockSize, runner);

                ASSERT_LE(lt, gt);
                ASSERT_LE(gt, dst.size());
                for (std::size_t i = 0; i < dst.size(); ++i) {
                    if (i < lt)      EXPECT_LT(dst[i].weight, pivot) << "i=" << i;
                    else if (i < gt) EXPECT_EQ(dst[i].weight, pivot) << "i=" << i;
                    else             EXPECT_GT(dst[i].weight, pivot) << "i=" << i;
                }
                EXPECT_EQ(sortedByFullKey(dst), sortedByFullKey(original));   // ті самі ребра
            }
        }
    }

    TEST(FilterKruskalParallelTests, ParallelFilterMatchesSequentialFilter) {
        TbbRunner runner(4);
        for (const std::size_t blockSize : kBlockSizes) {
            SCOPED_TRACE("blockSize=" + std::to_string(blockSize));
            const auto edges = randomEdges(1000, 11, 100);

            // з'єднуємо частину вершин
            kruskal::Dsu dsuSeq(100);
            kruskal::Dsu dsuPar(100);
            for (Vertex v = 0; v + 1 < 50; ++v) {
                dsuSeq.unite(v, v + 1);
                dsuPar.unite(v, v + 1);
            }

            auto expected = edges;
            const std::size_t expectedKept = kruskal::detail::filterEdges(std::span<Edge<int>>(expected), dsuSeq);
            expected.resize(expectedKept);

            auto src = edges;
            std::vector<Edge<int>> dst(src.size());
            const std::size_t kept = kruskal::detail::parallelFilter(std::span<Edge<int>>(src), 
                std::span<Edge<int>>(dst), dsuPar, blockSize, runner);
            dst.resize(kept);

            EXPECT_EQ(dst, expected);   // ті самі ребра й у тому самому порядку
        }
    }

    // Паралельний quicksort: результат відсортований за вагою й містить ті самі ребра.
    // Малий sequentialCutoff (8), щоб рекурсія з задачами йшла глибоко; розміри навколо цієї межі.
    TEST(FilterKruskalParallelTests, ParallelQuicksortSortsCorrectly) {
        for (const unsigned threads : { 1u, 3u, 8u }) {
            TbbRunner runner(threads);
            for (const std::size_t n : { 0u, 1u, 8u, 9u, 1000u, 20000u }) {
                for (const int maxWeight : { 3, 1000000 }) {             // багато однакових ваг і майже всі різні
                    SCOPED_TRACE("threads=" + std::to_string(threads) + " n=" + std::to_string(n) +
                                 " maxWeight=" + std::to_string(maxWeight));
                    const auto original = randomEdges(n, n + static_cast<std::size_t>(maxWeight), maxWeight);
                    auto edges = original;

                    runner.execute([&] { kruskal::detail::parallelQuicksort(std::span<Edge<int>>(edges), 8); });

                    EXPECT_TRUE(std::is_sorted(edges.begin(), edges.end(), kruskal::ByWeight{}));
                    EXPECT_EQ(sortedByFullKey(edges), sortedByFullKey(original));   // ті самі ребра
                }
            }
        }
    }

    // порівняння зі звичайним Крускалом

    TEST(FilterKruskalParallelTests, MatchesKruskalOnRandomSparseGraphs) {
        for (std::uint64_t seed = 1; seed <= 5; ++seed) {
            SCOPED_TRACE("seed=" + std::to_string(seed));
            expectSameAsKruskal(sparseGraph(50, 120, seed, 1000000));
            expectSameAsKruskal(sparseGraph(1000, 8000, seed, 1000000));
        }
    }

    TEST(FilterKruskalParallelTests, MatchesKruskalWithManyEqualWeights) {
        for (std::uint64_t seed = 1; seed <= 3; ++seed) {
            SCOPED_TRACE("seed=" + std::to_string(seed));
            expectSameAsKruskal(sparseGraph(500, 4000, seed, 3));
            expectSameAsKruskal(sparseGraph(300, 2000, seed, 1));   // усі ваги однакові
        }
    }

    TEST(FilterKruskalParallelTests, DistinctWeightsGiveIdenticalTree) {
        for (std::uint64_t seed = 1; seed <= 3; ++seed) {
            SCOPED_TRACE("seed=" + std::to_string(seed));
            Graph<int> graph = sparseGraph(800, 6000, seed, 1);
            std::vector<int> weights(graph.edges.size());
            std::iota(weights.begin(), weights.end(), 1);
            std::shuffle(weights.begin(), weights.end(), std::mt19937_64(seed));
            for (std::size_t i = 0; i < graph.edges.size(); ++i) {
                graph.edges[i].weight = weights[i];
            }
            expectSameAsKruskal(graph, true);
        }
    }

    TEST(FilterKruskalParallelTests, MatchesKruskalOnGridAndCompleteGraphs) {
        expectSameAsKruskal(kruskal::generateGridGraph(kruskal::GridGraphParams<int>{
            .rows = 40, .cols = 50, .seed = 7, .minWeight = 1, .maxWeight = 1000 }));

        const Vertex n = 80;
        expectSameAsKruskal(kruskal::generateDenseGraph(GraphParams<int>{
            .vertexCount = n, .edgeCount = std::uint64_t{ n } * (n - 1) / 2, .seed = 8,
            .minWeight = 1, .maxWeight = 1000 }));
    }

    // Незв'язний граф результат (ліс із трьох дерев).
    TEST(FilterKruskalParallelTests, MatchesKruskalOnDisconnectedGraph) {
        for (std::uint64_t seed = 1; seed <= 3; ++seed) {
            SCOPED_TRACE("seed=" + std::to_string(seed));
            const Graph<int> a = sparseGraph(200, 600, seed, 100);
            const Graph<int> b = sparseGraph(150, 400, seed + 100, 100);

            Graph<int> forest{ a.vertexCount + b.vertexCount + 1, a.edges };   // остання вершина ізольована
            for (Edge<int> e : b.edges) {
                e.u += a.vertexCount;
                e.v += a.vertexCount;
                forest.edges.push_back(e);
            }
            std::shuffle(forest.edges.begin(), forest.edges.end(), std::mt19937_64(seed));

            ASSERT_EQ(kruskal::kruskalSequential(forest).componentCount, 3u);
            expectSameAsKruskal(forest);
        }
    }

    // Великі графи з реальними параметрами: паралельний FK порівнюється з послідовним FK.
    // Ваги різні, тож дерево єдине й мають збігтися самі ребра. Кожна конфігурація повторюється кілька разів,
    TEST(FilterKruskalParallelTests, LargeGraphsMatchSequentialFilterKruskal) {
        const Vertex n = 1100;   // повний граф: близько 604 тисяч ребер
        const Graph<int> graphs[] = {
            withDistinctWeights(sparseGraph(80000, 640000, 21, 1), 21),
            withDistinctWeights(kruskal::generateDenseGraph(GraphParams<int>{
                .vertexCount = n, .edgeCount = std::uint64_t{ n } * (n - 1) / 2, .seed = 22,
                .minWeight = 1, .maxWeight = 1 }), 22),
        };

        const ParallelFilterParams variantA{};   // параметри за замовчуванням
        const ParallelFilterParams variantB{ .parallelCutoff = 65536, .threshold = 131072, .parallelSortCutoff = 32768 };
        const unsigned hardwareThreads = std::max(1u, std::thread::hardware_concurrency());
        const int repeats = 3;

        for (const Graph<int>& graph : graphs) {
            const auto expected = kruskal::filterKruskal(graph);       // послідовний FK — еталон
            const auto expectedEdges = sortedByFullKey(expected.edges);

            for (ParallelFilterParams params : { variantA, variantB }) {
                for (const unsigned threads : { 2u, hardwareThreads }) {
                    params.threads = threads;
                    for (int r = 1; r <= repeats; ++r) {
                        SCOPED_TRACE("E=" + std::to_string(graph.edges.size()) + " threads=" + std::to_string(threads) +
                                     " parallelSortCutoff=" + std::to_string(params.parallelSortCutoff) +
                                     " repeat=" + std::to_string(r));
                        const auto actual = kruskal::filterKruskalParallel(graph, params);
                        EXPECT_EQ(actual.totalWeight, expected.totalWeight);
                        EXPECT_EQ(actual.componentCount, expected.componentCount);
                        EXPECT_EQ(sortedByFullKey(actual.edges), expectedEdges);
                    }
                }
            }
        }
    }

    TEST(FilterKruskalParallelTests, EdgeCases) {
        expectSameAsKruskal(Graph<int>{ 0, {} });                          // порожній граф
        expectSameAsKruskal(Graph<int>{ 1, {} });                          // одна вершина
        expectSameAsKruskal(Graph<int>{ 1, { {0, 0, 5} } });               // лише петля
        expectSameAsKruskal(Graph<int>{ 5, {} });                          // вершини без ребер
        expectSameAsKruskal(Graph<int>{ 4, {                               // петлі й паралельні ребра
            {0, 1, 5}, {1, 0, 2}, {0, 1, 2}, {2, 2, 1}, {1, 2, 7}, {2, 1, 3}, {3, 3, 0}, {2, 3, 9} } });
        expectSameAsKruskal(Graph<double>{ 3, { {0, 1, -1.5}, {1, 2, 0.25}, {0, 2, -0.5} } });

        EXPECT_THROW(TbbRunner(0), std::invalid_argument);
    }

} // namespace
