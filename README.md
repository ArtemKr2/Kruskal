# Алгоритм Крускала

Курсова робота студента групи ІП-34 Кривіцького Артема на тему «Алгоритм Крускала».

Проєкт містить послідовні та паралельні реалізації алгоритму Крускала і Filter-Kruskal для побудови мінімального остовного дерева (лісу), модульні тести, програму для вимірювання часу виконання і ноутбуки з аналізом результатів.

## Структура проєкту

### Алгоритми — `include/`

Усі алгоритми реалізовані як заголовкові файли (header-only).

| Файл | Що містить |
|---|---|
| [`Graph.hpp`](include/Graph.hpp) | Ребро `Edge`, граф `Graph`, концепт типу ваги `EdgeWeight`, компаратор `ByWeight` |
| [`Dsu.hpp`](include/Dsu.hpp) | Система неперетинних множин (DSU) зі стисненням шляхів і об'єднанням за рангом; варіант `find` для паралельного фільтра |
| [`Kruskal.hpp`](include/Kruskal.hpp) | Послідовний алгоритм Крускала: `kruskalSequential`, спільний прохід по DSU `uniteRange`, `kruskalFromSorted` |
| [`KruskalParallel.hpp`](include/KruskalParallel.hpp) | Паралельний алгоритм Крускала: паралельне сортування злиттям з розбиттям Merge Path (`parallelSort`), `kruskalParallel` |
| [`FilterKruskal.hpp`](include/FilterKruskal.hpp) | Послідовний Filter-Kruskal: вибір опорного елемента, тришляхове розбиття, фільтр ребер, `filterKruskal` |
| [`FilterKruskalParallel.hpp`](include/FilterKruskalParallel.hpp) | Паралельний Filter-Kruskal на пулі задач TBB: паралельні розбиття і фільтр, паралельне швидке сортування базових випадків, параметри `ParallelFilterParams`, `filterKruskalParallel` |
| [`UninitializedBuffer.hpp`](include/UninitializedBuffer.hpp) | Буфер без ініціалізації елементів, спільний для паралельних алгоритмів |
| [`GraphGenerator.hpp`](include/GraphGenerator.hpp) | Генератори випадкових графів: розріджений, щільний, решітка |
| [`GraphIO.hpp`](include/GraphIO.hpp) | Запис і читання графа у бінарному форматі |
| [`GraphUtils.hpp`](include/GraphUtils.hpp) | Допоміжні перевірки для тестів: коректність, простота і зв'язність графа |
| [`Benchmark.hpp`](include/Benchmark.hpp) | Набір тестових графів (B1–B11, G1, C1–C7), запуск алгоритмів, вимірювання часу, запис результатів у CSV |

### Програма для вимірювань — `KruskalApp/`

[`KruskalApp.cpp`](KruskalApp/KruskalApp.cpp) — консольна програма, яка генерує набір графів, запускає обрані алгоритми з різними параметрами, перевіряє однаковість ваги остову і записує час виконання у CSV-файл. Список параметрів виводиться з `--help`.

### Тести — `KruskalTests/`

Модульні тести на GoogleTest:

| Файл | Що перевіряє |
|---|---|
| [`DsuTests.cpp`](KruskalTests/DsuTests.cpp) | Систему неперетинних множин |
| [`KruskalTests.cpp`](KruskalTests/KruskalTests.cpp) | Послідовний алгоритм Крускала |
| [`KruskalParallelTests.cpp`](KruskalTests/KruskalParallelTests.cpp) | Паралельне сортування і паралельний алгоритм Крускала |
| [`FilterKruskalTests.cpp`](KruskalTests/FilterKruskalTests.cpp) | Послідовний Filter-Kruskal |
| [`FilterKruskalParallelTests.cpp`](KruskalTests/FilterKruskalParallelTests.cpp) | Паралельний Filter-Kruskal та його частини |
| [`GraphGeneratorTests.cpp`](KruskalTests/GraphGeneratorTests.cpp) | Генератори графів |
| [`GraphIOTests.cpp`](KruskalTests/GraphIOTests.cpp) | Запис і читання графів |
| [`GraphUtilsTests.cpp`](KruskalTests/GraphUtilsTests.cpp) | Допоміжні перевірки графів |

### Аналіз результатів — ноутбуки Jupyter

| Ноутбук | Що аналізує |
|---|---|
| [`Compare_kruskal_and_fk.ipynb`](Compare_kruskal_and_fk.ipynb) | Порівняння послідовних алгоритмів Крускала і Filter-Kruskal |
| [`Choose_pivot_threshold.ipynb`](Choose_pivot_threshold.ipynb) | Вибір опорного елемента і порогу базового випадку послідовного Filter-Kruskal |
| [`Choose_parallelCutoff.ipynb`](Choose_parallelCutoff.ipynb), [`Choose_final_parallelCutoff.ipynb`](Choose_final_parallelCutoff.ipynb) | Вибір порогу паралельності паралельного Filter-Kruskal |
| [`Choose_blocks_size.ipynb`](Choose_blocks_size.ipynb), [`Choose_blocks_sizes.ipynb`](Choose_blocks_sizes.ipynb) | Вибір розмірів блоків паралельного розбиття і фільтра |
| [`Choose_psort_threshold.ipynb`](Choose_psort_threshold.ipynb) | Поріг базового випадку і паралельне сортування базових випадків |
| [`Choose_psort_cutoff_qsort_cutoff.ipynb`](Choose_psort_cutoff_qsort_cutoff.ipynb) | Параметри паралельного швидкого сортування |

Серії експериментів з паралельними алгоритмами (серія 1 — вплив обсягу даних, серія 2 — вплив кількості потоків):

| Ноутбук | Що аналізує |
|---|---|
| [`Series1_2_kruskal_par.ipynb`](Series1_2_kruskal_par.ipynb) | Паралельний алгоритм Крускала: прискорення й ефективність за кількістю потоків, прискорення залежно від розміру графа, порівняння з межею за законом Амдала, розподіл часу між сортуванням і проходом по DSU |
| [`Series1_2_fk_par.ipynb`](Series1_2_fk_par.ipynb) | Паралельний Filter-Kruskal, варіанти A (поріг 8192, послідовне сортування базових випадків) і B (поріг 262144, паралельне сортування): прискорення й ефективність за кількістю потоків і залежно від розміру графа відносно найкращого послідовного Filter-Kruskal |

Ноутбуки читають CSV-файли з папки `results/`, яка не входить до репозиторію: її створює `KruskalApp` під час вимірювань.

## Збірка

- Visual Studio 2022, стандарт C++20, платформа x64.
- Бібліотека oneTBB підключається через vcpkg у режимі маніфесту ([`vcpkg.json`](vcpkg.json)), потрібна інтеграція vcpkg з Visual Studio.
- GoogleTest підключається як пакет NuGet ([`KruskalTests/packages.config`](KruskalTests/packages.config)) і відновлюється автоматично.
- Для вимірювань використовується конфігурація Release.

## Приклади запуску

```
x64\Release\KruskalApp.exe --algs kruskal,fk --threshold 8192 --out results\seq.csv
x64\Release\KruskalApp.exe --algs kruskal,kruskal-par --threads 1,2,4,6,8,12,16 --out results\kpar.csv
x64\Release\KruskalApp.exe --algs fk,fk-par --threads 1,2,4,6,8,12,16 --threshold 8192,262144 --psort-cutoff 32768 --out results\fkpar.csv
```

Перший алгоритм у `--algs` є еталоном для обчислення прискорення і перевірки ваги остову.
