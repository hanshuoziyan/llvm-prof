/* Store Load Instruction for Global Variable Profiling 
 * 
 * This Profiling would insert for all store instructions on global variable
 * bind the instruction id with the global variable address. a map like
 * <address,instr>
 *
 * Then insert for all load instruction on global variable, with the same
 * address, can find which stored instruction before.
 */

#include <llvm/Pass.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/InstIterator.h>
#include "ProfilingUtils.h"
#include "ValueUtils.h"

#include <stdio.h>

using namespace std;
using namespace llvm;

class SLGlobalProfiling: public ModulePass
{
public:
   static char ID;
   SLGlobalProfiling():ModulePass(ID) {}
   bool runOnModule(Module& M);
};

char SLGlobalProfiling::ID = 0;
static RegisterPass<SLGlobalProfiling> X("insert-slg-profiling", "Insert StoreLoadGlobalVariable Profiling into Module", false, true);

static bool isArgumentWrite(llvm::Argument *Arg)
{
   Function* F = Arg->getParent();
   if(!Arg->getType()->isPointerTy()) return false;
   if(F->hasFnAttribute("lle.arg.write")){
      Attribute Attr = F->getFnAttribute("lle.arg.write");
      char ArgNo[5];
      snprintf(ArgNo, sizeof(ArgNo), "%u", Arg->getArgNo());
      size_t ArgNoLen = strlen(ArgNo);
      StringRef AttrStr = Attr.getValueAsString();
      for(size_t beg = 0; beg != StringRef::npos ; beg = AttrStr.find(ArgNo, beg+1)){
         size_t end = beg+ArgNoLen+1;
         if(!(end < AttrStr.size() && !isdigit(AttrStr[end]))) continue;
         if(!(beg > 0 && !isdigit(AttrStr[beg-1]))) continue;
         return true;
      }
   }else{
      for(Argument::use_iterator U = Arg->use_begin(), E = Arg->use_end(); U!=E; ++U){
         if(isa<StoreInst>(*U)) return true;
      }
   }
   return false;
}

bool SLGlobalProfiling::runOnModule(Module &M)
{
   LLVMContext& C = M.getContext();
   Type* VoidTy = Type::getVoidTy(C);
   PointerType* VoidPtrTy = PointerType::getUnqual(VoidTy);
   Type* Int32Ty = IntegerType::getInt32Ty(C);
   APInt loadCount = APInt::getNullValue(32); // get a 32 bit zero
   APInt storeCount = APInt::getNullValue(32);
   storeCount++;// it start from 1
   Constant* StoreCall = M.getOrInsertFunction("llvm_profiling_trap_store",
         VoidTy, Int32Ty, VoidPtrTy, NULL);
   Constant* LoadCall = M.getOrInsertFunction("llvm_profiling_trap_load",
         VoidTy, Int32Ty, VoidPtrTy, NULL);
   llvm::Value* callArgs[2];

   for(Module::iterator F = M.begin(), ME = M.end(); F != ME; ++F){
      for(inst_iterator I = inst_begin(F), E = inst_end(F); I!=E; ++I){
         if(lle::access_global_variable(&*I)){
            if(StoreInst* SI = dyn_cast<StoreInst>(&*I)){
               callArgs[0] = Constant::getIntegerValue(Int32Ty, storeCount++);
               callArgs[1] = CastInst::CreatePointerCast(SI->getPointerOperand(), VoidPtrTy, "", SI);
               CallInst::Create(StoreCall, callArgs, "", SI);
            }else if(LoadInst* LI = dyn_cast<LoadInst>(&*I)){
               callArgs[0] = Constant::getIntegerValue(Int32Ty, loadCount++);
               callArgs[1] = CastInst::CreatePointerCast(LI->getPointerOperand(), VoidPtrTy, "", LI);
               CallInst::Create(LoadCall, callArgs, "", LI);
            }
         }
      }
   }

   Type* CounterTy = ArrayType::get(Int32Ty, loadCount.getZExtValue());
   GlobalVariable* Counters = new GlobalVariable(M, CounterTy, false,
         GlobalVariable::InternalLinkage, Constant::getNullValue(CounterTy),
         "SLGCounters");
   Function* MainFn = M.getFunction("main");
   InsertProfilingInitCall(MainFn, "llvm_start_slg_profiling", Counters);

   Constant* SettingFn = M.getOrInsertFunction("llvm_start_slg_setting", VoidTy, Int32Ty, NULL);
   Value* SettingArgs[] = {
      Constant::getIntegerValue(Int32Ty, storeCount)
   };
   CallInst::Create(SettingFn, SettingArgs, "", MainFn->getEntryBlock().getFirstInsertionPt());

   return true;
}
