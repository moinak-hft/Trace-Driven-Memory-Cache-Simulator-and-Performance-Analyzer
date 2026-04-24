#include "pin.H"
#include <cstdio>
#include <string>

using namespace std;

FILE *TraceFile = nullptr;

KNOB<string> KnobOutput(KNOB_MODE_WRITEONCE, "pintool", "o", "pinatrace.out", "output trace file");

VOID RecordMemRead(VOID *ip, VOID *addr)
{
    fprintf(TraceFile, "R %p %p\n", ip, addr);
}

VOID RecordMemWrite(VOID *ip, VOID *addr)
{
    fprintf(TraceFile, "W %p %p\n", ip, addr);
}

VOID Instruction(INS ins, VOID *v)
{
    UINT32 memOperands = INS_MemoryOperandCount(ins);

    for (UINT32 memOp = 0; memOp < memOperands; memOp++) {
        if (INS_MemoryOperandIsRead(ins, memOp)) {
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead,
                IARG_INST_PTR,
                IARG_MEMORYOP_EA, memOp,
                IARG_END);
        }

        if (INS_MemoryOperandIsWritten(ins, memOp)) {
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR)RecordMemWrite,
                IARG_INST_PTR,
                IARG_MEMORYOP_EA, memOp,
                IARG_END);
        }
    }
}

VOID Fini(INT32 code, VOID *v)
{
    if (TraceFile) fclose(TraceFile);
}

int main(int argc, char *argv[])
{
    if (PIN_Init(argc, argv)) return 1;

    TraceFile = fopen(KnobOutput.Value().c_str(), "w");
    if (!TraceFile) return 1;

    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);

    PIN_StartProgram();
    return 0;
}
