#include "pch.h"

#include <cstdint>
#include <numeric>
#include <random>
#include <utility>
#include <vector>

#include "Dsu.hpp"

namespace {

    using kruskal::Dsu;
    using kruskal::Vertex;

    // 1. Початковий стан.
    TEST(DsuTests, InitiallyEveryVertexIsSeparate) {
        constexpr Vertex n = 6;
        Dsu dsu(n);

        EXPECT_EQ(dsu.size(), n);
        EXPECT_EQ(dsu.componentCount(), n);

        for (Vertex v = 0; v < n; ++v) {
            EXPECT_EQ(dsu.find(v), v);
            EXPECT_EQ(dsu.findReadOnly(v), v);
        }
    }

    // 2. Результат unite та зміна лічильника.
    TEST(DsuTests, UniteReportsWhetherComponentsMerged) {
        Dsu dsu(3); // [0, 1, 2]

        EXPECT_TRUE(dsu.unite(0, 1));
        EXPECT_EQ(dsu.componentCount(), 2u);

        EXPECT_FALSE(dsu.unite(0, 1));
        EXPECT_EQ(dsu.componentCount(), 2u);

        EXPECT_FALSE(dsu.unite(1, 0));
        EXPECT_EQ(dsu.componentCount(), 2u);

        EXPECT_FALSE(dsu.unite(2, 2));
        EXPECT_EQ(dsu.componentCount(), 2u);
    }

    // 3. Симетрія: порівнюємо два незалежні DSU.
    TEST(DsuTests, UniteIsSymmetric) {
        constexpr Vertex n = 6;

        Dsu forward(n);
        Dsu reversed(n);

        const std::pair<Vertex, Vertex> operations[] = {
            {0, 1},
            {2, 3},
            {1, 2},
            {0, 3}, // Уже одна компонента.
            {4, 4}  // Злиття із собою.
        };

        for (const auto& [a, b] : operations) {
            const bool first = forward.unite(a, b);
            const bool second = reversed.unite(b, a);

            EXPECT_EQ(first, second);
            EXPECT_EQ(
                forward.componentCount(),
                reversed.componentCount()
            );

            // Номери коренів можуть різнитися.
            // Порівнюємо саме належність до компонент.
            for (Vertex u = 0; u < n; ++u) {
                for (Vertex v = 0; v < n; ++v) {
                    EXPECT_EQ(
                        forward.findReadOnly(u) ==
                        forward.findReadOnly(v),
                        reversed.findReadOnly(u) ==
                        reversed.findReadOnly(v)
                    );
                }
            }
        }
    }

    // 4. Транзитивність.
    TEST(DsuTests, ConnectivityIsTransitive) {
        Dsu dsu(4);

        EXPECT_TRUE(dsu.unite(0, 1)); // A-B.
        EXPECT_TRUE(dsu.unite(2, 3)); // C-D.
        EXPECT_TRUE(dsu.unite(1, 2)); // B-C.

        EXPECT_EQ(dsu.find(0), dsu.find(3));
        EXPECT_EQ(dsu.componentCount(), 1u);
    }

    // 5. Приклад із шістьма містами.
    TEST(DsuTests, SixCitiesExample) {
        constexpr Vertex A = 0;
        constexpr Vertex B = 1;
        constexpr Vertex C = 2;
        constexpr Vertex D = 3;
        constexpr Vertex E = 4;
        constexpr Vertex F = 5;

        Dsu dsu(6);

        EXPECT_EQ(dsu.componentCount(), 6u);

        EXPECT_TRUE(dsu.unite(A, B));
        EXPECT_EQ(dsu.componentCount(), 5u);

        EXPECT_TRUE(dsu.unite(C, D));
        EXPECT_EQ(dsu.componentCount(), 4u);

        EXPECT_TRUE(dsu.unite(B, C));
        EXPECT_EQ(dsu.componentCount(), 3u);

        EXPECT_FALSE(dsu.unite(A, D));
        EXPECT_EQ(dsu.componentCount(), 3u);

        EXPECT_NE(dsu.find(E), dsu.find(A));
        EXPECT_NE(dsu.find(F), dsu.find(A));
        EXPECT_NE(dsu.find(E), dsu.find(F));
    }

    // 6. Малі межі.
    TEST(DsuTests, HandlesSmallSizes) {
        Dsu empty(0);

        EXPECT_EQ(empty.size(), 0u);
        EXPECT_EQ(empty.componentCount(), 0u);
        // У порожнього DSU немає допустимих аргументів для find.

        Dsu single(1);

        EXPECT_EQ(single.size(), 1u);
        EXPECT_EQ(single.find(0), 0u);
        EXPECT_EQ(single.findReadOnly(0), 0u);
        EXPECT_FALSE(single.unite(0, 0));
        EXPECT_EQ(single.componentCount(), 1u);

        Dsu pair(2);

        EXPECT_NE(pair.find(0), pair.find(1));
        EXPECT_TRUE(pair.unite(0, 1));
        EXPECT_EQ(pair.find(0), pair.find(1));
        EXPECT_EQ(pair.componentCount(), 1u);
    }

    // 7. findReadOnly повертає ті самі корені, що й find.
    TEST(DsuTests, ReadOnlyFindAgreesWithFind) {
        constexpr Vertex n = 9;
        Dsu dsu(n);

        // Злиття компонент однакового рангу створюють
        // дерево з кількома рівнями. Вершина 8 окрема.
        dsu.unite(0, 1);
        dsu.unite(2, 3);
        dsu.unite(0, 2);

        dsu.unite(4, 5);
        dsu.unite(6, 7);
        dsu.unite(4, 6);

        dsu.unite(0, 4);

        const Dsu& readOnly = dsu;
        std::vector<Vertex> roots(n);

        // Спочатку читаємо всі корені без стиснення.
        for (Vertex v = 0; v < n; ++v) {
            roots[v] = readOnly.findReadOnly(v);
        }

        const Vertex countBefore = dsu.componentCount();

        // Потім перевіряємо пошук зі стисненням.
        for (Vertex v = 0; v < n; ++v) {
            EXPECT_EQ(dsu.find(v), roots[v]);
        }

        EXPECT_EQ(dsu.componentCount(), countBefore);

        for (Vertex v = 0; v < n; ++v) {
            EXPECT_EQ(readOnly.findReadOnly(v), roots[v]);
        }
    }

    // Проста незалежна модель для випадкового тесту
    // label_[v] -мітка компоненти вершини v
    // Дерев, рангів та стиснення шляхів тут немає
    class NaiveComponents {
    public:
        explicit NaiveComponents(Vertex n): label_(n), components_(n)
        {
            std::iota(label_.begin(), label_.end(), Vertex{0});
        }

        bool unite(Vertex a, Vertex b) {
            const Vertex oldLabel = label_[b];
            const Vertex newLabel = label_[a];

            if (oldLabel == newLabel) {
                return false;
            }

            for (Vertex& label : label_) {
                if (label == oldLabel) {
                    label = newLabel;
                }
            }

            --components_;
            return true;
        }

        bool connected(Vertex a, Vertex b) const {
            return label_[a] == label_[b];
        }

        Vertex componentCount() const {
            return components_;
        }

    private:
        std::vector<Vertex> label_;
        Vertex components_;
    };

    // 8. Випадкове порівняння з незалежною моделлю.
    TEST(DsuTests, MatchesNaiveModelOnRandomOperations) {
        const Vertex sizes[] = { 1, 2, 3, 5, 10, 50, 200 };

        for (const Vertex n : sizes) {
            for (std::uint32_t seed = 0; seed < 200; ++seed) {
                SCOPED_TRACE(::testing::Message()<< "n=" << n << ", seed=" << seed);

                Dsu dsu(n);
                NaiveComponents model(n);

                std::mt19937 generator(seed);
                std::uniform_int_distribution<Vertex> vertex(Vertex{0}, n - Vertex{1});

                for (Vertex step = 0; step < 4u * n; ++step) {
                    SCOPED_TRACE(
                        ::testing::Message() << "step=" << step
                    );

                    const Vertex a = vertex(generator);
                    const Vertex b = vertex(generator);

                    const bool expected = model.unite(a, b);
                    const bool actual = dsu.unite(a, b);

                    ASSERT_EQ(actual, expected);
                    ASSERT_EQ(
                        dsu.componentCount(),
                        model.componentCount()
                    );

                    // Після кожної операції перевіряємо
                    // зв'язність випадкової пари.
                    const Vertex u = vertex(generator);
                    const Vertex v = vertex(generator);

                    const bool expectedConnected =model.connected(u, v);

                    ASSERT_EQ(
                        dsu.findReadOnly(u) == dsu.findReadOnly(v),
                        expectedConnected
                    );

                    ASSERT_EQ(
                        dsu.find(u) == dsu.find(v),
                        expectedConnected
                    );
                }
            }
        }
    }

    // 9а. Мільйон вершин, прямий порядок аргументів.
    TEST(DsuTests, HandlesMillionVerticesForward) {
        constexpr Vertex n = 1000000;
        Dsu dsu(n);

        for (Vertex i = 0; i < n - 1; ++i) {
            ASSERT_TRUE(dsu.unite(i, i + 1)) << "i=" << i;
        }

        ASSERT_EQ(dsu.componentCount(), 1u);

        const Vertex root = dsu.find(0);

        for (Vertex v = 0; v < n; ++v) {
            ASSERT_EQ(dsu.findReadOnly(v), root) << "v=" << v;
        }
    }

    // 9b. Новий DSU, обернений порядок аргументів.
    TEST(DsuTests, HandlesMillionVerticesReversed) {
        constexpr Vertex n = 1000000;
        Dsu dsu(n);

        for (Vertex i = 0; i < n - 1; ++i) {
            ASSERT_TRUE(dsu.unite(i + 1, i)) << "i=" << i;
        }

        ASSERT_EQ(dsu.componentCount(), 1u);

        const Vertex root = dsu.find(0);

        for (Vertex v = 0; v < n; ++v) {
            ASSERT_EQ(dsu.findReadOnly(v), root) << "v=" << v;
        }
    }

} // namespace