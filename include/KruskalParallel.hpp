#pragma once

#include <algorithm>
#include <barrier>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>
#include "Graph.hpp"
#include "Kruskal.hpp"
#include "UninitializedBuffer.hpp"

namespace kruskal {

    namespace detail {
        // межі p кусків масиву довжини n, де кусок i це [bounds[i], bounds[i + 1])
        inline std::vector<std::size_t> chunkBounds(std::size_t n, unsigned p) {
            std::vector<std::size_t> bounds(static_cast<std::size_t>(p) + 1);
            for (unsigned i = 0; i <= p; ++i) {
                bounds[i] = n * i / p;
            }
            return bounds;
        }

        // двійковий пошук індекса початку зчитування
        // сусідні відсортовані частини одного масиву, які зільються, позначються як
        //   A = v[aBegin, bBegin),  B = v[bBegin, bEnd)
        template<EdgeWeight W>
        std::size_t mergePathSplit(std::span<const Edge<W>> v, std::size_t aBegin, 
            std::size_t bBegin, std::size_t bEnd, std::size_t k) {

            const std::size_t a = bBegin - aBegin;                                                            
            const std::size_t b = bEnd - bBegin;                                                             

            // з лівої частини можна взяти не менше k - b і не більше min(k, a)                              
            std::size_t lo = k > b ? k - b : 0; // випливає з властивості i + j = k та вимоги j <= b         
            std::size_t hi = std::min(k, a);                                                                 
            
            while (lo < hi) {
                const std::size_t i = lo + (hi - lo) / 2;   // беремо i з A
                const std::size_t j = k - i;                // j з B
                // i < a та j >= 1 завжди
                const Edge<W>& nextA = v[aBegin + i];       // перший невзятий з A
                const Edge<W>& lastB = v[bBegin + j - 1];   // останній взятий з B
                if (!ByWeight{}(lastB, nextA)) {
                    // lastB >= nextA, значить nextA мав іти раніше за lastB, тож з A треба брати більше.
                    lo = i + 1;
                }
                else {
                    hi = i;
                }
            }
            return lo;
        }

        // ітератор на елемент з номером i
        inline auto iterAt(auto& v, std::size_t i) {
            return v.begin() + static_cast<std::ptrdiff_t>(i);
        }

    } // namespace detail

    // паралельне сортування злиттям з Merge Path.
    template<EdgeWeight W>
    std::span<Edge<W>> parallelSort(std::span<Edge<W>> edges, std::span<Edge<W>> buffer, unsigned threads) {
        if (threads == 0) {
            throw std::invalid_argument("parallelSort: threads must be >= 1");
        }

        const unsigned p = threads;
        if (p > 1 && buffer.size() < edges.size()) {
            throw std::invalid_argument("parallelSort: buffer is smaller than edges");
        }
        const std::vector<std::size_t> chunks = detail::chunkBounds(edges.size(), p);
        bool resultInBuffer = false;
        std::barrier sync(static_cast<std::ptrdiff_t>(p));

        auto worker = [&](unsigned tid) {
            // позиції результату, куди пише тільки відповідний поток
            const std::size_t myBegin = chunks[tid];
            const std::size_t myEnd = chunks[tid + 1];

            // сортування свого куска
            sortRange(detail::iterAt(edges, myBegin), detail::iterAt(edges, myEnd));

            std::span<Edge<W>>* src = &edges;       // звідки читаємо в цьому раунді
            std::span<Edge<W>>* dst = &buffer;      // куди пишемо
            std::vector<std::size_t> runs = chunks; // межі відсортованих частин

            // усі потоки приймають участь в кожному раунді злиття
            while (runs.size() > 2) {
                sync.arrive_and_wait(); // бар'єр

                std::vector<std::size_t> next{0};
                for (std::size_t r = 0; r + 1 < runs.size(); r += 2) {
                    const std::size_t s = runs[r];
                    const std::size_t mid = runs[r + 1];
                    // шматок без пари зливається з порожнім, тобто просто копіюється
                    const std::size_t e = (r + 2 < runs.size()) ? runs[r + 2] : mid;
                    next.push_back(e);

                    // чи поточне злиття в діапазоні відповідальності потока
                    if (myBegin == myEnd || myBegin < s || myEnd > e) {
                        continue; // якщо ні шукаємо далі відповідний інтервал
                    }

                    const std::size_t k = myBegin - s;
                    const std::size_t i = detail::mergePathSplit(std::span<const Edge<W>>(*src), s, mid, e, k);

                    std::size_t aPos = s + i;
                    std::size_t bPos = mid + (k - i);
                    
                    // злиття розрізів в результуючі позиції
                    const Edge<W>* in = src->data();
                    Edge<W>* outData = dst->data();
                    for (std::size_t pos = myBegin; pos < myEnd; ++pos) {
                        if (bPos == e || (aPos < mid && !ByWeight{}(in[bPos], in[aPos]))) {
                            outData[pos] = in[aPos];
                            ++aPos;
                        }
                        else {
                            outData[pos] = in[bPos];
                            ++bPos;
                        }
                    }

                }

                std::swap(src, dst);
                runs = std::move(next);
            }

            if (tid == 0) {
                resultInBuffer = (src != &edges);
            }
        };

        {
            // Потік 0 вважається майстром, створює і запускає інші потоки, і також виконує роботу
            std::vector<std::jthread> pool;
            pool.reserve(p - 1);
            for (unsigned t = 1; t < p; ++t) {
                pool.emplace_back(worker, t);
            }
            worker(0);
        }   

        return resultInBuffer ? buffer.first(edges.size()) : edges;
    }

    // Крускал із паралельним сортуванням Merge Path і звичайним проходом по DSU
    template<EdgeWeight W>
    [[nodiscard]] MstResult<W> kruskalParallel(Graph<W> graph, unsigned threads) {
        detail::UninitializedBuffer<Edge<W>> buffer(threads > 1 ? graph.edges.size() : 0);   // додатковий буффер
        const std::span<const Edge<W>> sorted = parallelSort(std::span<Edge<W>>(graph.edges), buffer.span(), threads);
        return kruskalFromSorted(graph.vertexCount, sorted);
    }

} // namespace kruskal
