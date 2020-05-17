#include <Grappa.hpp>

using namespace Grappa;
using namespace std;

#define myassert(expr) \
      if (!(expr)) \
        std::cout << __FILE__ << ":" << __LINE__ << " aborted.\n";

static void test_0(GlobalAddress<int>& array) {
  LOG(INFO) << "test_0";
  delegate::write(array, 4);
  delegate::write(array+1, 5);
}

static void test_1(GlobalAddress<int>& array) {
  LOG(INFO) << "test_1";
  while (delegate::read(array+1) != 5)
    Grappa::yield();
  myassert(delegate::read(array) == 4);
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
