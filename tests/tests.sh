#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TESTS_DIR="$ROOT_DIR/tests"

LLVM_INSTALL_PATH=/modules/cs325/llvm-23.1.0

if ! command -v module >/dev/null 2>&1 && [ -f /etc/profile.d/modules.sh ]; then
  source /etc/profile.d/modules.sh
fi

if command -v module >/dev/null 2>&1; then
  module load GCC/13.3.0
fi

export PATH=$LLVM_INSTALL_PATH/bin:$PATH
export LD_LIBRARY_PATH=$LLVM_INSTALL_PATH/lib:$LD_LIBRARY_PATH
CLANG=$LLVM_INSTALL_PATH/bin/clang++

COMPILE_ONLY=0
for arg in "$@"; do
  case "$arg" in
    -compile_only|--compile-only)
      COMPILE_ONLY=1
      ;;
    -h|--help)
      echo "Usage: $0 [-compile_only|--compile-only]"
      exit 0
      ;;
    *)
      echo "Unknown option: $arg"
      echo "Usage: $0 [-compile_only|--compile-only]"
      exit 1
      ;;
  esac
done

cd "$ROOT_DIR"

echo "Compile compiler *****"
make mccomp

COMP="$ROOT_DIR/mccomp"

validate() {
  local command="$1"
  if ! $command > perf_out; then
    echo
    echo "$command"
    cat perf_out
    rm -f perf_out
    echo "TEST FAILED *****"
    exit 1
  fi

  echo
  echo "$command"
  grep "Result" perf_out || true
  if ! grep "PASSED" perf_out; then
    cat perf_out
    rm -f perf_out
    echo "TEST FAILED *****"
    exit 1
  fi
  rm -f perf_out
}

run_test() {
  local name="$1"
  local exe="$2"
  local test_dir="$TESTS_DIR/$name"

  echo
  echo "Test: $name"
  cd "$test_dir"
  rm -f output.ll "$exe" perf_out

  "$COMP" "./$name.c"

  if [ "$COMPILE_ONLY" -eq 1 ]; then
    return
  fi

  "$CLANG" driver.cpp output.ll -o "$exe"
  validate "./$exe"
}

echo "Run tests *****"

run_test addition add
run_test factorial fact
run_test fibonacci fib
run_test pi pi
run_test while while
run_test void void
run_test cosine cosine
run_test unary unary
run_test recurse recurse
run_test rfact rfact
run_test palindrome palindrome
run_test array_addition array_addition
run_test array_func_arg_1d array_func_arg
run_test global_array global_array
run_test matrix_multiplication matrix_mul

if [ "$COMPILE_ONLY" -eq 1 ]; then
  echo
  echo "***** ALL TESTS COMPILED *****"
else
  echo
  echo "***** ALL TESTS PASSED *****"
fi
