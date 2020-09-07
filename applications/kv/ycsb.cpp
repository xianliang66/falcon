#include "db.hpp"
#include <cstdlib>
#include <cmath>

//#define CONSTANT_DISTRIBUTION
#define ZIPF_DISTRIBUTION
#define ZIPF_ALPHA 0
// YCSB template parameters
#define recordcount 10000
#define operationcount 500000
#define readproportion .8
#define updateproportion (1 - readproportion)
// Percentage of data items that constitute the hot set
#define hotspotdatafraction .2
// Percentage of operations that access the hot set
#define hotspotopnfraction .8

/*
 * GlobalAddress::pointer() returns valid pointer for owned objects. For
 * non-owned objects, the pointer is INVALID!
 */
enum db_op { READ, UPDATE, INSERT, REMOVE };

static double highest_prob = 0.0;
static double cul_prob[recordcount];

static db_op get_op(void) {
  double dice = rand() * 1.0 / RAND_MAX;
  if (dice < readproportion) {
    return READ;
  }
  else {
    return UPDATE;
  }
}

static int get_idx(void) {
  double dice = rand() * 1.0 / RAND_MAX;

#ifdef CONSTANT_DISTRIBUTION
  return recordcount * dice;
#elif defined(ZIPF_DISTRIBUTION)
  int left = 0, right = recordcount - 1;
  int idx;
  int cnt = 0;

  if (dice < highest_prob) {
    return 0;
  }
  while (true) {
    cnt++;
    idx = (left + right) / 2;
    if (idx >= recordcount - 1)
      return recordcount - 1;
    if (dice > cul_prob[idx] && dice < cul_prob[idx + 1])
      break;
    if (dice > cul_prob[idx])
      left = idx;
    else
      right = idx;
  }
  return (idx * (recordcount / 10)) % recordcount;

#endif
}

static void init_db(db& db) {
  static_assert(SLOT_NUMBER >= 2 * recordcount, "Too many records!");

  db.data = global_alloc<record>(SLOT_NUMBER);
  record_field_t key, value;
  for (int i = 0; i < recordcount; i++) {
    db.generate_key(i, key);
    db.generate_value(value);
    bool r = db.insert(key, value, i);
    if (!r) {
      LOG(ERROR) << "Insert key " << i << " failed.";
    }
  }
}

static void init_cul_prob(void) {
#ifdef ZIPF_DISTRIBUTION
  double sum = 1;
  for (int i = 1; i < recordcount; i++) {
    sum += pow(i, -ZIPF_ALPHA);
  }
  highest_prob = cul_prob[0] = 1.0 / sum;
  for (int i = 1; i < recordcount; i++) {
    cul_prob[i] = cul_prob[i - 1] + pow(i, -ZIPF_ALPHA) / sum;
  }
#endif
}

static int failcount = 0;
static void execute_operation(db db) {
  db_op op;
  record_field_t key, value;
  bool result;

  op = get_op();

  switch (op) {
    case READ: {
      int idx = get_idx();
      db.generate_key(idx, key);
      result = db.read(key, value, idx);
      if (!result) {
        LOG(ERROR) << "Core " << Grappa::mycore() << " read " << idx << " failed. Hash:"
          << db.hash(key);
        failcount++;
      }
      break;
    }
    case UPDATE: {
      int idx = get_idx();
      db.generate_key(idx, key);
      result = db.update(key, value, idx);
      if (!result) {
        LOG(ERROR) << "Core " << Grappa::mycore() << " write " << idx << " failed. Hash:"
          << db.hash(key);
        failcount++;
      }
      break;
    }
  }
}

int main(int argc, char * argv[]) {
#ifndef SINGLE_TASK
  static_assert(false, "Please define SINGLE_TASK only in system/TardisCache.hpp");
#endif
  init( &argc, &argv );
  run([]{
    double begin_time = 0.0;

    db mydb;
    init_db(mydb);

    on_all_cores([] {
      srand(Grappa::mycore());
      init_cul_prob();
    });

    record_field_t key, value;
    // Why can't we pass db the lambda expression?
    GlobalAddress<record> g = mydb.data;

    Metrics::reset_all_cores();
    Metrics::start_tracing();

    begin_time = walltime();
    on_all_cores( [g] {
      db a(g);
      for (int i = 0; i < operationcount / Grappa::cores(); i++) {
        execute_operation(a);
      }
      if (failcount > 0) {
        LOG(ERROR) << "Core " << Grappa::mycore() << " failure count:" << failcount;
      }
    });
    LOG(ERROR) << "Time: " << walltime() - begin_time << "s. proto:"
      << GRAPPA_CC_PROTOCOL_NAME;
    global_free(mydb.data);

    Metrics::merge_and_dump_to_file();

  });
  finalize();
}
