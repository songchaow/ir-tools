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
#include "llvm/IR/ModuleSlotTracker.h"
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

cl::opt<std::string> InputFilename(cl::Positional, cl::desc("<Input shader IR file path>"), cl::Required);
cl::opt<int32_t> SrcVar(cl::Positional, cl::desc("Destination node to test"), cl::Required);
cl::opt<int32_t> DestVar(cl::Positional, cl::desc("Src node to test"), cl::Required);

// Returns true if the load or store can be analyzed. Atomic and volatile
// operations have properties which this analysis does not understand.
static
bool isLoadOrStore(const Instruction* I) {
      if (const LoadInst* LI = dyn_cast<LoadInst>(I))
            return LI->isUnordered();
      else if (const StoreInst* SI = dyn_cast<StoreInst>(I))
            return SI->isUnordered();
      return false;
}

int main(int argc, char **argv) {

  cl::ParseCommandLineOptions(argc, argv);

  // Parse the input LLVM IR file into a module.
  SMDiagnostic Err;
  LLVMContext Context;
  std::unique_ptr<Module> Mod(parseIRFile(InputFilename, Err, Context));
  if (!Mod) {
    Err.print(argv[0], errs());
    return 1;
  }

  std::string OutputFilename;
  OutputFilename = std::string("Graph.dot");

  

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

        // find the two assignment nodes
        const Instruction* srcInstruction = nullptr;
        const Instruction* destInstruction = nullptr;
        ModuleSlotTracker MST(Mod.get(), false);
        MST.incorporateFunction(*funcPtr);
        SlotTracker* SlotTable = MST.getMachine() ? MST.getMachine() : nullptr;
        DDGNode** testIt = ddg.begin();
        while (testIt != ddg.end()) {
              DDGNode* currNode = *testIt;
              if (isa<SimpleDDGNode>(currNode)) {
                    // find all assignment statements
                    const DDGNode::InstructionListType& currInstructionList = static_cast<SimpleDDGNode*>(currNode)->getInstructions();
                    for (const Instruction* i : currInstructionList) {
                          //if (ConstantInt* CI = dyn_cast<ConstantInt>(i)) {
                          int slotIdx = MST.getLocalSlot(i);
                          if (slotIdx == SrcVar) {
                                srcInstruction = i;
                          }
                          else if (slotIdx == DestVar) {
                                destInstruction = i;
                          }
                    }
              }
              else if (isa<PiBlockDDGNode>(currNode)) {
                    ;
              }

              ++testIt;
        }
        

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