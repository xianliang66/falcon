Falcon
===============================================================================

Falcon is a novel self-invalidation distributed shared memory coherence protocol. Compared to the traditional write-invalidation protocol, Falcon eliminates the useless invalidations under the scarce cache scenario.

This repo is a Grappa-based implementation. To run the code, you should set up a [slurm](https://slurm.schedmd.com/documentation.html) cluster and refer [Grappa's manual](https://github.com/uwsampa/grappa) to build the program.

Additionally, you can configure three parameters to control the cache coherence. `cache_proto` is the protocol type, where vanilla (no cache) is 0, Falcon is 1, and write-invalidation is 2. `lease` is the maximum expiration timestamps of a lease, which only works for Falcon. `max_cache_number` is the number of cached entries.

For example, to run a YCSB benchmark with 100k records with Falcon and 25k cached entries, you can use:
```
  salloc -N1 -n16 mpirun applications/kv/ycsb.exe \
    -constant=false \
    -alpha=0.99 \
    -read_propotion=0.9 \
    -cache_proto=1 \
    -max_cache_number=25000 \
    -loop_threshold=120
```
