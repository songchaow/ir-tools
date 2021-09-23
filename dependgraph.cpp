//------------------------------------------------------------------------------
// This is currently ad-hoc
//
// Eli Bendersky (eliben@gmail.com)
// This code is in the public domain
//------------------------------------------------------------------------------
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/DDGPrinter.h"
#include "llvm/Analysis/DDG.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Pass.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/CommandLine.h"

#include <string>
#include <cstdlib>

using namespace llvm;

cl::opt<std::string> InputFilename(cl::Positional, cl::desc("<Input shader IR>"), cl::Required);

int main(int argc, char **argv) {
  if (argc < 2) {
    errs() << "Usage: " << argv[0] << " <IR file>\n";
    return 1;
  }

  cl::ParseCommandLineOptions(argc, argv);

  // Parse the input LLVM IR file into a module.
  SMDiagnostic Err;
  LLVMContext Context;
  std::unique_ptr<Module> Mod(parseIRFile(InputFilename, Err, Context));
  if (!Mod) {
    Err.print(argv[0], errs());
    return 1;
  }

  // Parse output filename if available
  std::string OutputFilename;
        OutputFilename = std::string("Graph.dot");

  // Go over all named mdnodes in the module
  for (Module::const_named_metadata_iterator I = Mod->named_metadata_begin(),
                                             E = Mod->named_metadata_end();
       I != E; ++I) {
    outs() << "Found MDNode:\n";
    // These dumps only work with LLVM built with a special cmake flag enabling
    // dumps.
    // I->dump();

    for (unsigned i = 0, e = I->getNumOperands(); i != e; ++i) {
      Metadata *Op = I->getOperand(i);
      if (auto *N = dyn_cast<MDNode>(Op)) {
        outs() << "  Has MDNode operand:\n  ";
        // N->dump();
        outs() << "  " << N->getNumOperands() << " operands\n";
      }
    }
  }

  //ModulePassManager MPM;
  //MPM.addPass(DDGDotPrinterPass());

  Function* funcPtr = Mod->getFunction(StringRef("MainPSPacked"));
  if (funcPtr == nullptr) {
        funcPtr = Mod->getFunction(StringRef("MainVSPacked"));
  }
  if (funcPtr) {
        TargetLibraryInfoImpl TLII;
        TargetLibraryInfo TLI(TLII);
        AssumptionCache AC(*funcPtr);
        DominatorTree DT(*funcPtr);
        LoopInfo LI(DT);
        ScalarEvolution SE(*funcPtr, TLI, AC, DT, LI);

        AAResults AA(TLI);


        DependenceInfo dinfo(funcPtr, &AA, &SE, &LI);
        DataDependenceGraph ddg(*funcPtr, dinfo);

        // write to file
        std::error_code EC;
        raw_fd_ostream DotFile(OutputFilename, EC, sys::fs::OF_Text);

        if (!EC) {
              WriteGraph(DotFile, (const DataDependenceGraph*)&ddg);
        }

        std::string cmd = "dot -Tsvg " + OutputFilename;
        std::system(cmd.c_str());


  }



  return 0;
}