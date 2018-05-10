#ifndef STM_H__
#define STM_H__

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/types.h>

#include <map>
#include <vector>

#define CFENCE __asm__ volatile("" ::: "memory")
#define MFENCE __asm__ volatile("mfence" ::: "memory")

#define IS_ODD(clock) ((clock & 1) != 0)
#define IS_EVEN(clock) ((clock & 1) == 0)

/**************************
 **** GLOBALS METADATA ****
 **************************/
extern volatile unsigned int global_clock;
extern volatile int total_threads;
extern std::vector<unsigned int> local_counters;

struct STM_EXCEPTION {}; // Empty struct for throwing in tx abort

class STM {
public:
  void tx_begin() {
    // Reset read and write sets
    read_set.clear();
    write_set.clear();
    snapshots.clear();
    // snapshots.resize();

    CFENCE;
    do {
      RV = global_clock;
      snapshots = local_counters;
    } while ((RV & 1) != 0);
    CFENCE;
  }

  void tx_write(int *address, int value) {
    // Update if address found in write set, else insert
    write_set[address] = value;
  }

  int tx_read(int *address) {
    // Find the address in the write-set
    if (write_set.count(address) > 0) {
      return write_set[address];
    }

    CFENCE;
    int value = *address;
    CFENCE;

    while (RV != global_clock) {
      RV = tx_validate();
      CFENCE;
      value = *address;
      CFENCE;
    }

    read_set[address] = value;
    return value;
  }

  void tx_validate_htm() {
    while (true) {
      snapshots = local_counters;

      for (auto &entry : read_set) {
        if (*entry.first != entry.second) {
          // *********************************************
          // ROLL BACK THE ACQUIRED (LOCK OF) GLOBAL CLOCK
          // *********************************************
          CFENCE;
          global_clock = RV;
          CFENCE;
          tx_abort();
        }
      }

      if (snapshots == local_counters) {
        return;
      }
    }
  }
  void tx_commit() {
    if (write_set.empty()) {
      return;
    }

    while (!(__sync_bool_compare_and_swap(&global_clock, RV, (RV + 1)))) {
      RV = tx_validate();
    }

    // Perform additional validation on all local counters
    /*NOrec’s SW COMMIT must also now
    perform additional validation after acquiring seqlock, in order to
    detect any hardware commit(s) that occurred before the CAS (Figure
    2, Line 22). This validation consists of reading the hardware
    counter, and performing value-based validation if it has changed.
    This extra validation adds serial overhead to software transactions*/
    //
    if (local_counters != snapshots) {
      tx_validate_htm();
    }

    // Write back
    for (auto &entry : write_set) {
      CFENCE;
      *entry.first = entry.second;
      CFENCE;
    }

    CFENCE;
    global_clock = RV + 2;
    CFENCE;
  }

  unsigned int tx_validate() {
    while (true) {
      CFENCE;
      unsigned int _time = __sync_fetch_and_add(&global_clock, 0);
      CFENCE;

      if (IS_ODD(_time)) {
        continue;
      }

      for (auto &entry : read_set) {
        if (*entry.first != entry.second) {
          tx_abort();
        }
      }

      if (_time == global_clock) {
        return _time;
      }
    }
  }

  void tx_abort() {
    STM_EXCEPTION e;
    throw e;
  }

private:
  unsigned int RV;
  std::vector<unsigned int> snapshots;
  std::map<int *, int> read_set;
  std::map<int *, int> write_set;
};

#endif // STM_H__