# HyNOrec-Hybrid-Transactional-Memory-Algorithm
I did not know I wanted to live until I wanted to die
It took a while to understand the paper because of the special abort after acquiring lock in commit of NOrec.
I splitted into two (with and without read validate of the local counters) since I was not sure the paper meant the "polling" to include read validation. 
Results are seen in Assignment 7 excel spreadsheets. 

test_threads*.cpp is the normal run configurations. 

I created a bash script to run along with test_data*.cpp to create a result.csv,from which I copy into Assignment7*.xlsx Let me know which one is the correct implementation, as I spent a lot of times figuring out the p-counter in the paper, and it is still ambiguous to me.
