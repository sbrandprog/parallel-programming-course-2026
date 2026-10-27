lab2_rel := "out/build/linux-release/lab2"
lab2_tsan := "out/build/linux-tsan/lab2"

# Этап 0: стресс-тест, по 3 прогона каждой конфигурации
run_lab2_0: build_release
    for i in 1 2 3; do {{lab2_rel}}/stress 10000 none v1; done
    for i in 1 2 3; do {{lab2_rel}}/stress 10000 yield v1; done
    for i in 1 2 3; do {{lab2_rel}}/stress 10000 none v2; done

# Этапы 1–2: FirstAlive, SchedLock, transfer
run_lab2_1: build_release
    {{lab2_rel}}/lab2 withdraw-v0 first log
    {{lab2_rel}}/lab2 withdraw-v1 first log
    {{lab2_rel}}/lab2 transfer first
    {{lab2_rel}}/lab2 transfer replay AB log

# Этап 2: TSan (должно быть 0 предупреждений)
run_lab2_2_tsan: build_tsan
    {{lab2_tsan}}/lab2 withdraw-v1 first
    {{lab2_tsan}}/lab2 transfer first
    {{lab2_tsan}}/lab2 transfer replay AB

# Этап 3: Random, 1000 seed'ов
run_lab2_3: build_release
    {{lab2_rel}}/lab2 withdraw-v1 randoms 1000

# Этап 3: воспроизвести трассу из лога, например: just replay withdraw-v1 BBAABBAA
replay target trace: build_release
    {{lab2_rel}}/lab2 {{target}} replay {{trace}} log

# Этап 4: полный перебор DFS
run_lab2_4: build_release
    {{lab2_rel}}/lab2 withdraw-v1 dfs
    {{lab2_rel}}/lab2 withdraw-v2 dfs
    {{lab2_rel}}/lab2 withdraw-v0 dfs
    {{lab2_rel}}/lab2 transfer dfs

# Этап 4: TSan (должно быть 0 предупреждений)
run_lab2_4_tsan: build_tsan
    {{lab2_tsan}}/lab2 withdraw-v1 dfs
    {{lab2_tsan}}/lab2 withdraw-v2 dfs
    {{lab2_tsan}}/lab2 withdraw-v0 dfs
    {{lab2_tsan}}/lab2 transfer dfs

# Этап 5: bounded
run_lab2_5: build_release
    {{lab2_rel}}/lab2 withdraw-v1 bounded 0
    {{lab2_rel}}/lab2 withdraw-v1 bounded 1
    {{lab2_rel}}/lab2 withdraw-v1 bounded 2
    {{lab2_rel}}/lab2 transfer bounded 0
    {{lab2_rel}}/lab2 transfer bounded 1

# Этап 6а: поиск бага в коллекторе
run_lab2_6a: build_release
    {{lab2_rel}}/lab2 collector-nostep3-1x1 dfs

# Этап 6б: рост пространства состояний
run_lab2_6b: build_release
    {{lab2_rel}}/lab2 collector-nostep3-1x2 dfs
    {{lab2_rel}}/lab2 collector-nostep3-1x3 dfs
    {{lab2_rel}}/lab2 collector-nostep3-1x4 dfs
    {{lab2_rel}}/lab2 collector-nostep3-2x1 bounded 0
    {{lab2_rel}}/lab2 collector-nostep3-2x1 bounded 1
    {{lab2_rel}}/lab2 collector-nostep3-2x1 bounded 2
    {{lab2_rel}}/lab2 collector-nostep3-2x1 bounded 3

# Этап 6в: границы применимости
run_lab2_6c: build_release
    {{lab2_rel}}/lab2 collector-plain-1x1 dfs
    {{lab2_rel}}/lab2 collector-acqrel-1x1 dfs
    {{lab2_rel}}/lab2 collector-ok-1x1 dfs

# Этап 6: контрольная точка (ожидается "explored 1334 schedules, 0 fail")
run_lab2_6_check: build_release
    {{lab2_rel}}/lab2 collector-ok-2x1 bounded 2

# Этап 7: mini-jcstress
run_lab2_7: build_release
    {{lab2_rel}}/ministress relaxed 2000 10000
    for i in 1 2 3; do {{lab2_rel}}/ministress acqrel 2000 10000; done
    {{lab2_rel}}/ministress seqcst 2000 10000
    for i in 1 2 3; do {{lab2_rel}}/ministress naive 200000 1; done

# Этап 7.1: размер пачки
run_lab2_7_batch: build_release
    {{lab2_rel}}/ministress acqrel 200000 100
    {{lab2_rel}}/ministress acqrel 200 100000

run_lab2_all: run_lab2_0 run_lab2_1 run_lab2_2_tsan run_lab2_3 run_lab2_4 run_lab2_4_tsan run_lab2_5 run_lab2_6a run_lab2_6b run_lab2_6c run_lab2_6_check run_lab2_7 run_lab2_7_batch