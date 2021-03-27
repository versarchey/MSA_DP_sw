#!/bin/bash
mpicc -host -c master_tree.c 
sw5cc -slave -c slave_tree.c 
mpicc -hybrid master_tree.o slave_tree.o -o test 
bsub -I -b -perf -q q_sw_expr -n 4 -cgsp 64 -share_size 4096 ./test 
