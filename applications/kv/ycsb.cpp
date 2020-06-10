#include "db.hpp"
#include <cstdlib>

// YCSB template parameters
#define recordcount 100
#define operationcount 50
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

static int failcount = 0;
static void execute_operation(db db) {
  db_op op;
  bool hotop;
  double dice;
  record_field_t key, value;
  bool result;

  dice = rand() * 1.0 / RAND_MAX;
  if (dice < readproportion) {
    op = READ;
  }
  else {
    op = UPDATE;
  }
  dice = rand() * 1.0 / RAND_MAX;
  if (dice < hotspotopnfraction) {
    hotop = true;
  }
  else {
    hotop = false;
  }

  dice = rand() * 1.0 / RAND_MAX;
  switch (op) {
    case READ: {
      if (hotop) {
        int idx = recordcount * hotspotdatafraction * dice;
        db.generate_key(idx, key);
        result = db.read(key, value, idx);
        if (!result) {
          LOG(ERROR) << "Core " << Grappa::mycore() << " read " << idx << " failed. Hash:"
            << db.hash(key);
          failcount++;
        }
      }
      else {
        int idx = (recordcount - recordcount * hotspotdatafraction) * dice
          + recordcount * hotspotdatafraction;
        db.generate_key(idx, key);
        result = db.read(key, value, idx);
        if (!result) {
          LOG(ERROR) << "Core " << Grappa::mycore() << " read " << idx << " failed. Hash:"
            << db.hash(key);
          failcount++;
        }
      }
      break;
    }
    case UPDATE: {
      if (hotop) {
        int idx = recordcount * hotspotdatafraction * dice;
        db.generate_key(idx, key);
        result = db.update(key, value, idx);
        if (!result) {
          LOG(ERROR) << "Core " << Grappa::mycore() << " write " << idx << " failed. Hash:"
            << db.hash(key);
          failcount++;
        }
      }
      else {
        int idx = (recordcount - recordcount * hotspotdatafraction) * dice
          + recordcount * hotspotdatafraction;
        db.generate_key(idx, key);
        result = db.update(key, value, idx);
        if (!result) {
          LOG(ERROR) << "Core " << Grappa::mycore() << " write " << idx << " failed. Hash:"
            << db.hash(key);
          failcount++;
        }
      }
      break;
    }
  }
}

int main(int argc, char * argv[]) {
  init( &argc, &argv );
  run([]{
    Metrics::reset_all_cores();
    Metrics::start_tracing();

    srand(1);

    db mydb;
    init_db(mydb);

    record_field_t key, value;
    // Why can't we pass db the lambda expression?
    GlobalAddress<record> g = mydb.data;

    on_all_cores( [g] {
      db a(g);
      for (int i = 0; i < operationcount; i++) {
        execute_operation(a);
      }
      LOG(ERROR) << "Core " << Grappa::mycore() << " failure count:" << failcount;
    });
    global_free(mydb.data);

    Metrics::merge_and_dump_to_file();

  });
  finalize();
}
