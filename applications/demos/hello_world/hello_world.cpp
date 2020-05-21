#include <Grappa.hpp>

using namespace Grappa;
using namespace std;

static bool myassert(bool expr) {
  if (!(expr)) {
    //std::cout << __FILE__ << ":" << __LINE__ << " aborted.\n";
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

int main(int argc, char * argv[]) {
  init( &argc, &argv );
  run([]{
    //Metrics::reset_all_cores();
    //Metrics::start_tracing();

    GlobalAddress< int > array = global_alloc<int>(5);
    on_all_cores([&array]{
        switch (mycore()) {
        case 0: test_0(array); break;
        case 1: test_1(array); break;
        }
    });

    //Metrics::merge_and_dump_to_file();

  });
  finalize();
}
