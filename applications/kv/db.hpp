#include <Grappa.hpp>

using namespace Grappa;
using namespace std;

#define RECORD_LENGTH 64

#include <bitset>

struct record {
  char key[RECORD_LENGTH]; 
  char value[RECORD_LENGTH]; 
  bool valid;
};

class db {
  const int open_addr_limit = 5;
  const int SLOT_NUMBER = 102400;
  GlobalAddress <record> data;

  unsigned long hash(const char *str)
  {
    unsigned long hash = 5381;
    int c;

    for (int i = 0; i < RECORD_LENGTH; i++) {
      c = str[i];
      hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }

    return hash;
  }

public:
  db() : data(global_alloc<record>(SLOT_NUMBER)) {}
  bool read(const char *key, char *value) {
    int start_idx = hash(key) % SLOT_NUMBER;
    for (int i = start_idx; i < start_idx + open_addr_limit; i++) {
      record r = delegate::read(data + (i % SLOT_NUMBER));      
      if (r.valid && memcmp(r.key, key, RECORD_LENGTH) == 0) {
        memcpy(value, r.value, RECORD_LENGTH);
        return true;
      }
    }
    return false;
  }

  bool update(const char *key, const char *value) {
    int start_idx = hash(key) % SLOT_NUMBER;
    for (int i = start_idx; i < start_idx + open_addr_limit; i++) {
      record r = delegate::read(data + (i % SLOT_NUMBER));      
      if (r.valid && memcmp(r.key, key, RECORD_LENGTH) == 0) {
        memcpy(r.value, value, RECORD_LENGTH);
        delegate::write(data + (i % SLOT_NUMBER), r);      
        return true;
      }
    }
    return false;
  }

  bool insert(const char *key, const char *value) {
    int start_idx = hash(key) % SLOT_NUMBER;
    for (int i = start_idx; i < start_idx + open_addr_limit; i++) {
      record r = delegate::read(data + (i % SLOT_NUMBER));      
      if (!r.valid) {
        memcpy(r.key, key, RECORD_LENGTH);
        memcpy(r.value, value, RECORD_LENGTH);
        r.valid = true;
        delegate::write(data + (i % SLOT_NUMBER), r);      
        return true;
      }
    }
    return false;
  }

  bool remove(const char *key) {
    int start_idx = hash(key) % SLOT_NUMBER;
    for (int i = start_idx; i < start_idx + open_addr_limit; i++) {
      record r = delegate::read(data + (i % SLOT_NUMBER));      
      if (r.valid && memcmp(r.key, key, RECORD_LENGTH) == 0) {
        r.valid = false;
        delegate::write(data + (i % SLOT_NUMBER), r);      
        return true;
      }
    }
    return false;
  }
};
