#!/bin/bash
mpicc -host -c master_tree_s.c 
sw5cc -slave -c slave_tree_s.c 
mpicc -hybrid master_tree_s.o slave_tree_s.o -o test 
bsub -I -b -perf -q q_sw_expr -n 1 -cgsp 64 -share_size 4096 ./test 
