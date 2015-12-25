#include "preheader.h"
#include <llvm/Pass.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/raw_ostream.h>
#include <unordered_map>

#include "ValueUtils.h"
#include "ProfilingUtils.h"
#include "ProfileInstrumentations.h"
#include "ProfileDataTypes.h"

namespace {
   class TimeProfiler : public llvm::ModulePass
   {
      public:
      static char ID;
      TimeProfiler():ModulePass(ID) {};
      bool runOnModule(llvm::Module&) override;
   };
}

using namespace llvm;
using namespace lle;
char TimeProfiler::ID = 0;
static RegisterPass<TimeProfiler> X("insert-time-profiling",
      "insert profiling for the time of mpi communication", false, false);

static void IncrementTimeCounter(Value* Inc, Value* calle, unsigned Index, GlobalVariable* Counters, IRBuilder<>& Builder)
{
   LLVMContext &Context = Inc->getContext();

   // Create the getelementptr constant expression
   std::vector<Constant*> Indices(2);
   Indices[0] = Constant::getNullValue(Type::getInt32Ty(Context));
   Indices[1] = ConstantInt::get(Type::getInt32Ty(Context), Index);
   Constant *ElementPtr =
      ConstantExpr::getGetElementPtr(Counters, Indices);

   // Load, increment and store the value back.
   ArrayRef<Value*> args;
   CallInst* Inc_end = Builder.CreateCall(DoubleTy, wtime);
   Value* OldVal = Builder.CreateLoad(ElementPtr, "OldTimeCounter");
   Value* TmpVal = Builder.CreateFSub(OldVal, Inc, "TmpTimeCounter");
   Value* NewVal = Builder.CreateFAdd(TmpVal, Inc_end, "NewTimeCounter");
   Builder.CreateStore(NewVal, ElementPtr);
}

bool TimeProfiler::runOnModule(llvm::Module &M)
{
  Function *Main = M.getFunction("main");
  if (Main == 0) {
    errs() << "WARNING: cannot insert edge profiling into a module"
           << " with no main function!\n";
    return false;  // No main, no instrumentation!
  }

  std::vector<CallInst*> Traped;
  Function* wtime = NULL;
  for(auto F = M.begin(), E = M.end(); F!=E; ++F){
     if(F.getName() == "mpi_wtime_")
        wtime = &*F;
     for(auto I = inst_begin(*F), IE = inst_end(*F); I!=IE; ++I){
        CallInst* CI = dyn_cast<CallInst>(&*I);
        if(CI == NULL) continue;
        Function* func = CI->getCalledFunction();
        StringRef str = func->getName();
        if(str.find_first_of("mpi_"))
        {
           if(str.find_first_of("mpi_init_")||str.find_first_of("mpi_comm_rank_")||str.find_first_of("mpi_comm_size_"))
              continue;
           errs()<< str<<"\n";
           Traped.push_back(CI);
        }
     }
  }

  IRBuilder<> Builder(M.getContext());
  Type* I32Ty = Type::getInt32Ty(M.getContext());
  Type* DoubleTy = Type::getDoubleTy(M.getContext());

  Type*ATy = ArrayType::get(DoubleTy, Traped.size());
  GlobalVariable* Counters = new GlobalVariable(M, ATy, false,
        GlobalVariable::InternalLinkage, Constant::getNullValue(ATy),
        "TimeCounters");

  unsigned I=0;
  for(auto P : Traped){
     ArrayRef<Value*> args;
     CallInst* callTime = CallInst::Create(DoubleTy, wtime, args, "", P);
     Builder.SetInsertPoint(P);

     IncrementTimeCounter(callTime, wtime, I++, Counters, Builder);
  }

  InsertPredProfilingInitCall(Main, "llvm_start_time_profiling", Counters);
  return true;
}
