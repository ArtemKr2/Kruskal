#include "pch.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>

#include "GraphIO.hpp"

namespace {

    using kruskal::Graph;
    using kruskal::Vertex;
    using kruskal::readGraph;
    using kruskal::writeGraph;

    // Спільна підготовка та очищення для тестів.
    class GraphIOTests : public ::testing::Test {
    protected:
        std::filesystem::path path_;

        void SetUp() override {
            const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();

            path_ = std::filesystem::temp_directory_path() /
                ("kruskal_test_" + std::string(info->name()) + ".bin");

            // Прибираємо залишок від попереднього запуску,
            // якщо той був аварійно перерваний.
            std::error_code error;
            std::filesystem::remove(path_, error);

            ASSERT_FALSE(error) << error.message();
        }

        void TearDown() override {
            std::error_code error;
            std::filesystem::remove(path_, error);

            EXPECT_FALSE(error) << error.message();
        }

        // Записуємо граф, читаємо й порівнюємо.
        template<kruskal::EdgeWeight W>
        void expectRoundTrip(const Graph<W>& original) {
            writeGraph(path_, original);

            const auto restored = readGraph<W>(path_);

            EXPECT_EQ(restored.vertexCount, original.vertexCount);
            EXPECT_EQ(restored.edges, original.edges);
        }

        static Graph<double> makeSmallGraph() {
            return Graph<double>{
                4,
                {
                    {0, 1, -2.5},
                    {1, 2,  0.25},
                    {2, 3, 10.0}
                }
            };
        }

        // Навмисно пошкоджуємо окреме поле готового файлу
        // Не використовуємо detail::writeValue із GraphIO
        // тест змінює байти незалежно від реалізації.
        template<typename T>
        void overwriteValue(std::streamoff offset, const T& value) {
            std::fstream file;

            file.exceptions(
                std::ios::failbit | std::ios::badbit
            );

            file.open(
                path_,
                std::ios::binary | std::ios::in | std::ios::out
            );

            file.seekp(offset);

            file.write(
                reinterpret_cast<const char*>(&value),
                static_cast<std::streamsize>(sizeof(T))
            );

            file.flush();
            file.close();
        }
    };

    // 1. Порожній граф.
    TEST_F(GraphIOTests, RoundTripsEmptyGraph) {
        const Graph<int> graph{ 0, {} };

        expectRoundTrip(graph);
    }

    // 2. Одна вершина без ребер.
    TEST_F(GraphIOTests, RoundTripsSingleVertexGraph) {
        const Graph<int> graph{ 1, {} };

        expectRoundTrip(graph);
    }

    // 3. Малий граф із цілими вагами.
    TEST_F(GraphIOTests, RoundTripsIntGraph) {
        const Graph<int> graph{
            4,
            {
                {0, 1, -7},
                {1, 2,  0},
                {2, 3, 10},
                {0, 3, 25}
            }
        };

        expectRoundTrip(graph);
    }

    // 4. Малий граф із від'ємними та дробовими вагами.
    TEST_F(GraphIOTests, RoundTripsDoubleGraph) {
        expectRoundTrip(makeSmallGraph());
    }

    // 5. Великий граф зі 100 000 ребер.
    TEST_F(GraphIOTests, RoundTripsLargeGraph) {
        Graph<double> graph;
        graph.vertexCount = 10000;

        constexpr std::size_t edgeCount = 100000;
        graph.edges.reserve(edgeCount);

        std::mt19937 generator(12345);

        std::uniform_int_distribution<Vertex> vertex(
            Vertex{0}, graph.vertexCount - 1
        );

        std::uniform_int_distribution<int> weight(
            -10000, 10000
        );

        for (std::size_t i = 0; i < edgeCount; ++i) {
            const Vertex u = vertex(generator);
            Vertex v = vertex(generator);

            // Не створюємо петлю.
            if (u == v) {
                v = (v + 1u) % graph.vertexCount;
            }

            // Ділення на 4 дає точно представлені двійкові дроби.
            const double w = static_cast<double>(weight(generator)) / 4.0;

            graph.edges.push_back({u, v, w});
        }

        expectRoundTrip(graph);
    }

    // 6. Розмір файлу відповідає формату.
    TEST_F(GraphIOTests, FileSizeMatchesFormat) {
        const auto graph = makeSmallGraph();

        writeGraph(path_, graph);

        // Константи формату задані незалежно від GraphIO.
        constexpr std::uintmax_t headerSize = 24;
        constexpr std::uintmax_t edgeSize = 8 + sizeof(double);

        const std::uintmax_t expectedSize = headerSize +
            static_cast<std::uintmax_t>(graph.edges.size()) * edgeSize;

        EXPECT_EQ(
            std::filesystem::file_size(path_),
            expectedSize
        );
    }

    // 7. Читання неіснуючого файлу.
    TEST_F(GraphIOTests, RejectsMissingFile) {
        // SetUp видалив файл, якщо він існував.
        ASSERT_FALSE(std::filesystem::exists(path_));

        EXPECT_THROW(
            static_cast<void>(readGraph<int>(path_)),
            std::runtime_error
        );
    }

    // 8. Неправильна сигнатура.
    TEST_F(GraphIOTests, RejectsWrongMagic) {
        writeGraph(path_, makeSmallGraph());

        // Змінюємо лише magic, решта файлу залишається коректною.
        const char badMagic[] = { 'B', 'A', 'D', '!' };
        overwriteValue(0, badMagic);

        EXPECT_THROW(
            static_cast<void>(readGraph<double>(path_)),
            std::runtime_error
        );
    }

    // 9. Спроба прочитати double як int64_t.
    TEST_F(GraphIOTests, RejectsWrongWeightType) {
        writeGraph(path_, makeSmallGraph());

        EXPECT_THROW(
            static_cast<void>(readGraph<std::int64_t>(path_)),
            std::runtime_error
        );
    }

    // 10. Обрізаний файл.
    TEST_F(GraphIOTests, RejectsTruncatedFile) {
        writeGraph(path_, makeSmallGraph());

        const auto originalSize = std::filesystem::file_size(path_);

        std::filesystem::resize_file(path_, originalSize - 1);

        EXPECT_THROW(
            static_cast<void>(readGraph<double>(path_)),
            std::runtime_error
        );
    }

    // 11. Зайвий байт наприкінці.
    TEST_F(GraphIOTests, RejectsTrailingByte) {
        writeGraph(path_, makeSmallGraph());

        {
            std::ofstream out(
                path_,
                std::ios::binary | std::ios::app
            );

            ASSERT_TRUE(out.is_open());

            out.put('X');
            out.flush();

            ASSERT_TRUE(out.good());

            // Потік закриється перед початком читання.
        }

        EXPECT_THROW(
            static_cast<void>(readGraph<double>(path_)),
            std::runtime_error
        );
    }

    // 12. Некоректний номер вершини:
    // окремо перевіряємо u та v.
    TEST_F(GraphIOTests, RejectsOutOfRangeVertex) {
        Graph<int> graph{
            3,
            {
                {3, 1, 5} // Допустимі лише вершини 0, 1, 2.
            }
        };

        ASSERT_NO_THROW(writeGraph(path_, graph));

        EXPECT_THROW(
            static_cast<void>(readGraph<int>(path_)),
            std::runtime_error
        );

        graph.edges[0] = {0, 3, 5};

        ASSERT_NO_THROW(writeGraph(path_, graph));

        EXPECT_THROW(
            static_cast<void>(readGraph<int>(path_)),
            std::runtime_error
        );
    }

    // 13. Петля.
    TEST_F(GraphIOTests, RejectsSelfLoop) {
        const Graph<int> graph{
            3,
            {
                {1, 1, 5}
            }
        };

        ASSERT_NO_THROW(writeGraph(path_, graph));

        EXPECT_THROW(
            static_cast<void>(readGraph<int>(path_)),
            std::runtime_error
        );
    }

    // 14. Невідома версія формату.
    TEST_F(GraphIOTests, RejectsUnsupportedVersion) {
        writeGraph(path_, makeSmallGraph());

        // version знаходиться за зміщенням 4.
        const std::uint32_t unsupportedVersion = 2;
        overwriteValue(4, unsupportedVersion);

        EXPECT_THROW(
            static_cast<void>(readGraph<double>(path_)),
            std::runtime_error
        );
    }

    // 15. Завелике edgeCount у заголовку.
    TEST_F(GraphIOTests, RejectsHugeEdgeCount) {
        writeGraph(path_, makeSmallGraph());

        // edgeCount знаходиться за зміщенням 16.
        const std::uint64_t hugeCount = (std::numeric_limits<std::uint64_t>::max)();

        overwriteValue(16, hugeCount);

        EXPECT_THROW(
            static_cast<void>(readGraph<double>(path_)),
            std::runtime_error
        );
    }

} // namespace