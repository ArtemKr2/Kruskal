#include "pch.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <random>
#include <string>
#include <tuple>
#include <vector>

#include "FilterKruskal.hpp"
#include "Graph.hpp"
#include "GraphGenerator.hpp"
#include "Kruskal.hpp"

// Filter-Kruskal перевіряється порівнянням зі звичайним Крускалом

namespace {

    using kruskal::Edge;
    using kruskal::Graph;
    using kruskal::GraphParams;
    using kruskal::Vertex;

    const std::size_t kThresholds[] = { 0, 1, 4, 16, kruskal::kDefaultFilterThreshold };

    template<kruskal::EdgeWeight W>
    bool fullKeyLess(const Edge<W>& a, const Edge<W>& b) {
        return std::tie(a.weight, a.u, a.v) < std::tie(b.weight, b.u, b.v);
    }

    // Порівнює FK з різними порогами зі звичайним Крускалом.
    template<kruskal::EdgeWeight W>
    void expectSameAsKruskal(const Graph<W>& graph, bool sameEdges = false) {
        const auto expected = kruskal::kruskalSequential(graph);

        for (const std::size_t threshold : kThresholds) {
            SCOPED_TRACE("threshold=" + std::to_string(threshold));

            kruskal::FilterKruskalStats stats;
            const auto actual = kruskal::filterKruskal(graph, threshold, &stats);

            EXPECT_EQ(actual.totalWeight, expected.totalWeight);
            EXPECT_EQ(actual.componentCount, expected.componentCount);
            EXPECT_EQ(actual.edges.size(), expected.edges.size());
            EXPECT_LE(stats.sortedEdges, graph.edges.size());

            if (sameEdges) {
                auto a = actual.edges;
                auto b = expected.edges;
                std::sort(a.begin(), a.end(), fullKeyLess<W>);
                std::sort(b.begin(), b.end(), fullKeyLess<W>);
                EXPECT_EQ(a, b);
            }
        }
    }

    Graph<int> sparseGraph(Vertex n, std::uint64_t m, std::uint64_t seed, int maxWeight) {
        return kruskal::generateSparseGraph(GraphParams<int>{
            .vertexCount = n, .edgeCount = m, .seed = seed, .minWeight = 1, .maxWeight = maxWeight });
    }

    TEST(FilterKruskalTests, MatchesKruskalOnRandomSparseGraphs) {
        for (std::uint64_t seed = 1; seed <= 10; ++seed) {
            SCOPED_TRACE("seed=" + std::to_string(seed));
            expectSameAsKruskal(sparseGraph(50, 120, seed, 1000000));
            expectSameAsKruskal(sparseGraph(1000, 8000, seed, 1000000));
        }
    }

    TEST(FilterKruskalTests, MatchesKruskalWithManyEqualWeights) {
        for (std::uint64_t seed = 1; seed <= 5; ++seed) {
            SCOPED_TRACE("seed=" + std::to_string(seed));
            expectSameAsKruskal(sparseGraph(500, 4000, seed, 3));
            expectSameAsKruskal(sparseGraph(300, 2000, seed, 1));   // усі ваги однакові
        }
    }

    TEST(FilterKruskalTests, DistinctWeightsGiveIdenticalTree) {
        for (std::uint64_t seed = 1; seed <= 5; ++seed) {
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

    TEST(FilterKruskalTests, MatchesKruskalOnGridAndCompleteGraphs) {
        expectSameAsKruskal(kruskal::generateGridGraph(kruskal::GridGraphParams<int>{
            .rows = 40, .cols = 50, .seed = 7, .minWeight = 1, .maxWeight = 1000 }));

        const Vertex n = 80;
        expectSameAsKruskal(kruskal::generateDenseGraph(GraphParams<int>{
            .vertexCount = n, .edgeCount = std::uint64_t{ n } * (n - 1) / 2, .seed = 8,
            .minWeight = 1, .maxWeight = 1000 }));
    }

    // Незв'язний граф
    TEST(FilterKruskalTests, MatchesKruskalOnDisconnectedGraph) {
        for (std::uint64_t seed = 1; seed <= 5; ++seed) {
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

            expectSameAsKruskal(forest);
            EXPECT_EQ(kruskal::filterKruskal(forest, 4).componentCount, 3u);
        }
    }

    TEST(FilterKruskalTests, EdgeCases) {
        expectSameAsKruskal(Graph<int>{ 0, {} });                          // порожній граф
        expectSameAsKruskal(Graph<int>{ 1, {} });                          // одна вершина
        expectSameAsKruskal(Graph<int>{ 1, { {0, 0, 5} } });               // лише петля
        expectSameAsKruskal(Graph<int>{ 5, {} });                          // вершини без ребер
        expectSameAsKruskal(Graph<int>{ 4, {                               // петлі й паралельні ребра
            {0, 1, 5}, {1, 0, 2}, {0, 1, 2}, {2, 2, 1}, {1, 2, 7}, {2, 1, 3}, {3, 3, 0}, {2, 3, 9} } });
        expectSameAsKruskal(Graph<double>{ 3, { {0, 1, -1.5}, {1, 2, 0.25}, {0, 2, -0.5} } });
    }

    TEST(FilterKruskalTests, CountsSortedEdges) {
        const Graph<int> graph = sparseGraph(1000, 8000, 3, 1000000);

        kruskal::FilterKruskalStats all;
        (void)kruskal::filterKruskal(graph, 8000, &all);
        EXPECT_EQ(all.sortedEdges, graph.edges.size());

        kruskal::FilterKruskalStats filtered;
        (void)kruskal::filterKruskal(graph, 256, &filtered);
        EXPECT_LT(filtered.sortedEdges, graph.edges.size());
    }

} // namespace
