#include "db.hpp"
#include <cstdlib>
#include <cmath>

// YCSB template parameters
#define recordcount 100000
#define operationcount 5000000
// Percentage of data items that constitute the hot set
#define hotspotdatafraction .2
// Percentage of operations that access the hot set
#define hotspotopnfraction .8

DEFINE_bool(constant, false, "Whether to use the constant distribution");
// 1.0f: commpletely skewed
// 0.0f: constant
DEFINE_double( alpha, 0.01f, "Alpha of zipf distribution." );
DEFINE_double( read_propotion, 0.8f, "Read propotion" );

/*
 * GlobalAddress::pointer() returns valid pointer for owned objects. For
 * non-owned objects, the pointer is INVALID!
 */
enum db_op { READ, UPDATE, INSERT, REMOVE };

static double highest_prob = 0.0;
static double cul_prob[recordcount];
static std::vector<uint32_t> shuffled_map;

static db_op get_op(void) {
  double dice = rand() * 1.0 / RAND_MAX;
  if (dice < FLAGS_read_propotion) {
    return READ;
  }
  else {
    return UPDATE;
  }
}

static int get_idx(void) {
  double dice = rand() * 1.0 / RAND_MAX;

  if (FLAGS_constant) {
    return recordcount * dice;
  }
  else {
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
    return shuffled_map[idx];
  }
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
  double sum = 1;
  for (int i = 1; i < recordcount; i++) {
    sum += pow(i, -FLAGS_alpha);
  }
  highest_prob = cul_prob[0] = 1.0 / sum;
  for (int i = 1; i < recordcount; i++) {
    cul_prob[i] = cul_prob[i - 1] + pow(i, -FLAGS_alpha) / sum;
  }
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
  init( &argc, &argv );
  run([]{
    double begin_time = 0.0;

    db mydb;
    init_db(mydb);

    on_all_cores([] {
      srand(0);
      for (uint32_t i = 0; i < recordcount; i++) {
        shuffled_map.push_back(i);
      }
      std::random_shuffle(shuffled_map.begin(), shuffled_map.end());

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
      << cache_proto_str[FLAGS_cache_proto];
    global_free(mydb.data);

    Metrics::merge_and_dump_to_file();

  });
  finalize();
}
