#include "pch.h"

#include <cstdint>
#include <numeric>
#include <utility>
#include <vector>

#include "Graph.hpp"
#include "GraphUtils.hpp"

using namespace kruskal;

namespace {

    using G = Graph<int>;

    // просте створення графу
    G makeGraph(Vertex n, std::vector<std::pair<Vertex, Vertex>> pairs) {
        G g;
        g.vertexCount = n;
        int w = 0;
        for (auto [u, v] : pairs) g.edges.push_back({u, v, w++});
        return g;
    }

    // ---------- isValid ----------

    TEST(GraphUtilsTests, EmptyGraphIsValid) {
        EXPECT_TRUE(isValid(makeGraph(0, {})));
        EXPECT_TRUE(isValid(makeGraph(1, {})));
        EXPECT_TRUE(isValid(makeGraph(5, {})));
    }

    TEST(GraphUtilsTests, CorrectEdgesAreValid) {
        EXPECT_TRUE(isValid(makeGraph(4, {{0, 1}, {1, 2}, {2, 3}, {3, 0}})));
    }

    TEST(GraphUtilsTests, EndpointOutOfRangeIsInvalid) {
        EXPECT_FALSE(isValid(makeGraph(3, {{0, 3}})));   // v == vertexCount
        EXPECT_FALSE(isValid(makeGraph(3, {{3, 0}})));   // u == vertexCount
        EXPECT_FALSE(isValid(makeGraph(3, {{0, 1}, {1, 100}})));  // погане ребро не першим
        EXPECT_FALSE(isValid(makeGraph(0, {{0, 0}})));   // ребро в графі без вершин
    }

    TEST(GraphUtilsTests, LoopIsInvalid) {
        EXPECT_FALSE(isValid(makeGraph(3, {{1, 1}})));
        EXPECT_FALSE(isValid(makeGraph(3, {{0, 1}, {2, 2}})));
    }

    TEST(GraphUtilsTests, ParallelEdgesAreStillValid) {
        // isValid не шукає дублікати
        EXPECT_TRUE(isValid(makeGraph(2, {{0, 1}, {1, 0}})));
    }

    // ---------- isSimple ----------

    TEST(GraphUtilsTests, SimpleGraphIsSimple) {
        EXPECT_TRUE(isSimple(makeGraph(0, {})));
        EXPECT_TRUE(isSimple(makeGraph(4, {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}})));
    }

    TEST(GraphUtilsTests, DuplicateEdgeIsNotSimple) {
        EXPECT_FALSE(isSimple(makeGraph(3, {{0, 1}, {1, 2}, {0, 1}})));
    }

    TEST(GraphUtilsTests, ReversedDuplicateIsNotSimple) {
        EXPECT_FALSE(isSimple(makeGraph(3, {{0, 1}, {1, 2}, {1, 0}})));
    }

    TEST(GraphUtilsTests, InvalidGraphIsNotSimple) {
        EXPECT_FALSE(isSimple(makeGraph(3, {{0, 0}})));
        EXPECT_FALSE(isSimple(makeGraph(3, {{0, 5}})));
    }

    TEST(GraphUtilsTests, LargeVertexNumbersDoNotCollide) {
        // Перевірка, що ключ (a << 32) | b не склеює різні ребра.
        const Vertex big = 4000000000u;
        EXPECT_TRUE(isSimple(makeGraph(big + 1, {{0, big}, {1, big}, {0, 1}})));
    }

    // ---------- isConnected ----------

    TEST(GraphUtilsTests, ZeroAndOneVertexAreConnected) {
        EXPECT_TRUE(isConnected(makeGraph(0, {})));
        EXPECT_TRUE(isConnected(makeGraph(1, {})));
    }

    TEST(GraphUtilsTests, TwoVerticesWithoutEdgeAreNotConnected) {
        EXPECT_FALSE(isConnected(makeGraph(2, {})));
        EXPECT_TRUE(isConnected(makeGraph(2, {{0, 1}})));
    }

    TEST(GraphUtilsTests, PathIsConnected) {
        EXPECT_TRUE(isConnected(makeGraph(5, {{0, 1}, {1, 2}, {2, 3}, {3, 4}})));
        // Той самий шлях, ребра в перемішаному порядку.
        EXPECT_TRUE(isConnected(makeGraph(5, {{3, 4}, {0, 1}, {2, 3}, {1, 2}})));
    }

    TEST(GraphUtilsTests, StarIsConnected) {
        EXPECT_TRUE(isConnected(makeGraph(5, {{0, 1}, {0, 2}, {0, 3}, {0, 4}})));
    }

    TEST(GraphUtilsTests, TreeIsConnected) {
        // Дерево: 0 -> {1, 2}, 1 -> {3, 4}, 2 -> {5}.
        EXPECT_TRUE(isConnected(makeGraph(6, {{0, 1}, {0, 2}, {1, 3}, {1, 4}, {2, 5}})));
    }

    TEST(GraphUtilsTests, TwoSeparatePiecesAreNotConnected) {
        // {0,1,2} і {3,4}, всередині кожного шматка є цикл, але між ними ребер немає.
        EXPECT_FALSE(isConnected(makeGraph(5, {{0, 1}, {1, 2}, {2, 0}, {3, 4}})));
    }

    TEST(GraphUtilsTests, IsolatedVertexBreaksConnectivity) {
        // Вершина 4 без ребер.
        EXPECT_FALSE(isConnected(makeGraph(5, {{0, 1}, {1, 2}, {2, 3}, {3, 0}})));
    }

    TEST(GraphUtilsTests, ManyEdgesButNotConnected) {
        // Повний граф на 0..3 і окремо ребро 4-5: ребер багато (7 > 6 - 1), але граф незв'язний.
        EXPECT_FALSE(isConnected(makeGraph(6, {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}, {4, 5}})));
    }

    TEST(GraphUtilsTests, LastEdgeConnects) {
        // Граф стає зв'язним тільки на останньому ребрі.
        EXPECT_TRUE(isConnected(makeGraph(4, {{0, 1}, {2, 3}, {1, 2}})));
    }

    TEST(GraphUtilsTests, LongPathIsConnected) {
        const Vertex n = 1000000;
        G g;
        g.vertexCount = n;
        g.edges.reserve(n - 1);
        for (Vertex i = 0; i + 1 < n; ++i) g.edges.push_back({i, i + 1, 0});
        EXPECT_TRUE(isConnected(g));

        g.edges.pop_back();   // відрізали останню вершину
        EXPECT_FALSE(isConnected(g));
    }

    TEST(GraphUtilsTests, WorksWithDoubleWeights) {
        // Функції шаблонні перевіряємо що компілюються й працюють з іншим типом ваги
        Graph<double> g;
        g.vertexCount = 3;
        g.edges = {{0, 1, 0.5}, {1, 2, 1.5}};
        EXPECT_TRUE(isValid(g));
        EXPECT_TRUE(isSimple(g));
        EXPECT_TRUE(isConnected(g));
    }

} // namespace
