#pragma once

#include <algorithm>
#include <array>
#include <memory>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <tbb/task_arena.h>
#include <tbb/task_group.h>

#include "Dsu.hpp"
#include "FilterKruskal.hpp"
#include "Graph.hpp"
#include "Kruskal.hpp"
#include "UninitializedBuffer.hpp"

namespace kruskal {

    // Параметри
    struct ParallelFilterParams {
        unsigned    threads        = 8;
        std::size_t partitionBlock = 8192;     // розмір блоку для паралельного розбиття
        std::size_t filterBlock    = 2048;     // розмір блоку для паралельного фільтра
        std::size_t parallelCutoff = 65536;    // менші діапазони обробляються послідовно
        std::size_t threshold      = 262144;   // взятий на основі тестування послідовного
        std::size_t parallelSortCutoff = 32768;     // базовий випадок від такого розміру сортується паралельно; 0 - вимкнено
        std::size_t quicksortCutoff    = 16384; // шматки, не більші за це, паралельний quicksort сортує звичайним sortRange
    };

    // обгортка над пулом, яка запускає задачі
    class TbbRunner {
    public:
        explicit TbbRunner(unsigned threads) : arena_(checkedThreads(threads)) {}

        // Додає блок body(b) для всіх b в пул поток
        template<typename Body>
        void forEachBlock(std::size_t numBlocks, const Body& body) {
            if (numBlocks == 0) {
                return;
            }
            arena_.execute([&] { runRange(0, numBlocks, body); });
        }

        // Виконати f() всередині пулу TBB, а задачі, створені в f, виконуються потоками цьогож пулу
        template<typename F>
        void execute(const F& f) {
            arena_.execute(f);
        }

    private:
        static int checkedThreads(unsigned threads) {
            if (threads == 0) {
                throw std::invalid_argument("TbbRunner: threads must be >= 1");
            }
            return static_cast<int>(threads);
        }

        // Поділ навпіл задачі 
        template<typename Body>
        static void runRange(std::size_t lo, std::size_t hi, const Body& body) {
            if (hi - lo == 1) {
                body(lo);
                return;
            }
            const std::size_t mid = lo + (hi - lo) / 2;
            tbb::task_group tg;
            tg.run([&] { runRange(lo, mid, body); });   // fork
            runRange(mid, hi, body);                    // compute
            tg.wait();                                  // join
        }

        tbb::task_arena arena_;
    };

    namespace detail {

        // Межі блоків: [0, B, 2B, ..., n]. Кожен блок b займає [bounds[b], bounds[b + 1])
        inline std::vector<std::size_t> blockBounds(std::size_t n, std::size_t blockSize) {
            blockSize = std::max<std::size_t>(blockSize, 1); //мінімальний розмір блоку =1
            std::vector<std::size_t> bounds;
            bounds.reserve((n + blockSize - 1) / blockSize + 1);
            for (std::size_t start = 0; start < n; start += blockSize) {
                bounds.push_back(start);
            }
            bounds.push_back(n);
            return bounds;
        }

        // Функція для паралельного виконаня потоками запису значень з груп підзавдань з їх власних блоків 
        // в загальний реузльтат (додатковий буффер) в правильні місця за допомогою префіксних сум
        template<std::size_t K, typename Runner, EdgeWeight W>
        std::array<std::size_t, K> scatterGroups(std::span<const Edge<W>> src, std::span<Edge<W>> dst,
                                                 const std::vector<std::size_t>& bounds,
                                                 const std::vector<std::array<std::size_t, K>>& sizes,
                                                 Runner& runner) {
            const std::size_t blocks = sizes.size();

            // Префіксні суми
            std::array<std::size_t, K> totals{}; // загальна кількість елементів в кожній групі
            for (const auto& s : sizes) {
                for (std::size_t g = 0; g < K; ++g) {
                    totals[g] += s[g];
                }
            }

            std::array<std::size_t, K> pos{}; // глобальні позиції
            for (std::size_t g = 1; g < K; ++g) {
                pos[g] = pos[g - 1] + totals[g - 1];
            }

            std::vector<std::array<std::size_t, K>> offset(blocks);
            for (std::size_t b = 0; b < blocks; ++b) {
                offset[b] = pos; // зміщення звідки пише в групу блок b
                for (std::size_t g = 0; g < K; ++g) {
                    pos[g] += sizes[b][g]; // обрахування зміщення для наступного
                }
            }

            // Паралельне копіювання
            runner.forEachBlock(blocks, [&](std::size_t b) {
                std::size_t from = bounds[b];
                for (std::size_t g = 0; g < K; ++g) {
                    const std::size_t to = offset[b][g];
                    for (std::size_t i = 0; i < sizes[b][g]; ++i) {
                        dst[to + i] = src[from + i];
                    }
                    from += sizes[b][g];
                }
            });

            return totals;
        }

        // Паралельне розбиття масиву
        template<typename Runner, EdgeWeight W>
        std::pair<std::size_t, std::size_t> parallelPartition3(std::span<Edge<W>> src, std::span<Edge<W>> dst,
                        const W& pivot, std::size_t blockSize, Runner& runner) {
            // ділення масиву на діапазони блоків в залежності від заданого розміру блоку
            const std::vector<std::size_t> bounds = blockBounds(src.size(), blockSize); 
            // для кожного блоку {кількість <pivot, кількість =pivot, кількість >pivot}
            std::vector<std::array<std::size_t, 3>> sizes(bounds.size() - 1); 

            // кожен блок src паралельно розбивається послідовним partition3
            runner.forEachBlock(sizes.size(), [&](std::size_t b) {
                const auto block = src.subspan(bounds[b], bounds[b + 1] - bounds[b]);
                const auto [lt, gt] = partition3(block, pivot); // розбиття в блоці 
                sizes[b] = { lt, gt - lt, block.size() - gt };
            });

            // групи <pivot =pivot >pivot паралельно збираються в dst
            const auto totals = scatterGroups<3>(std::span<const Edge<W>>(src), dst, bounds, sizes, runner);
            return { totals[0], totals[0] + totals[1] };
        }

        // Паралельний фільтр: кожен блок src фільтрується послідовним filterEdges з атомарним find,
        // потім залишені ребра збираються в dst підряд.
        template<typename Runner, EdgeWeight W>
        std::size_t parallelFilter(std::span<Edge<W>> src, std::span<Edge<W>> dst, Dsu& dsu,
                                   std::size_t blockSize, Runner& runner) {
            const std::vector<std::size_t> bounds = blockBounds(src.size(), blockSize);
            std::vector<std::array<std::size_t, 1>> sizes(bounds.size() - 1);

            runner.forEachBlock(sizes.size(), [&](std::size_t b) {
                const auto block = src.subspan(bounds[b], bounds[b + 1] - bounds[b]);
                sizes[b] = { filterEdges<true>(block, dsu) };
            });

            return scatterGroups<1>(std::span<const Edge<W>>(src), dst, bounds, sizes, runner)[0];
        }

        // Паралельний quicksort з використанням partition3 (без буферів)
        // перший поток робить повне розбиття на першому кроці
        template<EdgeWeight W>
        void parallelQuicksort(std::span<Edge<W>> edges, std::size_t sequentialCutoff) {
            if (edges.size() <= sequentialCutoff) {
                sortRange(edges.begin(), edges.end());
                return;
            }

            const W pivot = choosePivot<W>(edges);
            const auto [lt, gt] = partition3(edges, pivot);    
            const auto light = edges.first(lt);
            const auto heavy = edges.subspan(gt);

            tbb::task_group tg;
            tg.run([&] { parallelQuicksort(light, sequentialCutoff); });   // fork на легких 
            parallelQuicksort(heavy, sequentialCutoff);                    // compute на важких 
            tg.wait();                                                     // join
        }

        // Крок рекурсії паралельного Filter-Kruskal
        template<typename Runner, EdgeWeight W>
        bool parallelStep(std::span<Edge<W>> data, std::span<Edge<W>> scratch, const ParallelFilterParams& params,
                          Runner& runner, Dsu& dsu, MstResult<W>& result, std::size_t& sortedEdges) {
            if (dsu.componentCount() <= 1) {
                return true;
            }
            if (data.empty()) {
                return false;
            }

            // Базовий випадок: сортування (паралельне, якщо діапазон не менший за parallelSortCutoff) і uniteRange.
            if (data.size() <= params.threshold) {
                sortedEdges += data.size();
                if (params.parallelSortCutoff != 0 && data.size() >= params.parallelSortCutoff) {
                    runner.execute([&] { parallelQuicksort(data, params.quicksortCutoff); });
                }
                else {
                    sortRange(data.begin(), data.end());
                }
                return uniteRange(dsu, data.begin(), data.end(), result);
            }

            // Малий діапазон
            if (data.size() < params.parallelCutoff) {
                return filterKruskalStep(data, params.threshold, dsu, result, sortedEdges);
            }

            // Паралельне Розбиття
            const W pivot = choosePivot<W>(data);
            const auto [lightEnd, heavyBegin] = parallelPartition3(data, scratch, pivot, params.partitionBlock, runner);

            // Рекурсія в легші ребра (scratch має коректне розбиття, тому він перший)
            if (parallelStep(scratch.first(lightEnd), data.first(lightEnd), params, runner, dsu, result, sortedEdges)) {
                return true;
            }

            // Послідовна обробка рівних ребер
            const auto equal = scratch.subspan(lightEnd, heavyBegin - lightEnd);
            if (uniteRange(dsu, equal.begin(), equal.end(), result)) {
                return true;
            }

            // Паралельний фільтр в
            const auto heavy = scratch.subspan(heavyBegin);
            const auto heavyWork = data.subspan(heavyBegin);
            const std::size_t kept = parallelFilter(heavy, heavyWork, dsu, params.filterBlock, runner);
            return parallelStep(heavyWork.first(kept), heavy.first(kept), params, runner, dsu, result, sortedEdges);
        }

    } // namespace detail

    
    template<typename Runner, EdgeWeight W>
    [[nodiscard]] MstResult<W> filterKruskalParallelInPlace(Vertex vertexCount, std::span<Edge<W>> edges,
                                                            const ParallelFilterParams& params, Runner& runner,
                                                            FilterKruskalStats* stats = nullptr) {
        MstResult<W> result{};
        result.componentCount = vertexCount;
        if (stats) {
            *stats = {};
        }

        if (vertexCount == 0) {
            return result;
        }

        Dsu dsu(vertexCount);
        result.edges.reserve(static_cast<std::size_t>(vertexCount - 1));

        std::size_t sortedEdges = 0;
        if (edges.size() < params.parallelCutoff || edges.size() <= params.threshold) {
            // При цих умовах буфер не виділяємо, бо базовий випадок і послідовний Fk його не використовують
            detail::parallelStep(edges, std::span<Edge<W>>{}, params, runner, dsu, result, sortedEdges);
        }
        else {
            // Один буфер на весь алгоритм
            detail::UninitializedBuffer<Edge<W>> buffer(edges.size());
            detail::parallelStep(edges, buffer.span(), params, runner, dsu, result, sortedEdges);
        }

        result.componentCount = dsu.componentCount();
        if (stats) {
            stats->sortedEdges = sortedEdges;
        }
        return result;
    }

    template<EdgeWeight W>
    [[nodiscard]] MstResult<W> filterKruskalParallel(Graph<W> graph, const ParallelFilterParams& params,
                                                     FilterKruskalStats* stats = nullptr) {
        TbbRunner runner(params.threads);
        return filterKruskalParallelInPlace(graph.vertexCount, std::span<Edge<W>>(graph.edges), params, runner, stats);
    }

} // namespace kruskal
