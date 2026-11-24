#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "FilterKruskal.hpp"
#include "FilterKruskalParallel.hpp"
#include "Graph.hpp"
#include "GraphGenerator.hpp"
#include "Kruskal.hpp"
#include "KruskalParallel.hpp"

namespace kruskal::bench {

    using Weight = int;
    inline constexpr Weight kMinWeight = 1;
    inline constexpr Weight kMaxWeight = 100000;

    using Clock = std::chrono::steady_clock;

    inline double msSince(Clock::time_point start) {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }

    // Графи
    enum class Family { Sparse, Dense, Grid };

    inline const char* familyName(Family f) {
        switch (f) {
        case Family::Sparse: return "sparse";
        case Family::Dense:  return "dense";
        case Family::Grid:   return "grid";
        }
        return "unknown";
    }

    // Опис одного графа з плану експериментів
    struct GraphSpec {
        std::string id;                 // B1 ... B11, G1, C1 ... C7
        std::string series;             // density, grid, size
        Family family = Family::Sparse;
        Vertex vertexCount = 0;
        std::uint64_t edgeCount = 0;    // для решітки кількість ребер рахує генератор
        Vertex rows = 0;                // лише для решітки
        Vertex cols = 0;
        std::uint64_t seed = 0;
    };

    // Усі 19 графів
    inline std::vector<GraphSpec> allGraphs() {
        std::vector<GraphSpec> specs;
        std::uint64_t seed = 42;

        // Щільність: V = 8000, E/V = 2, 4, ..., 1024, і повний гарф
        const Vertex densityV = 8000;
        for (int k = 1; k <= 10; ++k) {
            const std::uint64_t ratio = std::uint64_t{1} << k;
            specs.push_back({"B" + std::to_string(k), "density", Family::Dense,
                              densityV, ratio * densityV, 0, 0, seed++ });
        }
        specs.push_back({"B11", "density", Family::Dense,
                          densityV, std::uint64_t{densityV} * (densityV - 1) / 2, 0, 0, seed++ });

        // Решітка 2000 x 2000
        specs.push_back({"G1", "grid", Family::Grid, 2000u * 2000u, 0, 2000, 2000, seed++});

        // Фіксований E/V = 8
        const Vertex sizes[] = {10000, 30000, 100000, 300000, 1000000, 3000000, 10000000};
        int index = 1;
        for (const Vertex v : sizes) {
            specs.push_back({"C" + std::to_string(index++), "size", Family::Sparse,
                              v, std::uint64_t{8} * v, 0, 0, seed++ });
        }
        return specs;
    }

    inline Graph<Weight> makeGraph(const GraphSpec& s) {
        switch (s.family) {
            case Family::Sparse:
                return generateSparseGraph(GraphParams<Weight>{
                    .vertexCount = s.vertexCount, .edgeCount = s.edgeCount, .seed = s.seed,
                    .minWeight = kMinWeight, .maxWeight = kMaxWeight });
            case Family::Dense:
                return generateDenseGraph(GraphParams<Weight>{
                    .vertexCount = s.vertexCount, .edgeCount = s.edgeCount, .seed = s.seed,
                    .minWeight = kMinWeight, .maxWeight = kMaxWeight });
            case Family::Grid:
                return generateGridGraph(GridGraphParams<Weight>{
                    .rows = s.rows, .cols = s.cols, .seed = s.seed,
                    .minWeight = kMinWeight, .maxWeight = kMaxWeight });
        }
        throw std::logic_error("makeGraph: unknown family");
    }

    // Значення параметрів однієї конфігурації
    struct AlgorithmParams {
        std::optional<unsigned> threads;
        std::optional<std::size_t> threshold;
        std::optional<std::size_t> partitionBlock;       // лише для fk-par
        std::optional<std::size_t> filterBlock;
        std::optional<std::size_t> parallelCutoff;
        std::optional<std::size_t> parallelSortCutoff;   // 0 — паралельне сортування вимкнено
        std::optional<std::size_t> quicksortCutoff;      // шматки, не більші за це, паралельний quicksort сортує послідовно
    };

    // Результат одного запуску
    struct Measurement {
        double totalMs = 0;
        double sortMs = -1;                          // -1 значить що виміру часу окремої фази алгоритму нема
        double dsuMs = -1;
        std::optional<std::size_t> sortedEdges;      // лише для Filter-Kruskal
        WeightSum<Weight> totalWeight = 0;
        Vertex componentCount = 0;
    };

    using RunFunction = Measurement (*)(const Graph<Weight>&, const AlgorithmParams&);

    // Послідовний Крускал
    inline Measurement runKruskal(const Graph<Weight>& graph, const AlgorithmParams&) {
        double sortMs = 0;
        double dsuMs = 0;
        MstResult<Weight> result;

        const auto totalStart = Clock::now();
        {
            std::vector<Edge<Weight>> edges = graph.edges;   // копія

            const auto sortStart = Clock::now();
            sortEdges(edges);
            sortMs = msSince(sortStart);

            const auto dsuStart = Clock::now();
            result = kruskalFromSorted(graph.vertexCount, std::span<const Edge<Weight>>(edges));
            dsuMs = msSince(dsuStart);
        }   // тут звільняється копія ребер
        const double totalMs = msSince(totalStart);

        return { .totalMs = totalMs, .sortMs = sortMs, .dsuMs = dsuMs, .sortedEdges = std::nullopt,
                 .totalWeight = result.totalWeight, .componentCount = result.componentCount };
    }

    // kruskalParallel (parallelSort + kruskalFromSorted)
    inline Measurement runKruskalParallel(const Graph<Weight>& graph, const AlgorithmParams& params) {
        double sortMs = 0;
        double dsuMs = 0;
        MstResult<Weight> result;

        const auto totalStart = Clock::now();
        {
            std::vector<Edge<Weight>> edges = graph.edges;   // копія

            const unsigned threads = params.threads.value();

            const auto sortStart = Clock::now();

            kruskal::detail::UninitializedBuffer<Edge<Weight>> buffer(threads > 1 ? edges.size() : 0);
            const std::span<const Edge<Weight>> sorted = parallelSort(std::span<Edge<Weight>>(edges), 
                buffer.span(), threads);
            sortMs = msSince(sortStart);

            const auto dsuStart = Clock::now();
            result = kruskalFromSorted(graph.vertexCount, sorted);
            dsuMs = msSince(dsuStart);
        }   // тут звільняється копія ребер
        const double totalMs = msSince(totalStart);

        return { .totalMs = totalMs, .sortMs = sortMs, .dsuMs = dsuMs, .sortedEdges = std::nullopt,
                 .totalWeight = result.totalWeight, .componentCount = result.componentCount };
    }

    // Послідовний Filter-Kruskal
    inline Measurement runFilterKruskal(const Graph<Weight>& graph, const AlgorithmParams& params) {
        FilterKruskalStats stats;

        const auto start = Clock::now();
        const MstResult<Weight> result = filterKruskal(graph, params.threshold.value(), &stats);
        const double totalMs = msSince(start);

        return { .totalMs = totalMs, .sortedEdges = stats.sortedEdges,
                 .totalWeight = result.totalWeight, .componentCount = result.componentCount };
    }

    // Паралельний Filter-Kruskal
    inline Measurement runFilterKruskalParallel(const Graph<Weight>& graph, const AlgorithmParams& params) {
        ParallelFilterParams prm;
        prm.threads            = params.threads.value();
        prm.threshold          = params.threshold.value();
        prm.partitionBlock     = params.partitionBlock.value();
        prm.filterBlock        = params.filterBlock.value();
        prm.parallelCutoff     = params.parallelCutoff.value();
        prm.parallelSortCutoff = params.parallelSortCutoff.value();
        prm.quicksortCutoff    = params.quicksortCutoff.value();
        FilterKruskalStats stats;

        const auto start = Clock::now();
        const MstResult<Weight> result = filterKruskalParallel(graph, prm, &stats);
        const double totalMs = msSince(start);

        return { .totalMs = totalMs, .sortedEdges = stats.sortedEdges,
                 .totalWeight = result.totalWeight, .componentCount = result.componentCount };
    }

    // Вид алгоритму: ім'я для --algs, які параметри він бере і як його запустити
    struct AlgorithmKind {
        std::string_view name;
        bool usesThreads = false;        // --threads
        bool usesThreshold = false;      // --threshold
        bool usesParallelParams = false; // --block / --partition-block / --filter-block, --par-cutoff, --psort-cutoff
        RunFunction run = nullptr;
    };

    inline constexpr AlgorithmKind kAlgorithmKinds[] = {
        { "kruskal",     false, false, false, runKruskal },
        { "kruskal-par", true,  false, false, runKruskalParallel },
        { "fk",          false, true,  false, runFilterKruskal },
        { "fk-par",      true,  true,  true,  runFilterKruskalParallel },
    };

    inline const AlgorithmKind* findAlgorithmKind(std::string_view name) {
        for (const AlgorithmKind& kind : kAlgorithmKinds) {
            if (kind.name == name) return &kind;
        }
        return nullptr;
    }

    // Одна конфігурація для замірів: вид алгоритму + конкретні значення параметрів
    struct Algorithm {
        const AlgorithmKind* kind = nullptr;
        AlgorithmParams params;

        Measurement run(const Graph<Weight>& graph) const { return kind->run(graph, params); }
        std::string name() const { return std::string(kind->name); }
        unsigned threads() const { return params.threads.value_or(1); }

        std::string label() const {
            std::string s = name();
            if (params.threads)   s += " p=" + std::to_string(*params.threads);
            if (params.threshold) s += " t=" + std::to_string(*params.threshold);
            if (params.partitionBlock) s += " pb=" + std::to_string(*params.partitionBlock);
            if (params.filterBlock)    s += " fb=" + std::to_string(*params.filterBlock);
            if (params.parallelCutoff) s += " cut=" + std::to_string(*params.parallelCutoff);
            if (params.parallelSortCutoff && *params.parallelSortCutoff != 0) {
                s += " ps=" + std::to_string(*params.parallelSortCutoff);
                if (params.quicksortCutoff) s += " qs=" + std::to_string(*params.quicksortCutoff);
            }
            return s;
        }
    };

    // Списки значень параметрів з командного рядка
    struct ParameterLists {
        std::vector<unsigned> threads{ 1, 2, 4, 6, 8, 12, 16 };
        std::vector<std::size_t> thresholds{ kDefaultFilterThreshold };

        // Для fk-par. Значення за замовчуванням беруться з ParallelFilterParams.
        // blockSizes — пари (розмір блоку розбиття, розмір блоку фільтра)
        std::vector<std::pair<std::size_t, std::size_t>> blockSizes{
            { ParallelFilterParams{}.partitionBlock, ParallelFilterParams{}.filterBlock } };
        std::vector<std::size_t> parallelCutoffs{ ParallelFilterParams{}.parallelCutoff };
        std::vector<std::size_t> parallelSortCutoffs{ ParallelFilterParams{}.parallelSortCutoff };
        std::vector<std::size_t> quicksortCutoffs{ ParallelFilterParams{}.quicksortCutoff };
    };

    // Усі конфігурації одного виду
    inline std::vector<Algorithm> expandAlgorithm(const AlgorithmKind& kind, const ParameterLists& lists) {
        std::vector<std::optional<unsigned>> threads{ std::nullopt };
        std::vector<std::optional<std::size_t>> thresholds{ std::nullopt };
        std::vector<std::optional<std::pair<std::size_t, std::size_t>>> blockSizes{ std::nullopt };
        std::vector<std::optional<std::size_t>> parallelCutoffs{ std::nullopt };
        std::vector<std::optional<std::size_t>> parallelSortCutoffs{ std::nullopt };
        std::vector<std::optional<std::size_t>> quicksortCutoffs{ std::nullopt };

        if (kind.usesThreads) {
            threads.assign(lists.threads.begin(), lists.threads.end());
        }
        if (kind.usesThreshold) {
            thresholds.assign(lists.thresholds.begin(), lists.thresholds.end());
        }
        if (kind.usesParallelParams) {
            blockSizes.assign(lists.blockSizes.begin(), lists.blockSizes.end());
            parallelCutoffs.assign(lists.parallelCutoffs.begin(), lists.parallelCutoffs.end());
            parallelSortCutoffs.assign(lists.parallelSortCutoffs.begin(), lists.parallelSortCutoffs.end());
            quicksortCutoffs.assign(lists.quicksortCutoffs.begin(), lists.quicksortCutoffs.end());
        }

        std::vector<Algorithm> result;
        for (const auto& p : threads) {
            for (const auto& threshold : thresholds) {
                for (const auto& blocks : blockSizes) {
                    for (const auto& cutoff : parallelCutoffs) {
                        for (const auto& sortCutoff : parallelSortCutoffs) {
                            for (const auto& qsCutoff : quicksortCutoffs) {
                                AlgorithmParams params;
                                params.threads = p;
                                params.threshold = threshold;
                                if (blocks) {
                                    params.partitionBlock = blocks->first;
                                    params.filterBlock = blocks->second;
                                }
                                params.parallelCutoff = cutoff;
                                params.parallelSortCutoff = sortCutoff;
                                params.quicksortCutoff = qsCutoff;
                                result.push_back({ &kind, params });
                            }
                        }
                    }
                }
            }
        }
        return result;
    }

    // Перевірка після кожного запуску
    inline void checkResult(const GraphSpec& spec, const Algorithm& alg, const Measurement& m,
                            std::optional<WeightSum<Weight>>& reference) {
        if (m.componentCount != 1) {
            throw std::runtime_error(spec.id + " / " + alg.label() + ": result is not a spanning tree");
        }
        if (!reference) {
            reference = m.totalWeight;
        }
        else if (*reference != m.totalWeight) {
            throw std::runtime_error(spec.id + " / " + alg.label() + ": total weight differs from reference");
        }
    }

    // Кількість повторів визначається за часом який пішов на прогрівальний запуск
    inline int repeatsFor(double warmupMs, bool quick) {
        if (quick) return 3;
        if (warmupMs < 10) return 25;
        if (warmupMs < 100) return 20;
        if (warmupMs < 5000) return 7;
        return 6;
    }

    inline double median(std::vector<double> v) {
        if (v.empty()) return 0;
        std::sort(v.begin(), v.end());
        const std::size_t n = v.size();
        return n % 2 == 1 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
    }

    inline double mean(const std::vector<double>& v) {
        if (v.empty()) return 0.0;
        return std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
    }

    class CsvWriter {
    public:
        explicit CsvWriter(const std::string& path) : out_(path) {
            if (!out_) {
                throw std::runtime_error("Cannot open " + path);
            }
            out_ << std::fixed << std::setprecision(4);
            out_ << "series,graph,family,algorithm,threads,threshold,"
                    "partition_block,filter_block,par_cutoff,psort_cutoff,qsort_cutoff,V,E,E_over_V,seed,repeat,"
                    "total_ms,sort_ms,dsu_ms,sorted_edges,total_weight,components\n";
        }

        void write(const GraphSpec& spec, const Graph<Weight>& graph, const Algorithm& alg,
                   int repeat, const Measurement& m) {
            const double ratio = static_cast<double>(graph.edges.size()) / graph.vertexCount;
            out_ << spec.series << ',' << spec.id << ',' << familyName(spec.family) << ','
                 << alg.name() << ',' << alg.threads() << ',';
            writeOptional(alg.params.threshold);
            out_ << ',';
            writeOptional(alg.params.partitionBlock);
            out_ << ',';
            writeOptional(alg.params.filterBlock);
            out_ << ',';
            writeOptional(alg.params.parallelCutoff);
            out_ << ',';
            writeOptional(alg.params.parallelSortCutoff);
            out_ << ',';
            writeOptional(alg.params.quicksortCutoff);
            out_ << ',' << graph.vertexCount << ',' << graph.edges.size() << ',' << ratio << ','
                 << spec.seed << ',' << repeat << ','
                 << m.totalMs << ',';
            writeOptional(m.sortMs);
            out_ << ',';
            writeOptional(m.dsuMs);
            out_ << ',';
            writeOptional(m.sortedEdges);
            out_ << ',' << m.totalWeight << ',' << m.componentCount << '\n';
        }

        void flush() {
            out_.flush();
            if (!out_) {
                throw std::runtime_error("CSV write failed");
            }
        }

    private:
        void writeOptional(double value) {
            if (value >= 0) out_ << value;  
        }

        template<typename T>
        void writeOptional(const std::optional<T>& value) {
            if (value) out_ << *value;       
        }

        std::ofstream out_;
    };

} // namespace kruskal::bench
