#include "db.hpp"
#include <cstdlib>

// YCSB template parameters
const int recordcount = 10000;
const int operationcount = 3000000;
const double readproportion = .95;
const double updateproportion = .05;
// Percentage of data items that constitute the hot set
const double hotspotdatafraction = .2;
// Percentage of operations that access the hot set
const double hotspotopnfraction = .8;

enum db_op { READ, UPDATE, INSERT, REMOVE };

static char keys[recordcount][RECORD_LENGTH];

static void init_keys() {
  for (int i = 0; i < recordcount; i++) {
    for (int j = 0; j < RECORD_LENGTH; j++) {
      keys[i][j] = rand() % 0xFF;
    }
  }
}

static void generate_value(char* value) {
  for (int j = 0; j < RECORD_LENGTH; j++) {
    value[j] = rand() % 0xFF;
  }
}

static void init_db(db& db) {
  char value[RECORD_LENGTH];
  generate_value(value);
  for (int i = 0; i < recordcount; i++) {
    db.insert(keys[i], value);
  }
}

static void execute_operation(db& db) {
  db_op op;
  bool hotop;
  double dice;
  char value[RECORD_LENGTH];

  dice = rand() * 1.0 / RAND_MAX;
  if (dice < .95) {
    op = READ;
  }
  else {
    op = UPDATE;
  }
  dice = rand() * 1.0 / RAND_MAX;
  if (dice < .8) {
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
        db.read(keys[idx], value);
      }
      else {
        int idx = recordcount * (1 - hotspotdatafraction) * dice;
        db.read(keys[idx], value);
      }
      break;
    }
    case UPDATE: {
      if (hotop) {
        generate_value(value);
        int idx = recordcount * hotspotdatafraction * dice;
        db.update(keys[idx], value);
      }
      else {
        generate_value(value);
        int idx = recordcount * (1 - hotspotdatafraction) * dice;
        db.update(keys[idx], value);
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

    srand(0);
    init_keys();

    db db;
    init_db(db);

    on_all_cores( [&db] {
      for (int i = 0; i < operationcount; i++) {
        execute_operation(db);
      }
    });

    Metrics::merge_and_dump_to_file();

  });
  finalize();
}
