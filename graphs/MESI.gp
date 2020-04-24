#set term pngcairo transparent enhanced font "Times,26" size 1200,800
#set term pngcairo enhanced font "Times New Roman,24" size 1200,800
#set term pngcairo enhanced font "Cantarell,24" size 1200,800
#set term pngcairo enhanced font "Liberation Serif,24" size 1200,800
#set style data histogram
#set style histogram cluster gap 1
#set style fill solid border -1
#set boxwidth 0.9
#set xtic rotate by -45 scale 0
#set bmargin 10
#set xlabel "Operation" 
set ylabel "Latency [ns]" 

set term pngcairo enhanced font "Times New Roman,24" size 1200,800

set style fill solid 1.00 border lt -1
#set key fixed left top vertical Right noreverse noenhanced autotitle nobox
set style increment default
set style histogram clustered gap 1 title textcolor lt -1
set datafile missing '-'
set style data histograms
set xtics border in scale 0,0 nomirror autojustify
set xtics  norangelimit
set xtics   ()

set yrange [ 0 : 110 ] noreverse writeback

set output "img/MESI.png"

#set key inside top left nobox
set key outside center bottom nobox maxrows 2 box width 3 
#set nokey

#set border lw 3
#set grid lw 2.5
#set pointsize 3.0

#plot 'test.dat' using 2:3:4:5 ti col, '' u 12 ti col, '' u 13 ti col, '' u 14 ti col

plot "data/MESI-M.dat" using 2:xtic(1) ti "M" fs pattern 7 lc rgb "#500472", \
     "data/MESI-E.dat" using 2:xtic(1) ti "E" fs pattern 2 lc rgb "#1b6535", \
     "data/MESI-S.dat" using 2:xtic(1) ti "S" fs pattern 6 lc rgb "#320d3e", \
     "data/MESI-I.dat" using 2:xtic(1) ti "I" fs pattern 1 lc rgb "#ed3572"

#plot "CAS.dat" using 2:xtic(1) ti "CAS", \
#     "unCAS.dat" using 2:xtic(1) ti "unCAS"
#     "SWAP.dat" using 1:2 ti "SWAP", \
#     "FAA.dat" using 1:2 ti "FAA", \
#     "load.dat" using 1:2 ti "load", \
#     "store.dat" using 1:2 ti "store"
