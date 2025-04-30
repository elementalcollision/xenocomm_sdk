#!/bin/bash
set -e

# Set Python path to include the module root if needed
export PYTHONPATH=../../..

# Run memory leak tests under Valgrind
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
  --suppressions=valgrind-python.supp \
  python3 -m unittest memory_test.MemoryLeakTests

echo "Valgrind test complete. Check output for memory leaks." 