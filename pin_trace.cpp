#include "pin.H"
#include <cstdio>
#include <string>

// Sized data-access traces, optionally restricted to explicit ROI markers.
// Threads have independent ROI state; callbacks serialize writes to one file.
// This records a tool-observed interleaving, not a multicore/coherence model.
KNOB<std::string> Output(KNOB_MODE_WRITEONCE,"pintool","o","pinatrace.out","output trace");
KNOB<BOOL> ROI(KNOB_MODE_WRITEONCE,"pintool","roi","0","trace only between cachelab_roi_begin/end");
static FILE* trace_file=nullptr;
static PIN_LOCK trace_lock;
static TLS_KEY thread_key;
struct State { bool active; };

static VOID ThreadStart(THREADID tid,CONTEXT*,INT32,VOID*) {
    PIN_SetThreadData(thread_key,new State{!ROI.Value()},tid);
}
static VOID ThreadEnd(THREADID tid,const CONTEXT*,INT32,VOID*) {
    delete static_cast<State*>(PIN_GetThreadData(thread_key,tid));
}
static VOID Begin(THREADID tid) { static_cast<State*>(PIN_GetThreadData(thread_key,tid))->active=true; }
static VOID End(THREADID tid) { static_cast<State*>(PIN_GetThreadData(thread_key,tid))->active=false; }
static VOID Routine(RTN routine,VOID*) {
    const std::string name=RTN_Name(routine);
    if(name!="cachelab_roi_begin" && name!="cachelab_roi_end") return;
    RTN_Open(routine);
    RTN_InsertCall(routine,IPOINT_BEFORE,AFUNPTR(name=="cachelab_roi_begin"?Begin:End),IARG_THREAD_ID,IARG_END);
    RTN_Close(routine);
}
static VOID Record(THREADID tid,ADDRINT pc,ADDRINT address,UINT32 bytes,UINT32 write) {
    auto* state=static_cast<State*>(PIN_GetThreadData(thread_key,tid));
    if(ROI.Value() && (!state || !state->active)) return;
    PIN_GetLock(&trace_lock,tid+1);
    std::fprintf(trace_file,"%c 0x%llx 0x%llx %u\n",write?'W':'R',
                 static_cast<unsigned long long>(pc),static_cast<unsigned long long>(address),bytes);
    PIN_ReleaseLock(&trace_lock);
}
static VOID Instruction(INS ins,VOID*) {
    for(UINT32 op=0;op<INS_MemoryOperandCount(ins);++op) {
        if(INS_MemoryOperandIsRead(ins,op)) INS_InsertPredicatedCall(ins,IPOINT_BEFORE,AFUNPTR(Record),
            IARG_THREAD_ID,IARG_INST_PTR,IARG_MEMORYOP_EA,op,IARG_UINT32,INS_MemoryOperandSize(ins,op),IARG_UINT32,0,IARG_END);
        if(INS_MemoryOperandIsWritten(ins,op)) INS_InsertPredicatedCall(ins,IPOINT_BEFORE,AFUNPTR(Record),
            IARG_THREAD_ID,IARG_INST_PTR,IARG_MEMORYOP_EA,op,IARG_UINT32,INS_MemoryOperandSize(ins,op),IARG_UINT32,1,IARG_END);
    }
}
static VOID Finish(INT32,VOID*) { if(trace_file) std::fclose(trace_file); }
int main(int argc,char** argv) {
    PIN_InitSymbols();
    if(PIN_Init(argc,argv)) return 1;
    trace_file=std::fopen(Output.Value().c_str(),"w");
    if(!trace_file) { std::perror("trace output"); return 1; }
    PIN_InitLock(&trace_lock);
    thread_key=PIN_CreateThreadDataKey(nullptr);
    if(thread_key==INVALID_TLS_KEY) return 1;
    PIN_AddThreadStartFunction(ThreadStart,nullptr);
    PIN_AddThreadFiniFunction(ThreadEnd,nullptr);
    RTN_AddInstrumentFunction(Routine,nullptr);
    INS_AddInstrumentFunction(Instruction,nullptr);
    PIN_AddFiniFunction(Finish,nullptr);
    PIN_StartProgram();
    return 0;
}
