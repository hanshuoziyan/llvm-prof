#ifndef LLVM_TIMING_SOURCE_H_H
#define LLVM_TIMING_SOURCE_H_H

#include <llvm/IR/IRBuilder.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/GlobalVariable.h>

/* a timing source is used to count inst types in a basicblock */

namespace llvm{
class TimingSource{
   public:

   enum Kind {
      Base,
      Lmbench,
      MPI
   };

   TimingSource(Kind K, size_t NumParam):kindof(K){
      params.resize(NumParam); 
   }
   //在该模式中, 立即从文件中读取Timing数据并计算//
   /* init unit_times with nanoseconds unit.
    * @example:
    *    init(std::bind(TimingSource::load_lmbench, "lmbench.log", _1));
    */
   void init(std::function<void(double*)> func){
      func(params.data());
   }
   void init(std::initializer_list<double> list) {
      params = decltype(params)(list.begin(), list.end());
   }
   void init_with_file(const char* file) {
      init(std::bind(file_initializer, file, std::placeholders::_1));
   }
   Kind getKind() const { return kindof;}
   protected:
   Kind kindof;
   void (*file_initializer)(const char* file, double* data);
   std::vector<double> params;
};

namespace _timing_source{
template<class EnumType>
class T
{
   std::vector<double>& address;
   public:
   T(std::vector<double>& P):address(P) {}
   double get(EnumType E){
      return address[E];
   }
};
}

enum LmbenchInstGroups {
   Integer = 0, I64 = 1, Float = 2, Double = 3, // Ntype 
   Add = 0<<2, Mul = 1<<2, Div = 2<<2, Mod = 3<<2, // Method
   Last = Double|Mod, // Method | Ntype unit: nanosecond
   SOCK_BANDWIDTH, // unit: MB/sec
   SOCK_LATENCY, // unit: microsecond
   NumGroups
};

class LmbenchTiming: 
   public TimingSource, public _timing_source::T<LmbenchInstGroups>
{
   unsigned R;
   public:
   typedef LmbenchInstGroups EnumTy;
   static llvm::StringRef getName(EnumTy);
   static EnumTy classify(llvm::Instruction* I);
   static void load_lmbench(const char* file, double* cpu_times);
   static bool classof(const TimingSource* S) {
      return S->getKind() == Lmbench;
   }

   LmbenchTiming();

   double count(llvm::Instruction& I); // caculation part
   double count(llvm::BasicBlock& BB); // caculation part
   
   double count(llvm::Instruction& I, size_t bfreq, size_t count);
};

enum CommSpeed {
   MemRead, MemWrite, NetRead, NetWrite, NumCommSpeed
};

class MPITiming:
   public TimingSource, _timing_source::T<CommSpeed>
{
   public:
   typedef CommSpeed EnumTy;
   static void load_speed(const char* file, double* speed);
   double count(llvm::CallInst* MPI, size_t Count);

   MPITiming():TimingSource(MPI, NumCommSpeed), T(params) {
      file_initializer = load_speed;
   }
};

}

#endif
