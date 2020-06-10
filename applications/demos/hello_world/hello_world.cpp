#include <Grappa.hpp>

using namespace Grappa;
using namespace std;

static bool myassert(bool expr) {
  if (!(expr)) {
    LOG(ERROR) << __FILE__ << ":" << __LINE__ << " aborted.\n";
    return false;
    exit(0);
  }
  else {
    return true;
  }
}

static void test_0(GlobalAddress<int>& array) {
  for (int i = 0; i < 100000; i++) {
    LOG(INFO) << "test_0 write array[0] " << i;
    delegate::call(array, [i](int& a) {a = i;});
    while (delegate::read(array+2) != i) {
      Grappa::yield();
      LOG(INFO) << "test_0 write array[4] " << i;
      delegate::call(array+4, [i](int& a) {a = i*i;});
      LOG(INFO) << "test_0 read array[2] " << i;
    }
    LOG(INFO) << "test_0 read array[1] " << i;
    int r = delegate::read(array+1);
    if (!myassert(r == i*i)) {
      LOG(ERROR) << "it is " << r << ", and it should be " << i*i << std::endl;
    };
  }
}

static void test_1(GlobalAddress<int>& array) {
  for (int i = 0; i < 100000; i++) {
    while (delegate::read(array) != i) {
      Grappa::yield();
      LOG(INFO) << "test_1 write array[4] " << i;
      delegate::call(array+4, [i](int& a) {a = i*i;});
      LOG(INFO) << "test_1 read array[0] " << i;
    }
    LOG(INFO) << "test_1 write array[1] " << i;
    delegate::call(array+1, [i](int& a) {a = i*i;});
    LOG(INFO) << "test_1 write array[2] " << i;
    delegate::call(array+2, [i](int& a) {a = i;});
  }
}

static int test_3(GlobalAddress<int>& array) {
  delegate::call(array+5, [](int& a){a=4;});
  int a = delegate::read(array+6);
  return a;
}

static int test_4(GlobalAddress<int>& array) {
  delegate::write(array+6, 5);
  int b = delegate::read(array+5);
  return b;
}

static void test_3_4(GlobalAddress<int>& array) {
  int a, b;
  for (int i = 0; i < 500000; i++) {
    on_all_cores([&array, &a, &b]{
        switch (mycore()) {
        case 0: a = test_3(array); break;
        case 1: b = test_4(array); break;
        }
    });
    if (!((a == 0 && b == 4) ||
          (a == 5 && b == 4) ||
          (a == 5 && b == 0))) {
      LOG(ERROR) << "ERROR: a=" << a << " b=" << b;
      break;
    }
  }
}

static void test_5(GlobalAddress<int>& array) {
  delegate::write(array+5, 1);
}

static void test_6(GlobalAddress<int>& array) {
  delegate::write(array+6, 1);
}

static void test_7(GlobalAddress<int>& array) {
  delegate::write(array+7, delegate::read(array+5));
  delegate::write(array+8, delegate::read(array+6));
}

static void test_8(GlobalAddress<int>& array) {
  delegate::write(array+9, delegate::read(array+6));
  delegate::write(array+10, delegate::read(array+5));
}

static void test_5_8(GlobalAddress<int>& array) {
  int r1, r2, r3, r4;
  for (int i = 0; i < 50000; i++) {
    on_all_cores([&array, &r1, &r2, &r3, &r4]{
        switch (mycore()) {
        case 0:  test_4(array); break;
        case 1:  test_5(array); break;
        case 2:  test_6(array); break;
        case 3:  test_7(array); break;
        }
    });
    r1 = delegate::read(array+7);
    r2 = delegate::read(array+8);
    r3 = delegate::read(array+9);
    r4 = delegate::read(array+10);
    if (((r1 == 1 && r2 == 0 && r3 == 0 && r4 == 1) ||
         (r1 == 0 && r2 == 1 && r3 == 1 && r4 == 0))) {
      LOG(ERROR) << "ERROR: r1=" << r1 << " r2=" << r2 << " r3=" << r3 << " r4=" <<
        r4;
      break;
    }
  }
}

static void test_9(GlobalAddress<int> array) {
  delegate::write(array+5, 1);
}

static void test_10(GlobalAddress<int> array) {
  if (delegate::read(array+5) == 1) {
    delegate::write(array+6, 1);
  };
}

static void test_11(GlobalAddress<int> array) {
  int r = delegate::read(array+6);
  delegate::write(array+7, r);
  if (r == 1) {
    delegate::write(array+8, delegate::read(array+5));
  };
}

static void test_9_11(GlobalAddress<int>& array) {
  int r2, r3;
  for (int i = 0; i < 500000; i++) {
    on_all_cores([array, &r2, &r3]{
        switch (mycore()) {
        case 0:  test_9(array); break;
        case 1:  test_10(array); break;
        case 2:  test_11(array); break;
        }
    });
    r2 = delegate::read(array+7);
    r3 = delegate::read(array+8);
    if (((r2 == 0 && r3 == 1) ||
         (r2 == 0 && r3 == 1))) {
      LOG(ERROR) << "ERROR: r2=" << r2 << " r3=" << r3;
      break;
    }
  }
}

int main(int argc, char * argv[]) {
  init( &argc, &argv );
  run([]{
    Metrics::reset_all_cores();
    Metrics::start_tracing();

    GlobalAddress< int > array = global_alloc<int>(15);

    test_5_8(array);

    Metrics::merge_and_dump_to_file();

  });
  finalize();
}
