#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DerivedTypes.h>
#include <memory>
#include <iostream>

#include <gflags/gflags.h>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/Error.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/Transforms/Scalar/SimplifyCFG.h>
#include <llvm/Transforms/Scalar/Reassociate.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Transforms/Utils/Cloning.h>

using namespace std;
using namespace llvm;
using namespace llvm::orc;

DEFINE_string(bcFile, "", "The pre-compiled llvm .bc file");
DEFINE_int32(optLevel, 0, "the argument to -Ox: 0, 1, 2, 3");
DEFINE_int32(var, 1, "The integer value bound to the argument of functions");

namespace {
ExitOnError ExitOnErr;

char const *funcs[] = {"id", "add1", "times2", "squareroot", "threeX_1"};

string prefix = "__jit_";
void bindArgument(Module &m, char const *name, int val) {
    auto &cxt = m.getContext();
    auto oldF = m.getFunction(name); assert(oldF);
    oldF->addFnAttr(Attribute::AlwaysInline);
    auto newF = Function::Create(
        FunctionType::get(Type::getInt32Ty(cxt), {}, false),
        Function::ExternalLinkage, 
        (prefix+name).c_str(), 
        m);
    auto bb = BasicBlock::Create(cxt, "", newF);
    IRBuilder<> builder(bb);
    Value *args[] = {builder.getInt32(val)};
    auto call = builder.CreateCall(
        FunctionType::get(Type::getInt32Ty(cxt), {Type::getInt32Ty(cxt)}, false), oldF, args);
#if 0
    // something wrong with codes below, using addFnAttr instead
    InlineFunctionInfo ifi;
    InlineFunction(*call, ifi);
#endif
    builder.CreateRet(call);
}
auto getBoundFunc(LLJIT &jit, char const *name, bool dump = false) {
    using FuncPtr = auto (*)() -> int;

    auto symbol = ExitOnErr(jit.lookup(prefix + name));
    return reinterpret_cast<FuncPtr>(symbol.getAddress());
}

auto loadModule() {
    auto context = make_unique<LLVMContext>();
    SMDiagnostic smd;
    auto m = parseIRFile(FLAGS_bcFile, smd, *context);

    // bind the argument and create new functions
    for (auto fn: funcs) {
        bindArgument(*m, fn, FLAGS_var/*arbitrary value, for testing*/);
    }

    cout<<"Before opt: **************\n";
    m->print(llvm::errs(), nullptr);

    // do the optimization: the .bc file may already be compiled with optimizations, codes
    // below may don't need the whole passes
    LoopAnalysisManager LAM;
    FunctionAnalysisManager FAM;
    CGSCCAnalysisManager CGAM;
    ModuleAnalysisManager MAM;
    PassBuilder PB;
    FAM.registerPass([&] { return PB.buildDefaultAAPipeline(); });
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);
    ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(PassBuilder::OptimizationLevel::O3);
    MPM.run(*m, MAM);

    cout<<"After opt: **************\n";
    m->print(llvm::errs(), nullptr);
    return ThreadSafeModule(move(m), move(context));
}
}

int main(int argc, char **argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    InitLLVM X(argc, argv);
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();

    auto jit = ExitOnErr(LLJITBuilder().create());
    if (auto E = jit->addIRModule(loadModule())) return -1;

    // all of these functions could be computed as a single return
    for (auto fn: funcs) {
        auto ptr = getBoundFunc(*jit, fn, true);
        // no trivial method to inspect the assembly code.
        // the only method I found is using disassemble in gdb
        cout<<"call "<<fn<<": "<<ptr()<<'\n';
    }

    return 0;
}
