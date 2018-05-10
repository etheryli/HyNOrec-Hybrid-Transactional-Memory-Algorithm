#!/bin/bash
g++ -o test test_data_with_read_validate.cpp -lpthread -std=c++11
rm -f result_1k.csv result_1k_disjoint.csv result_1mil.csv result_1mil_disjoint.csv
for i in 1, 4, 8, 16, 24, 32, 48, 56, 64;
do
    echo $i;
    ./test $i 1000000 >> result_1mil.csv;
    ./test $i 1000000 -d >> result_1mil_disjoint.csv;
    ./test $i 1000 >> result_1k.csv;
    ./test $i 1000 -d >> result_1k_disjoint.csv;
done
printf "1 mil Accounts\n# Threads, Time (ns), inHTMs, inSTMs\n" > result.csv
cat result_1mil.csv >> result.csv
printf "\n1 mil Accounts with Disjoint Access\n# Threads, Time (ns), inHTMs, inSTMs\n" >> result.csv
cat result_1mil_disjoint.csv >> result.csv
printf "\n1 k Accounts\n# Threads, Time (ns), inHTMs, inSTMs\n" >> result.csv
cat result_1k.csv >> result.csv
printf "\n1 k Accounts with Disjoint Access\n# Threads, Time (ns), inHTMs, inSTMs\n" >> result.csv
cat result_1k_disjoint.csv >> result.csv
rm -f result_1k.csv result_1k_disjoint.csv result_1mil.csv result_1mil_disjoint.csv