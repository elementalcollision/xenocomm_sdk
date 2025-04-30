# Memory Leak Testing Guide

This guide describes how to test for memory leaks in the Python/C++ bindings using both Python-side and C++-side tools.

## Python-Side Testing (tracemalloc)

- Run the Python memory leak tests using:
  ```bash
  python -m unittest memory_test.MemoryLeakTests
  ```
- These tests use `tracemalloc` to track memory usage before and after repeated creation/destruction of C++-bound objects.
- If a test fails, it means memory usage grew unexpectedly, which may indicate a leak.

## C++-Side Testing (Valgrind)

- To run the memory leak tests under Valgrind:
  ```bash
  cd bindings/python/tests
  bash run_valgrind_tests.sh
  ```
- This will run the Python memory tests with Valgrind's leak checker enabled.
- The script uses `valgrind-python.supp` to suppress known Python interpreter leaks.

### Interpreting Valgrind Output
- Look for lines like `definitely lost` in the Valgrind output. These indicate real memory leaks.
- `still reachable` is usually safe, but review if large.
- Suppressed leaks are expected for Python internals.

### Common Issues
- **Reference cycles**: If Python and C++ objects reference each other, they may not be collected. Use weak references or explicit cleanup.
- **Callback leaks**: Ensure C++ does not hold strong references to Python callbacks after they're no longer needed.
- **Buffer protocol**: Ensure buffer views are released after use.

## CI Integration
- Memory leak tests are automatically run on every push and pull request to the `main` branch using GitHub Actions.
- See the workflow file at [`.github/workflows/memory-tests.yml`](../../.github/workflows/memory-tests.yml) for details.
- The workflow:
  - Runs Python memory leak tests
  - Installs and runs Valgrind on the same tests
  - Outputs results in the Actions logs for every push and pull request

## Troubleshooting
- If you see unexpected leaks, try reducing the number of iterations in the test to isolate the issue.
- Use Valgrind's `--track-origins=yes` to get more information about where leaked memory was allocated. 