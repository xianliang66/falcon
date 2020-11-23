#include <Grappa.hpp>
#include <cstdlib>

using namespace Grappa;
using namespace std;

#define RECORD_LENGTH 64
#define SLOT_NUMBER 2500000
#define open_addr_limit max(15, SLOT_NUMBER / 1000)

#include <bitset>

class record_field_t {
private:
  char data[RECORD_LENGTH];
public:
  record_field_t() { memset(data, 0, RECORD_LENGTH); }
  record_field_t(const record_field_t& other) {
    memcpy(data, other.data, RECORD_LENGTH);
  }
  record_field_t& operator=(const record_field_t& other) {
    if (this != &other) {
      memcpy(data, other.data, RECORD_LENGTH);
    }
    return *this;
  }
  bool operator==(const record_field_t& other) {
    if (this != &other) {
      return memcmp(data, other.data, RECORD_LENGTH) == 0;
    }
    else {
      return true;
    }
  }
  bool operator!=(const record_field_t& other) {
    if (this == &other) {
      return false;
    }
    else {
      return memcmp(data, other.data, RECORD_LENGTH) != 0;
    }
  }
  char& operator[](int idx) { return data[idx]; }
  char operator[](int idx) const { return data[idx]; }
  unsigned size() const { return RECORD_LENGTH; }
};

class record {
private:
  // Make sure these fields have their own copy constructor.
  record_field_t _key;
  record_field_t _value;
  bool _valid;

public:
  record() : _valid(false) {}

  record(const record& r) {
    _key = r._key;
    _value = r._value;
    _valid = r._valid;
  }

  record& operator=(const record& other) {
    if (this != &other) {
      _key = other._key;
      _value = other._value;
      _valid = other._valid;
    }
    return *this;
  }

  record_field_t& key() { return _key; }
  record_field_t& value() { return _value; }
  bool& valid() {  return _valid; }
} GRAPPA_BLOCK_ALIGNED;

class db {

public:
  GlobalAddress <record> data;

  static unsigned long hash(const record_field_t& s)
  {
    unsigned long hash = 5381;
    char c;

    for (unsigned i = 0; i < s.size(); i++) {
      c = s[i];
      hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }

    return hash;
  }

  static void generate_key(int idx, record_field_t& key) {
    static_assert(RECORD_LENGTH % sizeof(unsigned long) == 0,
        "RECORD_LENGTH is not multiple of hash result!");
    for (int i = 0; i < RECORD_LENGTH / sizeof(unsigned long); i+=sizeof(unsigned long)) {
      unsigned long c = (79 * idx + i) * (idx + 377) * (i + 574788) * 13379 + 8116321727L;
      for (int j = 0; j < sizeof(unsigned long); j++) {
        key[i + j] = (c >> 8 * j) & 0xFF;
      }
    }
  }

  static void generate_value(record_field_t& value) {
    for (int j = 0; j < value.size(); j++) {
      value[j] = rand() % 0xFF;
    }
  }

  db() /*: data(global_alloc<record>(SLOT_NUMBER))*/ { }
  db(GlobalAddress<record> g) { data = g;}
  db(const db& d) { data = d.data; }
  ~db() {}
  db& operator=(const db& other) {
    if (this != &other) {
      data = other.data;
    }
    return *this;
  }

  bool read(const record_field_t& key, record_field_t& value, int idx) {
    int start_idx = hash(key) % SLOT_NUMBER;
    for (int i = start_idx; i < start_idx + open_addr_limit; i++) {
      record r = delegate::read(data + (i % SLOT_NUMBER));
      LOG(INFO) << "Core " << Grappa::mycore() << " read key  " << idx << " hash:" <<
        hash(key) << " actually:" << hash(r.key()) << " value:" << hash(r.value()) <<
        " valid " << r.valid() << " at " << (i % SLOT_NUMBER);
      if (!r.valid()) {
        return false;
      }
      if (r.key() == key) {
        value = r.value();
        return true;
      }
    }
    return false;
  }

  bool update(const record_field_t& key, const record_field_t& value, int idx) {
    int start_idx = hash(key) % SLOT_NUMBER;
    for (int i = start_idx; i < start_idx + open_addr_limit; i++) {
      record r = delegate::read(data + (i % SLOT_NUMBER));
      if (!r.valid()) {
        return false;
      }
      if (r.key() == key) {
        r.value() = value;
        delegate::write(data + (i % SLOT_NUMBER), r);
        LOG(INFO) << "Core " << Grappa::mycore() << " write key  " << idx << " key:" <<
          hash(key) << " value:" << hash(value) << " valid " << r.valid() << " at "
          << (i % SLOT_NUMBER);
        return true;
      }
    }
    return false;
  }

  bool insert(const record_field_t& key, const record_field_t& value, int idx) {
    int start_idx = hash(key) % SLOT_NUMBER;
    for (int i = start_idx; i < start_idx + open_addr_limit; i++) {
      record r = delegate::read(data + (i % SLOT_NUMBER));
      if (!r.valid()) {
        r.valid() = true;
        r.key() = key;
        r.value() = value;
        delegate::write(data + (i % SLOT_NUMBER), r);
        return true;
      }
    }
    return false;
  }

  bool remove(const record_field_t& key) {
    int start_idx = hash(key) % SLOT_NUMBER;
    for (int i = start_idx; i < start_idx + open_addr_limit; i++) {
      record r = delegate::read(data + (i % SLOT_NUMBER));
      if (!r.valid()) {
        return false;
      }
      if (r.key() == key) {
        r.valid() = false;
        delegate::write(data + (i % SLOT_NUMBER), r);
        return true;
      }
    }
    return false;
  }
};
