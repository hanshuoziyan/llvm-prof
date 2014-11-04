//===- llvm-prof.cpp - Read in and process llvmprof.out data files --------===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This tools is meant for use with the various LLVM profiling instrumentation
// passes.  It reads in the data file produced by executing an instrumented
// program, and outputs a nice report.
//
//===----------------------------------------------------------------------===//

#include <ProfileInfo.h>
#include <ProfileInfoLoader.h>
#include <ProfileInfoWriter.h>
#include <ProfileInfoMerge.h>
#include <ProfileDataTypes.h>
#include <llvm/IR/Module.h>
#include <llvm/PassManager.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Bitcode/ReaderWriter.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/PrettyStackTrace.h>
#include "passes.h"

#if LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR == 4
#include <llvm/Support/system_error.h>
#include <llvm/ADT/OwningPtr.h>
#else
#include <system_error>
#include <memory>
#define OwningPtr unique_ptr
#endif

using namespace llvm;
using namespace std;

namespace {
  cl::opt<std::string>
  BitcodeFile(cl::Positional, cl::desc("<program bitcode file>"),
              cl::Required);

  cl::opt<std::string>
  ProfileDataFile(cl::Positional, cl::desc("<llvmprof.out file>"),
                  cl::Optional, cl::init("llvmprof.out"));

  cl::opt<bool> DiffMode("diff",cl::desc("Compare two out file"));
  ///////////////////
  enum Algorithm {
     ALGO_SUM,
     ALGO_AVG
  };
  cl::opt<Algorithm> Algo("algo", cl::desc("Merge algorithm"), cl::values(
           clEnumValN(ALGO_SUM, "sum", "cacluate sum of total"),
           clEnumValN(ALGO_AVG, "avg", "caculate averange of total"),
           clEnumValEnd), 
        cl::init(ALGO_SUM));
  cl::opt<bool> Merge("merge",cl::desc("Merge the Profile info"));
  cl::list<std::string> MergeFile(cl::Positional,cl::desc("<Merge file list>"),cl::ZeroOrMore);
  /////////////////////
  cl::opt<bool> Convert("to-block", cl::desc("Convert Profiling Types to BasicBlockInfo Type"));
}

struct AvgAcc{
   size_t N;
   unsigned operator()(unsigned Lhs, unsigned Rhs){ 
      ++N;
      return Lhs+Rhs;
   }
};

int main(int argc, char **argv) {
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal();
  PrettyStackTraceProgram X(argc, argv);

  LLVMContext &Context = getGlobalContext();
  llvm_shutdown_obj Y;  // Call llvm_shutdown() on exit.
  
  cl::ParseCommandLineOptions(argc, argv, "llvm profile dump decoder\n");

  // Read in the bitcode file...
  std::string ErrorMessage;
  error_code ec;
  Module *M = 0;
  if(DiffMode) {
     ProfileInfoLoader PIL1(argv[0], BitcodeFile);
     ProfileInfoLoader PIL2(argv[0], ProfileDataFile);
     
     ProfileInfoCompare Compare(PIL1,PIL2);
     Compare.run();
     return 0;
  }
  if(Merge) {
     /** argument alignment: 
      *  BitcodeFile ProfileDataFile MergeFile 
      *  output.out  input1.out      other-input.out 
      **/

     // Add the ProfileDataFile arg to MergeFile, it blongs to the MergeFile
     MergeFile.push_back(std::string(ProfileDataFile.getValue()));
     if(MergeFile.size()==0){
        errs()<<"No merge file!";
        return 0;
     }

     //Initialize the ProfileInfoMerge class using one of merge files
     ProfileInfoLoader AHS(argv[0], *(MergeFile.end()-1));
     ProfileInfoMerge MergeClass(std::string(argv[0]),BitcodeFile,AHS);

     for(std::vector<std::string>::iterator merIt = MergeFile.begin(),END = MergeFile.end()-1;merIt!=END;++merIt){
        //errs()<<*merIt<<"\n";
        ProfileInfoLoader THS(argv[0], *merIt);
        MergeClass.addProfileInfo(THS);
     }
     if(Algo == ALGO_SUM){
        MergeClass.writeTotalFile();
     }else if(Algo == ALGO_AVG){
        /** avg = sum/N **/
        MergeClass.writeTotalFile(std::bind2nd(std::divides<unsigned>(), MergeFile.size()));
     }
     return 0;
  }
#if LLVM_VERSION_MAJOR==3 && LLVM_VERSION_MINOR==4
  OwningPtr<MemoryBuffer> Buffer;
  if (!(ec = MemoryBuffer::getFileOrSTDIN(BitcodeFile, Buffer.get()))) {
     M = ParseBitcodeFile(Buffer.get(), Context, &ErrorMessage);
  } else
     ErrorMessage = ec.message();

#else

  auto Buffer = MemoryBuffer::getFileOrSTDIN(BitcodeFile);
  if (!(ec = Buffer.getError())){
     auto R = parseBitcodeFile(&**Buffer, Context);
     if(R.getError()){
        M = NULL;
        ErrorMessage = R.getError().message();
     }else
        M = R.get();
  } else
     ErrorMessage = ec.message();
#endif
  if (M == 0) {
     errs() << argv[0] << ": " << BitcodeFile << ": "
        << ErrorMessage << "\n";
     return 1;
  }
  if(Convert){
     if(MergeFile.size() == 0){
        errs()<< "no output file\n";
        return 1;
     }
     ProfileInfoWriter PIW(argv[0], MergeFile.front());

     PassManager PassMgr;
     PassMgr.add(createProfileLoaderPass(ProfileDataFile));
     PassMgr.add(new ProfileInfoConverter(PIW));
     PassMgr.run(*M);
     return 0;
  }

  // Read the profiling information. This is redundant since we load it again
  // using the standard profile info provider pass, but for now this gives us
  // access to additional information not exposed via the ProfileInfo
  // interface.
  ProfileInfoLoader PIL(argv[0], ProfileDataFile);

  // Run the printer pass.
  PassManager PassMgr;
  PassMgr.add(createProfileLoaderPass(ProfileDataFile));
  PassMgr.add(new ProfileInfoPrinterPass(PIL));
  PassMgr.run(*M);

  return 0;
}
