# Plot operations-per-second vs number of threads from table.txt
# Usage: gnuplot plot.gnuplot

set datafile separator "\t"
set autoscale fix
set key outside right center
set grid
set xtics (1,2,4,8)
set xlabel "Number of threads"
set xrange [0.8:9.5]

set style line 1 lc rgb "#d62728" lw 2 pt 7 ps 1.2
set style line 2 lc rgb "#ff7f0e" lw 2 pt 9 ps 1.2
set style line 3 lc rgb "#2ca02c" lw 2 pt 11 ps 1.2
set style line 4 lc rgb "#1f77b4" lw 2 pt 5 ps 1.2
set style line 5 lc rgb "#9467bd" lw 2 pt 13 ps 1.2
set style data linespoints

# Baseline: single-threaded synchronized throughput
BASELINE = 5.43564e8
set style line 6 lc rgb "#000000" lw 2 dt 2
set style arrow 1 nohead ls 6

# --- linear scale ---
set terminal pngcairo size 1300,900 enhanced font "DejaVu Sans,11"
set output "plot.png"
set multiplot layout 2,1 title "Throughput vs number of threads" font ",14"

set title "Linear scale" font ",12"
set ylabel "Operations per second"
unset logscale y
plot BASELINE ls 6 title "Synchronous", \
     "table.txt" using 1:2 ls 1 title "Synchronized", \
     "table.txt" using 1:3 ls 2 title "Synchronized Empty", \
     "table.txt" using 1:4 ls 3 title "Sharded", \
     "table.txt" using 1:5 ls 4 title "Thread Local", \
     "table.txt" using 1:6 ls 5 title "DB Thread Local"

set title "Logarithmic scale" font ",12"
set ylabel "Operations per second (log)"
set logscale y
set format y "10^{%T}"
plot BASELINE ls 6 title "Synchronous", \
     "table.txt" using 1:2 ls 1 title "Synchronized", \
     "table.txt" using 1:3 ls 2 title "Synchronized Empty", \
     "table.txt" using 1:4 ls 3 title "Sharded", \
     "table.txt" using 1:5 ls 4 title "Thread Local", \
     "table.txt" using 1:6 ls 5 title "DB Thread Local"

unset multiplot

# --- SVG version (log scale, single panel) ---
set terminal svg size 1000,700 enhanced font "DejaVu Sans,11" dynamic
set output "plot.svg"
set multiplot layout 1,1
set title "Throughput vs number of threads (log scale)" font ",13"
set ylabel "Operations per second (log)"
set key inside left top
plot BASELINE ls 6 title sprintf("Baseline (sync, 1 thread) = %.3e", BASELINE), \
     "table.txt" using 1:2 ls 1 title "Synchronized", \
     "table.txt" using 1:3 ls 2 title "Synchronized Empty", \
     "table.txt" using 1:4 ls 3 title "Sharded", \
     "table.txt" using 1:5 ls 4 title "Thread Local", \
     "table.txt" using 1:6 ls 5 title "DB Thread Local"
unset multiplot
