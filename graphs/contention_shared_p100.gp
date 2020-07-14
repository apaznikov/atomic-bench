#set term pngcairo transparent enhanced font "Times,26" size 1200,800
set term pngcairo enhanced font "Times New Roman,24" size 1200,800
#set term pngcairo enhanced font "Cantarell,24" size 1200,800
#set term pngcairo enhanced font "Liberation Serif,24" size 1200,800
set xlabel "Number of threads" 
set ylabel "Latency [ns]" 
set output "img/contention_shared_p100.png"
set key inside top left width 2 maxrows 3 box
#set key outside bmargin nobox 
#set nokey

set xrange [ 1 : 100 ] noreverse writeback

set border lw 3
set grid lw 2.5
set pointsize 1.0

plot "./data/contention_shared-CAS-p100.dat" using 1:2 \
     ti "CAS" with lp lw 3 pt 5 lc rgb '#C40D28', \
     \
     "./data/contention_shared-unCAS-p100.dat" using 1:2 \
     ti "unCAS" with lp dt "_" lw 3 pt 9 lc rgb '#007BCC', \
     \
     "./data/contention_shared-SWAP-p100.dat" using 1:2 \
     ti "SWAP" with lp dt "_.." lw 3 pt 8 lc rgb '#d9138a', \
     \
     "./data/contention_shared-FAA-p100.dat" using 1:2 \
     ti "FAA" with lp dt "-_" lw 3 pt 2 lc rgb '#f3ca20', \
     \
     "./data/contention_shared-load-p100.dat" using 1:2 \
     ti "load" with lp dt "-." lw 3 pt 7 lc rgb '#500472', \
     \
     "./data/contention_shared-store-p100.dat" using 1:2 \
     ti "store" with lp dt "-" lw 3 pt 4 lc rgb '#ff6e40'
