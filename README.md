# DXIL Tool Set

Some self-use tools I have developed during DXIL code analysis. Currently there are following tools:

- ir2dug
  Print a def-use graph from a LLVM IR assembly. The program input is the path to the IR file.

- checkdependency
  Input the IR path, the source local variable slot index, the destination index, and the program outputs

## Build
CMake and LLVM are required.
Modify CMakeList.txt yourself to configure the LLVM installation path.

An example build script run at the repo root:
```bash
mkdir build & cd build
cmake .. -DLLVM_DIR="C:/Users/songdogwang/Codes/llvm-project/build/lib/cmake/llvm"
```