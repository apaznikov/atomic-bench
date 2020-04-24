#!/bin/sh

for file in `ls data`; do
    echo $file
    cat data/$file | sort -n >data/$file.tmp
    mv data/$file.tmp data/$file
done
