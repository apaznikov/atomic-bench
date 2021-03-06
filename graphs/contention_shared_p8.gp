#set term pngcairo transparent enhanced font "Times,26" size 1200,800
set term pngcairo enhanced font "Times New Roman,24" size 1200,800
#set term pngcairo enhanced font "Cantarell,24" size 1200,800
#set term pngcairo enhanced font "Liberation Serif,24" size 1200,800
set xlabel "Number of threads" 
set ylabel "Latency [ns]" 
set output "img/contention_shared_p8.png"
set key inside top left width 2 maxrows 3 box
#set key outside bmargin nobox 
#set nokey

set xrange [ 1 : 8 ] noreverse writeback

set border lw 3
set grid lw 2.5
set pointsize 3.0

plot "./data/contention_shared-CAS.dat" using 1:2 \
     ti "CAS" with lp lw 4 pt 5 lc rgb '#C40D28', \
     \
     "./data/contention_shared-unCAS.dat" using 1:2 \
     ti "unCAS" with lp dt "_" lw 4 pt 9 lc rgb '#007BCC', \
     \
     "./data/contention_shared-SWAP.dat" using 1:2 \
     ti "SWAP" with lp dt "_.." lw 4 pt 8 lc rgb '#d9138a', \
     \
     "./data/contention_shared-FAA.dat" using 1:2 \
     ti "FAA" with lp dt "-_" lw 4 pt 2 lc rgb '#f3ca20', \
     \
     "./data/contention_shared-load.dat" using 1:2 \
     ti "load" with lp dt "-." lw 4 pt 7 lc rgb '#500472', \
     \
     "./data/contention_shared-store.dat" using 1:2 \
     ti "store" with lp dt "-" lw 4 pt 4 lc rgb '#ff6e40'
