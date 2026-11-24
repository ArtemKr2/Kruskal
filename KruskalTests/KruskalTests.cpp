#include "pch.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include "Graph.hpp"
#include "GraphGenerator.hpp"
#include "GraphUtils.hpp"
#include "Kruskal.hpp"

namespace {

    using kruskal::Graph;
    using kruskal::GraphParams;
    using kruskal::Vertex;
    using kruskal::kruskalSequential;

    
    Graph<int> firstTestGraph() {
        Graph<int> graph{
            6,
            {
                {5, 4, 9},
                {5, 1, 4},
                {4, 1, 1},
                {4, 3, 5},
                {4, 2, 3},
                {1, 2, 2},
                {3, 2, 3},
                {3, 6, 8},
                {2, 6, 7}
            }
        };

        
        for (auto& edge : graph.edges) {
            --edge.u;
            --edge.v;
        }

        return graph;
    }

    Graph<int> secondTestGraph() {
        return Graph<int>{
            10,
            {
                {0, 1, 1},
                {0, 2, 3},
                {0, 3, 7},
                {1, 3, 7},
                {1, 5, 3},
                {2, 3, 8},
                {2, 4, 3},
                {3, 4, 9},
                {3, 5, 2},
                {4, 6, 6},
                {4, 9, 5},
                {5, 6, 4},
                {5, 7, 5},
                {6, 7, 9},
                {6, 8, 2},
                {6, 9, 10},
                {7, 8, 8},
                {8, 9, 2}
            }
        };
    }

    // Спільна перевірка остовного дерева непорожнього зв'язного графа.
    template<kruskal::EdgeWeight W>
    void expectMst(const Graph<W>& original, const kruskal::MstResult<W>& result, kruskal::WeightSum<W> expectedWeight)
    {
        ASSERT_GT(original.vertexCount, 0u);

        EXPECT_EQ(result.componentCount, 1u);

        EXPECT_EQ(
            result.edges.size(),
            static_cast<std::size_t>(original.vertexCount - 1)
        );

        EXPECT_EQ(result.totalWeight, expectedWeight);

        kruskal::WeightSum<W> actualSum{};

        for (const auto& edge : result.edges) {
            ASSERT_LT(edge.u, original.vertexCount);
            ASSERT_LT(edge.v, original.vertexCount);

            // Кожне ребро результату має бути у початковому графі.
            EXPECT_TRUE(
                std::find(
                    original.edges.begin(),
                    original.edges.end(),
                    edge
                ) != original.edges.end()
            ) << "Edge: " << edge.u << " - " << edge.v;

            actualSum += static_cast<kruskal::WeightSum<W>>(edge.weight);
        }

        // Поле totalWeight відповідає фактично повернутим ребрам.
        EXPECT_EQ(actualSum, result.totalWeight);

        const Graph<W> tree {
            original.vertexCount,
            result.edges
        };

        EXPECT_TRUE(kruskal::isSimple(tree));
        EXPECT_TRUE(kruskal::isConnected(tree));
    }

    // Використовує інший алгоритм знаходження мінімального остовного дерева Прім на матриці суміжності.
    // Використовується лише для малих зв'язних графів з цілочисельними вагами.
    std::int64_t primWeight(const Graph<int>& graph) {
        const Vertex n = graph.vertexCount;

        if (n == 0) {
            return 0;
        }

        constexpr auto inf = (std::numeric_limits<std::int64_t>::max)();

        std::vector<std::vector<std::int64_t>> matrix(
            n, std::vector<std::int64_t>(n, inf)
        );

        for (const auto& edge : graph.edges) {
            const auto weight = static_cast<std::int64_t>(edge.weight);

            matrix[edge.u][edge.v] = std::min(matrix[edge.u][edge.v], weight);

            matrix[edge.v][edge.u] = std::min(matrix[edge.v][edge.u], weight);
        }

        std::vector<std::int64_t> best(n, inf);
        std::vector<bool> used(n, false);

        best[0] = 0;
        std::int64_t total = 0;

        for (Vertex step = 0; step < n; ++step) {
            Vertex v = n;

            // Вибираємо найближчу до поточного дерева вершину.
            for (Vertex u = 0; u < n; ++u) {
                if (!used[u] && (v == n || best[u] < best[v])) {
                    v = u;
                }
            }

            if (v == n || best[v] == inf) {
                throw std::logic_error(
                    "Prim reference requires a connected graph"
                );
            }

            used[v] = true;
            total += best[v];

            // Оновлюємо вартість приєднання решти вершин.
            for (Vertex u = 0; u < n; ++u) {
                if (!used[u] && matrix[v][u] < best[u]) {
                    best[u] = matrix[v][u];
                }
            }
        }

        return total;
    }

    // 1. Сортування впорядковує ваги й зберігає всі ребра.
    TEST(KruskalTests, SortEdgesOrdersByWeightAndPreservesEdges) {
        std::vector<kruskal::Edge<int>> edges{
            {2, 3, 8},
            {0, 1, -3},
            {1, 2, 0},
            {0, 2, 0}
        };

        const auto original = edges;

        kruskal::sortEdges(edges);

        EXPECT_TRUE(std::is_sorted(
            edges.begin(),
            edges.end(),
            [](const auto& a, const auto& b) {
                return a.weight < b.weight;
            }
        ));

        EXPECT_TRUE(std::is_permutation(
            edges.begin(), edges.end(),
            original.begin(), original.end()
        ));
    }

    // 2. Перший граф: 5 ребер остова, сума 17.
    TEST(KruskalTests, firstTestGraphHasWeight17) {
        const auto graph = firstTestGraph();

        ASSERT_EQ(graph.vertexCount, 6u);
        ASSERT_EQ(graph.edges.size(), 9u);

        const auto result = kruskalSequential(graph);

        expectMst(graph, result, std::int64_t{ 17 });
        EXPECT_EQ(primWeight(graph), 17);
    }

    // 3. Другий граф: 9 ребер остова, сума 25.
    TEST(KruskalTests, secondTestGraphHasWeight25) {
        const auto graph = secondTestGraph();

        ASSERT_EQ(graph.vertexCount, 10u);
        ASSERT_EQ(graph.edges.size(), 18u);

        const auto result = kruskalSequential(graph);

        expectMst(graph, result, std::int64_t{ 25 });
        EXPECT_EQ(primWeight(graph), 25);
    }

    // 4. Порожній граф.
    TEST(KruskalTests, EmptyGraph) {
        const auto result = kruskalSequential(Graph<int>{});

        EXPECT_TRUE(result.edges.empty());
        EXPECT_EQ(result.totalWeight, 0);
        EXPECT_EQ(result.componentCount, 0u);
    }

    // 5. Одна вершина без ребер.
    TEST(KruskalTests, SingleVertexGraph) {
        const Graph<int> graph{ 1, {} };

        const auto result = kruskalSequential(graph);

        EXPECT_TRUE(result.edges.empty());
        EXPECT_EQ(result.totalWeight, 0);
        EXPECT_EQ(result.componentCount, 1u);
    }

    // 6. Дві вершини та одне ребро.
    TEST(KruskalTests, SingleEdgeGraph) {
        const Graph<int> graph{
            2,
            {
                {0, 1, 7}
            }
        };

        const auto result = kruskalSequential(graph);

        expectMst(graph, result, std::int64_t{ 7 });
    }

    // 7. Кілька ізольованих вершин.
    TEST(KruskalTests, GraphWithoutEdges) {
        const Graph<int> graph{ 5, {} };

        const auto result = kruskalSequential(graph);

        EXPECT_TRUE(result.edges.empty());
        EXPECT_EQ(result.totalWeight, 0);
        EXPECT_EQ(result.componentCount, 5u);
    }

    // 8. Незв'язний граф повертає мінімальний остовний ліс.
    TEST(KruskalTests, DisconnectedGraphProducesForest) {
        const Graph<int> graph{
            6,
            {
                {0, 1, 1},
                {1, 2, 2},
                {0, 2, 10},
                {3, 4, 4}
            }
        };

        // Компоненти: {0,1,2}, {3,4}, {5}.
        // Мінімальна сума: 1 + 2 + 4 = 7.
        const auto result = kruskalSequential(graph);

        EXPECT_EQ(result.componentCount, 3u);
        EXPECT_EQ(result.edges.size(), 3u);
        EXPECT_EQ(result.totalWeight, 7);

        const Graph<int> forest{
            graph.vertexCount,
            result.edges
        };

        EXPECT_TRUE(kruskal::isSimple(forest));
        EXPECT_FALSE(kruskal::isConnected(forest));
    }

    // 9. Рівні ваги та зворотний порядок входу.
    TEST(KruskalTests, EqualWeightsPreserveMinimumWeightWhenReversed) {
        Graph<int> graph{
            4,
            {
                {0, 1, 1},
                {0, 2, 1},
                {0, 3, 1},
                {1, 2, 1},
                {1, 3, 1},
                {2, 3, 1}
            }
        };

        const auto first = kruskalSequential(graph);
        expectMst(graph, first, std::int64_t{ 3 });

        std::reverse(graph.edges.begin(), graph.edges.end());

        const auto second = kruskalSequential(graph);
        expectMst(graph, second, std::int64_t{ 3 });

        EXPECT_EQ(first.totalWeight, second.totalWeight);
        EXPECT_EQ(first.componentCount, second.componentCount);

        // Списки ребер можуть відрізнятися:
        // для цього графа існує кілька мінімальних остовів.
    }

    // 10. Передавання за значенням зберігає початковий граф.
    TEST(KruskalTests, DoesNotModifyOriginalGraph) {
        const auto graph = secondTestGraph();
        const auto before = graph;

        const auto result = kruskalSequential(graph);

        EXPECT_EQ(result.totalWeight, 25);
        EXPECT_EQ(graph.vertexCount, before.vertexCount);
        EXPECT_EQ(graph.edges, before.edges);
    }

    // 11. Сума цілих ваг може перевищувати межу int.
    TEST(KruskalTests, AccumulatesIntegerWeightsIn64Bits) {
        const Graph<int> graph{
            3,
            {
                {0, 1, 2000000000},
                {1, 2, 2000000000}
            }
        };

        const auto result = kruskalSequential(graph);

        expectMst(graph, result, std::int64_t{ 4000000000 });
    }

    // 12. Від'ємні й дробові ваги.
    TEST(KruskalTests, HandlesNegativeAndFractionalWeights) {
        const Graph<double> graph{
            3,
            {
                {0, 1, -2.5},
                {1, 2, 0.75},
                {0, 2, 4.0}
            }
        };

        const auto result = kruskalSequential(graph);

        expectMst(graph, result, -1.75);
    }

    // 13. Звірка з Прімом на 300 випадкових графах.
    // Для кожного також перевіряємо зворотний порядок ребер.
    TEST(KruskalTests, MatchesPrimOnRandomGraphs) {
        const Vertex sizes[] = { 2, 3, 5, 10, 20, 50 };

        for (const Vertex n : sizes) {
            const std::uint64_t minEdges = std::uint64_t{n} - 1;

            const std::uint64_t maxEdges = std::max(minEdges, std::uint64_t{n} *(n - 1) / 4);

            for (std::uint64_t seed = 0; seed < 50; ++seed) {
                const std::uint64_t m = minEdges + (seed * 17) % (maxEdges - minEdges + 1);

                SCOPED_TRACE(
                    ::testing::Message()
                    << "n=" << n
                    << ", m=" << m
                    << ", seed=" << seed
                );

                auto graph = kruskal::generateSparseGraph(GraphParams<int>{.vertexCount = n, .edgeCount = m, 
                    .seed = seed, .minWeight = 1, .maxWeight = 10
                });

                const auto expectedWeight = primWeight(graph);

                const auto first = kruskalSequential(graph);
                expectMst(graph, first, expectedWeight);

                std::reverse(
                    graph.edges.begin(),
                    graph.edges.end()
                );

                const auto second = kruskalSequential(graph);
                expectMst(graph, second, expectedWeight);

                EXPECT_EQ(first.totalWeight, second.totalWeight);
                EXPECT_EQ(
                    first.componentCount,
                    second.componentCount
                );
            }
        }
    }

    // 14. Ліс із двох випадкових графі. Прім рахуємо для кожної частини окремо.
    TEST(KruskalTests, ForestOfTwoRandomGraphsMatchesPrim) {
        for (std::uint64_t seed = 0; seed < 50; ++seed) {
            const auto a = kruskal::generateSparseGraph(GraphParams<int>{ .vertexCount = 20, .edgeCount = 40, .seed = seed, .minWeight = 1, .maxWeight = 10 });
            const auto b = kruskal::generateSparseGraph(GraphParams<int>{ .vertexCount = 15, .edgeCount = 25, .seed = seed + 1000, .minWeight = 1, .maxWeight = 10 });

            // Друга частина зсувається на кількість вершин першої.
            Graph<int> both{ a.vertexCount + b.vertexCount, a.edges };
            for (const auto& e : b.edges) {
                both.edges.push_back({ e.u + a.vertexCount, e.v + a.vertexCount, e.weight });
            }

            const auto result = kruskalSequential(both);
            EXPECT_EQ(result.componentCount, 2u);
            EXPECT_EQ(result.edges.size(), std::size_t{ 33 });   // 35 вершин − 2 дерева
            EXPECT_EQ(result.totalWeight, primWeight(a) + primWeight(b));
        }
    }

    // Паралельні ребра (дублікати з різними і з однаковими вагами) і петля.
    TEST(KruskalTests, HandlesParallelEdgesAndSelfLoop) {
        const Graph<int> graph{ 3, {
            {0, 1, 5}, {0, 1, 5}, {1, 0, 7},   // три ребра між 0 і 1, найлегше 2
            {1, 2, 3}, {2, 1, 3},              // два однакові ребра між 1 і 2
            {2, 2, 1}                          // петля, найлегша з усіх
        } };
        const auto result = kruskalSequential(graph);
        EXPECT_EQ(result.totalWeight, 8);       // 5 + 3
        EXPECT_EQ(result.edges.size(), 2u);
        EXPECT_EQ(result.componentCount, 1u);
    }

} // namespace