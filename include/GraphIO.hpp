#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <system_error>
#include <type_traits>

#include "Graph.hpp"
#include "GraphUtils.hpp"

namespace kruskal {

    namespace detail {

        inline constexpr char graphMagic[] = { 'K', 'G', 'R', 'F' };
        inline constexpr std::uint32_t graphVersion = 1;
        inline constexpr std::uintmax_t graphHeaderSize = 24;

        // запис та читання працює тільки під little-endian
        static_assert(
            std::endian::native == std::endian::little,
            "GraphIO requires a little-endian platform."
            );

        template<typename>
        inline constexpr bool unsupportedGraphWeight = false;

        // код типу ваги який зберігається в заголовку.
        template<typename W>
        constexpr std::uint32_t weightTypeCode() {
            if constexpr (std::is_same_v<W, std::int32_t>) {
                return 1;
            }
            else if constexpr (std::is_same_v<W, std::int64_t>) {
                return 2;
            }
            else if constexpr (std::is_same_v<W, float>) {
                return 3;
            }
            else if constexpr (std::is_same_v<W, double>) {
                return 4;
            }
            else {
                static_assert(
                    unsupportedGraphWeight<W>,
                    "GraphIO supports only int32_t, int64_t, float and double."
                    );
            }
        }

        // записує байтове представлення одного значення
        template<typename T>
        void writeValue(std::ostream& out, const T& value) {
            static_assert(std::is_trivially_copyable_v<T>);

            out.write(reinterpret_cast<const char*>(&value), static_cast<std::streamsize>(sizeof(T)));
        }

        // читає одне значення. неповне читання є помилкою
        template<typename T>
        T readValue(std::istream& in) {
            static_assert(std::is_trivially_copyable_v<T>);

            T value{};

            if (!in.read( reinterpret_cast<char*>(&value), static_cast<std::streamsize>(sizeof(T)))) {
                throw std::runtime_error(
                    "GraphIO: truncated file or read error"
                );
            }

            return value;
        }


    } // namespace detail

    template<EdgeWeight W>
    void writeGraph(const std::filesystem::path& path, const Graph<W>& graph)
    {
        constexpr auto weightType = detail::weightTypeCode<W>();

        std::ofstream out(path, std::ios::binary | std::ios::trunc);

        if (!out) {
            throw std::runtime_error(
                "GraphIO: cannot open output file"
            );
        }

        // Заголовок: рівно 24 байти.
        out.write(detail::graphMagic, 4);
        detail::writeValue(out, detail::graphVersion);
        detail::writeValue(out, weightType);
        detail::writeValue(out, graph.vertexCount);
        detail::writeValue(out, static_cast<std::uint64_t>(graph.edges.size())  );

        // Кожне ребро записуємо окремими полями.
        for (const auto& edge : graph.edges) {
            detail::writeValue(out, edge.u);
            detail::writeValue(out, edge.v);
            detail::writeValue(out, edge.weight);
        }

        out.flush();

        if (!out) {
            throw std::runtime_error(
                "GraphIO: failed to write graph"
            );
        }

        out.close();

        if (!out) {
            throw std::runtime_error(
                "GraphIO: failed to close output file"
            );
        }
    }

    template<EdgeWeight W>
    [[nodiscard]] Graph<W> readGraph(const std::filesystem::path& path)
    {
        constexpr auto expectedWeightType = detail::weightTypeCode<W>();

        std::ifstream in(path, std::ios::binary);

        if (!in) {
            throw std::runtime_error(
                "GraphIO: cannot open input file"
            );
        }

        // Перевіряємо сигнатуру KGRF.
        for (const char expected : detail::graphMagic) {
            if (detail::readValue<char>(in) != expected) {
                throw std::runtime_error(
                    "GraphIO: invalid magic"
                );
            }
        }

        const auto version = detail::readValue<std::uint32_t>(in);

        const auto weightType = detail::readValue<std::uint32_t>(in);

        if (version != detail::graphVersion) {
            throw std::runtime_error(
                "GraphIO: unsupported version"
            );
        }

        if (weightType != expectedWeightType) {
            throw std::runtime_error(
                "GraphIO: weight type mismatch"
            );
        }

        Graph<W> graph;

        graph.vertexCount = detail::readValue<Vertex>(in);

        const auto edgeCount = detail::readValue<std::uint64_t>(in);

        std::error_code error;
        const auto fileSize = std::filesystem::file_size(path, error);

        if (error) {
            throw std::runtime_error(
                "GraphIO: cannot determine file size"
            );
        }

        constexpr std::uintmax_t edgeSize = 8 + sizeof(W);
        const auto payloadSize = fileSize - detail::graphHeaderSize;

        // Спочатку діленням перевіряємо, чи вміщується заявлена
        // кількість ребер у дані файлу. Потім перевіряємо точний
        // збіг розміру. Оператор || захищає множення від переповнення.
        if (edgeCount > payloadSize / edgeSize || payloadSize != edgeCount * edgeSize) {
            throw std::runtime_error(
                "GraphIO: file size mismatch"
            );
        }
        
        if (edgeCount > graph.edges.max_size()) {
            throw std::runtime_error(
                "GraphIO: too many edges"
            );
        }

        // Пам'ять виділяємо після перевірки розміру файлу.
        graph.edges.resize(static_cast<std::size_t>(edgeCount));

        for (auto& edge : graph.edges) {
            edge.u = detail::readValue<Vertex>(in);
            edge.v = detail::readValue<Vertex>(in);
            edge.weight = detail::readValue<W>(in);
        }

        if (!isValid(graph)) {
            throw std::runtime_error(
                "GraphIO: invalid vertex or self-loop"
            );
        }

        return graph;
    }

} // namespace kruskal