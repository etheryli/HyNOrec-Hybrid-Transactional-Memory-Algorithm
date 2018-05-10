#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#include "norec_stm_with_read_validate.h"
#include "rand_r_32.h"
#include "rtm.h"
#include <errno.h>

#define CFENCE __asm__ volatile("" ::: "memory")
#define MFENCE __asm__ volatile("mfence" ::: "memory")

volatile unsigned int global_clock = 0;

std::vector<int *> accounts;
std::vector<unsigned int> local_counters;

volatile int total_threads;
volatile int total_accounts;
volatile bool disjointed = false;

unsigned long long inHTMs[300];
unsigned long long inSTMs[300];

inline unsigned long long get_real_time() {
  struct timespec time;
  clock_gettime(CLOCK_MONOTONIC_RAW, &time);

  return time.tv_sec * 1000000000L + time.tv_nsec;
}

/**
 *  Support a few lightweight barriers
 */
void barrier(int which) {
  static volatile int barriers[16] = {0};
  CFENCE;
  __sync_fetch_and_add(&barriers[which], 1);
  while (barriers[which] != total_threads) {
  }
  CFENCE;
}

void signal_callback_handler(int signum) {
  // Terminate program
  exit(signum);
}
/*
volatile bool ExperimentInProgress = true;
static void catch_SIGALRM(int sig_num) { ExperimentInProgress = false; }
*/
/*********************
 **** th_run *********
 *********************/

void *th_run(void *args) {
  long id = (long)args;

  // Divide the 100,000 transfers equally
  int thread_transfers_count = (100000 / total_threads);
  int accounts_per_threads = (total_accounts / total_threads);
  int disjoint_min = id * accounts_per_threads;

  if (!disjointed) {
    disjoint_min = 0;
    accounts_per_threads = total_accounts;
  }

  unsigned int seed = id;

  barrier(0);

  int inHTM = 0;
  int inSTM = 0;
  int acc1[1000];
  int acc2[1000];

  STM *s = new STM();
  bool sw_again = true;

  for (int i = 0; i < thread_transfers_count; i++) {
    // RANDOM numbers must be shared in case of fall back to STM
    for (int j = 0; j < 10; j++) {
      acc1[j] = (rand_r_32(&seed) % accounts_per_threads) + disjoint_min;
      acc2[j] = (rand_r_32(&seed) % accounts_per_threads) + disjoint_min;
    }

    int attempts = 5;
  again:
    unsigned int status = _xbegin();
    // Code for HTM path
    if (status == _XBEGIN_STARTED) {
      _hw_post_begin(global_clock);

      // TX's
      for (int j = 0; j < 10; j++) {
        if (*accounts[acc2[j] >= 50]) {
          *accounts[acc2[j]] -= 50;
          *accounts[acc1[j]] += 50;
        }
      }

      _hw_pre_commit(&local_counters[id]);
      _xend();
      inHTM++;

    } else if (attempts > 0) {
      attempts--;
      goto again;

    } else { // SW PATH///////////////
      sw_again = true;
      do {
        try {
          s->tx_begin();

          for (int j = 0; j < 10; j++) {
            if (s->tx_read(accounts[acc2[j]]) >= 50) {
              s->tx_write(accounts[acc2[j]],
                          s->tx_read(accounts[acc2[j]]) - 50);
              s->tx_write(accounts[acc1[j]],
                          s->tx_read(accounts[acc1[j]]) + 50);
            }
          }
          s->tx_commit();
          sw_again = false;
        } catch (STM_EXCEPTION e) {
          sw_again = true;
        }
      } while (sw_again);

      inSTM++;
    }
  }
  inHTMs[id] = inHTM;
  inSTMs[id] = inSTM;
  printf("Thread %ld local counter = %d and global counter = %d. In HTM "
         "= %d, "
         "in STM = %d\n",
         id, local_counters[id], global_clock, inHTM, inSTM);
  return 0;
}

int main(int argc, char *argv[]) {
  //	signal(SIGINT, signal_callback_handler);

  if (argc < 3 || argc > 4) {
    printf("Usage test <threads #> <accounts #> <-d>\n");
    exit(0);
  }

  total_threads = atoi(argv[1]);

  // Additional commandline argument for number of accounts and disjoint
  // flag
  total_accounts = atoi(argv[2]);
  if (total_accounts <= 0) {
    printf("total accounts out of range\n");
    exit(0);
  }

  accounts.resize(total_accounts);
  for (int i = 0; i < total_accounts; i++) {
    accounts[i] = new int(1000);
  }

  local_counters.resize(total_threads);
  for (int i = 0; i < total_threads; i++) {
    local_counters[i] = 0;
  }

  if (argc == 4)
    disjointed = (std::string(argv[3]) == "-d");

  pthread_attr_t thread_attr;
  pthread_attr_init(&thread_attr);

  pthread_t client_th[300];
  long ids = 1;
  for (int i = 1; i < total_threads; i++) {
    pthread_create(&client_th[ids - 1], &thread_attr, th_run, (void *)ids);
    ids++;
  }
  printf("Threads: %d created\n", total_threads);

  int start_sum = 0;
  for (int i = 0; i < total_accounts; i++) {
    start_sum += *accounts[i];
  }

  printf("Start total balance for %d accounts: $%d\n", total_accounts,
         start_sum);

  unsigned long long start = get_real_time();

  th_run(0);

  for (int i = 0; i < ids - 1; i++) {
    pthread_join(client_th[i], NULL);
  }

  printf("Total time = %lld ns\n", get_real_time() - start);

  // DEBUG for accounts changed
  int final_sum = 0;
  for (int i = 0; i < total_accounts; i++) {
    final_sum += *accounts[i];
  }

  if (final_sum != start_sum) {
    printf("FINAL SUM IS NOT EQUAL TO INITIAL SUM\n");
  }
  printf("Final total balance for %d accounts: $%d\n", total_accounts,
         final_sum);
  for (int i = 0; i < total_accounts; i++) {
    delete accounts[i];
  }

  // How many finished in STM and how many finished in HTM?
  // Number and percentage!

  unsigned long long totalHTMs = 0;
  unsigned long long totalSTMs = 0;
  for (int i = 0; i < total_threads; i++) {
    totalHTMs += inHTMs[i];
    totalSTMs += inSTMs[i];
  }

  // double percentageSTMs = totalSTMs / 100000;
  // double percentageHTMs = totalHTMs / 100000;

  printf("Total in HTMs: %llu, Total in STMs: %llu\n\n", totalHTMs, totalSTMs);
  return 0;
}

// Build with
// g++ test_threads.cpp -o test -lpthread

// Build with
// g++ FILE -o NAME -lpthread -std=c++11

// run with: ( -d = disjoint flag)
// NAME <threads #> <accounts #> <-d>
