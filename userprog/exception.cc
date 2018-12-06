// exception.cc
//	Entry point into the Nachos kernel from user programs.
//	There are two kinds of things that can cause control to
//	transfer back to here from user code:
//
//	syscall -- The user code explicitly requests to call a procedure
//	in the Nachos kernel.  Right now, the only function we support is
//	"Halt".
//
//	exceptions -- The user code does something that the CPU can't handle.
//	For instance, accessing memory that doesn't exist, arithmetic errors,
//	etc.
//
//	Interrupts (which can also cause control to transfer from user
//	code into the Nachos kernel) are handled elsewhere.
//
// For now, this only handles the Halt() system call.
// Everything else core dumps.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "syscall.h"

extern void StartProcess(int spaceId);
void AdvancePC();
//----------------------------------------------------------------------
// ExceptionHandler
// 	Entry point into the Nachos kernel.  Called when a user program
//	is executing, and either does a syscall, or generates an addressing
//	or arithmetic exception.
//
// 	For system calls, the following is the calling convention:
//
// 	system call code -- r2
//		arg1 -- r4
//		arg2 -- r5
//		arg3 -- r6
//		arg4 -- r7
//
//	The result of the system call, if any, must be put back into r2.
//
// And don't forget to increment the pc before returning. (Or else you'll
// loop making the same system call forever!
//
//	"which" is the kind of exception.  The list of possible exceptions
//	are in machine.h.
//----------------------------------------------------------------------

void ExceptionHandler(ExceptionType which)
{
    int type = machine->ReadRegister(2);

    if ((which == SyscallException))
    {
        switch (type)
        {
        case SC_Halt:
        {
            DEBUG('a', "执行Halt系统调用，停机\n");
            interrupt->Halt();
            break;
        }
        case SC_Exec:
        {
            DEBUG('a', "执行Exec系统调用，运行新程序\n");
            char *filename = new char[128];
            int addr = machine->ReadRegister(4); //读取文件名地址
            int i = 0;
            do
            {
                machine->ReadMem(addr + i, 1, (int *)&filename[i]);
            } while (filename[i++] != '\0');
            DEBUG('a',"执行程序:%s\n", filename);
            OpenFile *executable = fileSystem->Open(filename);
            if (executable == NULL)
            {
                printf("无法打开程序 %s\n", filename);
                AdvancePC();
                return;
            }
            AddrSpace *space = new AddrSpace(executable);
            delete executable;
            Thread *thread = new Thread(filename);
            thread->Fork(StartProcess, space->getPid());
            thread->space = space;
            machine->WriteRegister(2, space->getPid());
            AdvancePC();
            currentThread->Yield();
            break;
        }
        case SC_Exit:
        {
            DEBUG('a', "执行Exit系统调用，程序退出\n");
            machine->WriteRegister(2, machine->ReadRegister(4));
            AdvancePC();
            currentThread->Finish();
            break;
        }
        case SC_Yield:
        {
            AdvancePC();
            currentThread->Yield();
            break;
        }
        default:
            printf("Unexpected user mode exception %d %d\n", which, type);
            ASSERT(FALSE);
        }
    }
    else
    {
        printf("Unexpected user mode exception %d %d\n", which, type);
        ASSERT(FALSE);
    }
}

void AdvancePC()
{
    //前进PC
    machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
    machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
    machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg) + 4);
}