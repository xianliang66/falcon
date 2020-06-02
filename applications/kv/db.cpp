#define RECORD_LENGTH 64
#define RECORD_NUMBER 102400

#include <bitset>

struct record {
  char key[RECORD_LENGTH]; 
  char value[RECORD_LENGTH]; 
  bool valid;
};

class db {
  const int open_addr_limit = 5;
  GlobalAddress <record> data;

  unsigned long hash(char *str)
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
  db() : data(global_alloc<record>(RECORD_NUMBER)) {}
  bool read(const char *key, char *value) {
    int start_idx = hash(key) % RECORD_NUMBER;
    for (int i = start_idx; i < start_idx + open_addr_limit; i++) {
      record& r = delegate::read(data + (i % RECORD_NUMBER));      
      if (r.valid && memcmp(r.key, key, RECORD_LENGTH) == 0) {
        memcpy(value, r.value);
        return true;
      }
    }
    return false;
  }

  bool write(const char *key, const char *value) {
    int start_idx = hash(key) % RECORD_NUMBER;
    for (int i = start_idx; i < start_idx + open_addr_limit; i++) {
      record& r = delegate::read(data + (i % RECORD_NUMBER));      
      if (r.valid && memcmp(r.key, key, RECORD_LENGTH) == 0) {
        memcpy(r.value, value);
        delegate::write(data + (i % RECORD_NUMBER), r);      
        return true;
      }
    }
    return false;
  }

  bool delete(const char *key) {
    int start_idx = hash(key) % RECORD_NUMBER;
    for (int i = start_idx; i < start_idx + open_addr_limit; i++) {
      record& r = delegate::read(data + (i % RECORD_NUMBER));      
      if (r.valid && memcmp(r.key, key, RECORD_LENGTH) == 0) {
        r.valid = false;
        delegate::write(data + (i % RECORD_NUMBER), r);      
        return true;
      }
    }
    return false;
  }
}
