#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "Benchmark.hpp"

using namespace kruskal;
using namespace kruskal::bench;

namespace {

    const char* kUsage =
        "Usage: KruskalApp [options]\n"
        "  --algs kruskal,kruskal-par,fk,fk-par   algorithms to run (default is kruskal);\n"
        "                                  the first one is the baseline for Sp and the weight check\n"
        "  --threads 1,2,4,6,8,12,16       thread counts for parallel algorithms\n"
        "  --threshold 4096,8192,16384     base case thresholds for fk and fk-par (default is 8192)\n"
        "  --block 4096,16384,65536        fk-par: same block size for partition and filter\n"
        "  --partition-block 65536         fk-par: partition block sizes (default is 65536)\n"
        "  --filter-block 16384            fk-par: filter block sizes (default is 16384)\n"
        "  --par-cutoff 262144             fk-par: smaller ranges are processed sequentially (default is 262144)\n"
        "  --psort-cutoff 0,32768          fk-par: base cases of this size and larger are sorted in parallel;\n"
        "                                  0 disables parallel sorting (default is 0)\n"
        "  --qsort-cutoff 4096,8192,16384  fk-par: parallel quicksort sorts pieces of this size and smaller\n"
        "                                  sequentially (default is 16384)\n"
        "  --graphs B10,G1,C6              only these graphs\n"
        "  --quick                         skip C6 and C7, 3 repeats\n"
        "  --repeats N                     fixed number of repeats instead of the automatic one\n"
        "  --out file.csv                  output file (default is results.csv)\n";

    struct Options {
        std::vector<std::string> algs{ "kruskal" };
        ParameterLists lists;
        bool threadsGiven = false;
        bool thresholdGiven = false;
        bool parallelParamsGiven = false;                       // будь-яка з опцій fk-par
        bool quicksortCutoffGiven = false;                      // --qsort-cutoff
        std::optional<std::vector<std::size_t>> blocks;          // --block
        std::optional<std::vector<std::size_t>> partitionBlocks; // --partition-block
        std::optional<std::vector<std::size_t>> filterBlocks;    // --filter-block
        std::set<std::string> graphs;
        bool quick = false;
        std::optional<int> repeats;
        std::string outPath = "results.csv";
    };

    std::vector<std::string> splitList(const std::string& text, const std::string& option) {
        std::vector<std::string> items;
        std::stringstream list(text);
        std::string item;
        while (std::getline(list, item, ',')) {
            if (!item.empty()) items.push_back(item);
        }
        if (items.empty()) {
            throw std::invalid_argument("empty list for " + option);
        }
        return items;
    }

    template<typename T>
    T parseNumber(const std::string& text, const std::string& option) {
        T value{};
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size()) {
            throw std::invalid_argument("bad number '" + text + "' for " + option);
        }
        return value;
    }

    template<typename T>
    std::vector<T> parseNumbers(const std::string& text, const std::string& option) {
        std::vector<T> values;
        for (const std::string& item : splitList(text, option)) {
            values.push_back(parseNumber<T>(item, option));
        }
        return values;
    }

    Options parseArgs(int argc, char** argv) {
        Options options;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            const bool hasValue = i + 1 < argc;

            if (arg == "--help" || arg == "-h") {
                std::cout << kUsage;
                std::exit(0);
            }
            else if (arg == "--quick") {
                options.quick = true;
            }
            else if (arg == "--algs" && hasValue) {
                options.algs = splitList(argv[++i], arg);
                for (const std::string& name : options.algs) {
                    if (!findAlgorithmKind(name)) {
                        std::string known;
                        for (const AlgorithmKind& kind : kAlgorithmKinds) {
                            known += ' ' + std::string(kind.name);
                        }
                        throw std::invalid_argument("unknown algorithm '" + name + "'. Known:" + known);
                    }
                }
            }
            else if (arg == "--threads" && hasValue) {
                options.lists.threads = parseNumbers<unsigned>(argv[++i], arg);
                if (std::find(options.lists.threads.begin(), options.lists.threads.end(), 0u) != options.lists.threads.end()) {
                    throw std::invalid_argument("--threads: thread count must be >= 1");
                }
                options.threadsGiven = true;
            }
            else if (arg == "--threshold" && hasValue) {
                options.lists.thresholds = parseNumbers<std::size_t>(argv[++i], arg);
                options.thresholdGiven = true;
            }
            else if ((arg == "--block" || arg == "--partition-block" || arg == "--filter-block") && hasValue) {
                auto values = parseNumbers<std::size_t>(argv[++i], arg);
                if (std::find(values.begin(), values.end(), std::size_t{ 0 }) != values.end()) {
                    throw std::invalid_argument(arg + ": block size must be >= 1");
                }
                (arg == "--block" ? options.blocks : arg == "--partition-block" ? 
                    options.partitionBlocks : options.filterBlocks) = std::move(values);
                options.parallelParamsGiven = true;
            }
            else if (arg == "--par-cutoff" && hasValue) {
                options.lists.parallelCutoffs = parseNumbers<std::size_t>(argv[++i], arg);
                options.parallelParamsGiven = true;
            }
            else if (arg == "--psort-cutoff" && hasValue) {
                options.lists.parallelSortCutoffs = parseNumbers<std::size_t>(argv[++i], arg);
                options.parallelParamsGiven = true;
            }
            else if (arg == "--qsort-cutoff" && hasValue) {
                options.lists.quicksortCutoffs = parseNumbers<std::size_t>(argv[++i], arg);
                if (std::find(options.lists.quicksortCutoffs.begin(), options.lists.quicksortCutoffs.end(),
                              std::size_t{ 0 }) != options.lists.quicksortCutoffs.end()) {
                    throw std::invalid_argument("--qsort-cutoff: value must be >= 1");
                }
                options.parallelParamsGiven = true;
                options.quicksortCutoffGiven = true;
            }
            else if ((arg == "--graphs" || arg == "--only") && hasValue) {
                for (const std::string& id : splitList(argv[++i], arg)) {
                    options.graphs.insert(id);
                }
            }
            else if (arg == "--repeats" && hasValue) {
                options.repeats = parseNumber<int>(argv[++i], arg);
                if (*options.repeats < 1) {
                    throw std::invalid_argument("--repeats must be >= 1");
                }
            }
            else if (arg == "--out" && hasValue) {
                options.outPath = argv[++i];
            }
            else {
                throw std::invalid_argument("unknown argument '" + arg + "'\n" + kUsage);
            }
        }

        // Розміри блоків fk-par: --block задає однаковий розмір для розбиття й фільтра,
        // --partition-block і --filter-block перебираються всіма комбінаціями.
        if (options.blocks) {
            if (options.partitionBlocks || options.filterBlocks) {
                throw std::invalid_argument("--block cannot be combined with --partition-block / --filter-block");
            }
            options.lists.blockSizes.clear();
            for (const std::size_t b : *options.blocks) {
                options.lists.blockSizes.push_back({ b, b });
            }
        }
        else if (options.partitionBlocks || options.filterBlocks) {
            const ParallelFilterParams defaults{};
            const auto pbs = options.partitionBlocks.value_or(std::vector<std::size_t>{ defaults.partitionBlock });
            const auto fbs = options.filterBlocks.value_or(std::vector<std::size_t>{ defaults.filterBlock });
            options.lists.blockSizes.clear();
            for (const std::size_t pb : pbs) {
                for (const std::size_t fb : fbs) {
                    options.lists.blockSizes.push_back({ pb, fb });
                }
            }
        }
        return options;
    }

    // валідація аргументів 
    void validate(const Options& options) {
        const auto specs = allGraphs();
        for (const std::string& id : options.graphs) {
            const bool known = std::any_of(specs.begin(), specs.end(),
                [&](const GraphSpec& s) { return s.id == id; });
            if (!known) throw std::invalid_argument("unknown graph id '" + id + "' in --graphs");
        }

        bool anyThreads = false;
        bool anyThreshold = false;
        bool anyParallelParams = false;
        for (const std::string& name : options.algs) {
            const AlgorithmKind* kind = findAlgorithmKind(name);
            anyThreads = anyThreads || kind->usesThreads;
            anyThreshold = anyThreshold || kind->usesThreshold;
            anyParallelParams = anyParallelParams || kind->usesParallelParams;
        }
        if (options.threadsGiven && !anyThreads) {
            std::cout << "WARNING: --threads is ignored, no selected algorithm uses it\n";
        }
        if (options.thresholdGiven && !anyThreshold) {
            std::cout << "WARNING: --threshold is ignored, no selected algorithm uses it\n";
        }
        if (options.parallelParamsGiven && !anyParallelParams) {
            std::cout << "WARNING: fk-par options are ignored, fk-par is not selected\n";
        }

       
        if (anyParallelParams) {
            bool sortNeverUsed = false;
            bool mixedBranches = false;
            for (const std::size_t t : options.lists.thresholds) {
                for (const std::size_t c : options.lists.parallelCutoffs) {
                    for (const std::size_t ps : options.lists.parallelSortCutoffs) {
                        sortNeverUsed = sortNeverUsed || (ps != 0 && ps > t);
                        mixedBranches = mixedBranches || (ps != 0 && ps <= t && c > t + 1);
                    }
                }
            }
            if (sortNeverUsed) {
                std::cout << "WARNING: psort-cutoff > threshold in some configurations: parallel sort will never be used there\n";
            }
            const bool anyParallelSort = std::any_of(options.lists.parallelSortCutoffs.begin(),
                options.lists.parallelSortCutoffs.end(), [](std::size_t ps) { return ps != 0; });
            if (options.quicksortCutoffGiven && !anyParallelSort) {
                std::cout << "WARNING: --qsort-cutoff is ignored, parallel sorting is disabled (psort-cutoff is 0)\n";
            }
            if (mixedBranches) {
                std::cout << "WARNING: psort-cutoff is set but par-cutoff > threshold in some configurations: "
                             "most base cases will be sorted sequentially inside the sequential Filter-Kruskal\n";
            }
        }
    }

    bool isSelected(const GraphSpec& spec, const Options& options) {
        if (!options.graphs.empty()) return options.graphs.contains(spec.id);
        if (options.quick) return spec.id != "C6" && spec.id != "C7";
        return true;
    }

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parseArgs(argc, argv);
        validate(options);

#ifndef NDEBUG
        std::cout << "WARNING: Debug build. Need to use Release.\n";
#endif

        // Усі конфігурації по порядку --algs. Перша є еталоном для Sp і перевірки ваги.
        std::vector<Algorithm> algorithms;
        for (const std::string& name : options.algs) {
            const auto expanded = expandAlgorithm(*findAlgorithmKind(name), options.lists);
            algorithms.insert(algorithms.end(), expanded.begin(), expanded.end());
        }

        std::size_t labelWidth = 0;
        for (const Algorithm& alg : algorithms) {
            labelWidth = std::max(labelWidth, alg.label().size());
        }
        const int width = static_cast<int>(labelWidth) + 2;

        std::cout << "Hardware threads: " << std::thread::hardware_concurrency() << '\n'
                  << "Output: " << options.outPath << (options.quick ? "  (quick mode)" : "") << '\n'
                  << "Configurations: " << algorithms.size()
                  << (algorithms.size() > 1 ? ", baseline: " + algorithms.front().label() : "") << "\n\n";

        CsvWriter csv(options.outPath);
        std::cout << std::fixed;

        for (const GraphSpec& spec : allGraphs()) {
            if (!isSelected(spec, options)) continue;

            std::cout << spec.id << " (" << familyName(spec.family) << ") generating... " << std::flush;
            const auto genStart = Clock::now();
            const Graph<Weight> graph = makeGraph(spec);
            std::cout << "V=" << graph.vertexCount << " E=" << graph.edges.size()
                      << "  [" << std::setprecision(1) << msSince(genStart) << " ms]\n";

            std::optional<WeightSum<Weight>> reference;
            double baselineMean = 0.0;

            for (std::size_t a = 0; a < algorithms.size(); ++a) {
                const Algorithm& alg = algorithms[a];

                // Прогрів
                const Measurement warmup = alg.run(graph);
                checkResult(spec, alg, warmup, reference);

                const int repeats = options.repeats.value_or(repeatsFor(warmup.totalMs, options.quick));
                std::vector<double> times;
                times.reserve(static_cast<std::size_t>(repeats));
                Measurement last = warmup;

                for (int r = 1; r <= repeats; ++r) {
                    last = alg.run(graph);
                    checkResult(spec, alg, last, reference);
                    csv.write(spec, graph, alg, r, last);
                    times.push_back(last.totalMs);
                }
                csv.flush();

                const double currentMean = mean(times);
                if (a == 0) {
                    baselineMean = currentMean;
                }

                std::cout << "    " << std::left << std::setw(width) << alg.label() << std::right
                          << "mean " << std::setw(9) << std::setprecision(2) << currentMean << " ms"
                          << "  (" << std::setw(2) << repeats << " runs)";

                if (a > 0 && currentMean > 0.0) {
                    const double sp = baselineMean / currentMean;
                    std::cout << "  Sp=" << std::setw(5) << std::setprecision(2) << sp;
                    if (alg.kind->usesThreads) {
                        std::cout << "  Ep=" << std::setw(4) << std::setprecision(2) << sp / alg.threads();
                    }
                }
                if (last.sortedEdges && !graph.edges.empty()) {
                    const double pct = 100.0 * static_cast<double>(*last.sortedEdges)
                                     / static_cast<double>(graph.edges.size());
                    std::cout << "  sorted=" << std::setprecision(1) << pct << '%';
                }
                std::cout << '\n';
            }
        }

        std::cout << "\nDone. Results: " << options.outPath << '\n';
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "\nERROR: " << e.what() << '\n';
        return 1;
    }
}
