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
#include <iostream>
#include <unordered_map>

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
        ModuleSlotTracker MST(Mod.get(), false);
        MST.incorporateFunction(*funcPtr);

        // find the two assignment nodes
        const Instruction* srcInstruction = nullptr;
        const DDGNode* srcNode = nullptr;
        const Instruction* destInstruction = nullptr;
        const DDGNode* destNode = nullptr;
        //using InstToNodeMap = DenseMap<Instruction*, DDGNode*>;
        //InstToNodeMap inst2Node;
        {
              
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
                                      srcNode = currNode;
                                }
                                else if (slotIdx == DestVar) {
                                      destInstruction = i;
                                      destNode = currNode;
                                }
                          }
                    }
                    else if (isa<PiBlockDDGNode>(currNode)) {
                          ;
                    }
                    ++testIt;
              }
        }
        
        
        if (!srcNode) {
              std::cout << "srcNode is nullptr. Exit." << std::endl;
              exit(0);
        }
        if (!destNode) {
              std::cout << "destNode is nullptr. Exit." << std::endl;
              exit(0);
        }

        // search in graph
        //std::vector<bool> exploredNodes(ddg.size(), false);
        std::unordered_map<const DDGNode*, bool> exploredNodes;
        {
              for (DDGNode** it = ddg.begin(); it < ddg.end(); it++) {
                    DDGNode* currNode = *it;
                    exploredNodes[currNode] = false;
              }
        }
        std::vector<const DDGNode*> pendingNodes{srcNode};
        std::vector<const DDGNode*> currPath;

        while (!pendingNodes.empty()) {
              // pop
              const DDGNode* currNode = pendingNodes.back();
              pendingNodes.pop_back();
              currPath.push_back(currNode);
              // destination check
              if (currNode == destNode) {
                    break;
              }
              // expand and push unvisited nodes
              auto& edges = currNode->getEdges();

              bool shouldGoBack = true;
              for (DDGEdge* e : edges) {
                    if (e->isDefUse()) {
                          // only analyse def-use edges for now
                          const DDGNode& target = e->getTargetNode();
                          if (!exploredNodes[&target]) {
                                exploredNodes[&target] = true;
                                shouldGoBack = false;
                                pendingNodes.push_back(&target);
                          }
                    }
              }
              if (shouldGoBack) {
                    // no new nodes are found in this iteration, which means 
                    // the current node is NOT to be on the correct path.
                    // We should remove it.
                    currPath.pop_back();
              }
        }

        if (pendingNodes.empty() || currPath.empty())
              std::cout << "Path is not found." << std::endl;
        else {
              std::cout << "Path from " << SrcVar << "to " << DestVar << " is found:";
              for (auto* node : currPath) {
                    // get all the slots
                    if (isa<SimpleDDGNode>(node)) {
                          const DDGNode::InstructionListType& currInstructionList = static_cast<const SimpleDDGNode*>(node)->getInstructions();
                          for (const Instruction* i : currInstructionList) {
                                int slotIdx = MST.getLocalSlot(i);
                                std::cout << "-> SimpleDDGNode " << slotIdx << std::endl;
                          }

                    }
                    else if (isa<PiBlockDDGNode>(node)) {
                          std::cout << "-> Pi-block node" << std::endl;
                    }
              }
              
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