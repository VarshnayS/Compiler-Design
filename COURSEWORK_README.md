# CS325 MiniC Starter Code

This directory contains the starter compiler and public tests for the MiniC
coursework.

The `tests/` directory contains 11 core-language examples and four array
examples, each with a C++ driver. The array examples cover local 2D arrays,
1D array parameters, global arrays, and matrix multiplication. Array tests
require the Part 3 extension.

The core grammar from the coursework specification is also available in
`grammar.txt`.

Build the starter compiler with:

```bash
make
```

On DCS machines, load the coursework toolchain before building:

```bash
module load GCC/13.3.0
export PATH=/modules/cs325/llvm-23.1.0/bin:$PATH
export LD_LIBRARY_PATH=/modules/cs325/llvm-23.1.0/lib:$LD_LIBRARY_PATH
```

At release, `mccomp.cpp` contains a lexer and parser/code-generation scaffold.
The initial starter code is expected to pass the public lexer smoke test:

```bash
./tests/tests.sh -compile_only
```

The full public test script links and runs generated LLVM IR:

```bash
./tests/tests.sh
```

That full script is intended for a completed compiler implementation, so it is
not expected to pass before the parser, AST, semantic checks, and LLVM IR code
generation have been implemented.
