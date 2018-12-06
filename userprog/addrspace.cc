// addrspace.cc
//	Routines to manage address spaces (executing user programs).
//
//	In order to run a user program, you must:
//
//	1. link with the -N -T 0 option
//	2. run coff2noff to convert the object file to Nachos format
//		(Nachos object code format is essentially just a simpler
//		version of the UNIX executable object code format)
//	3. load the NOFF file into the Nachos file system
//		(if you haven't implemented the file system yet, you
//		don't need to do this last step)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "addrspace.h"
#include "noff.h"

//----------------------------------------------------------------------
// SwapHeader
// 	Do little endian to big endian conversion on the bytes in the
//	object file header, in case the file was generated on a little
//	endian machine, and we're now running on a big endian machine.
//----------------------------------------------------------------------

static void
SwapHeader(NoffHeader *noffH)
{
    noffH->noffMagic = WordToHost(noffH->noffMagic);
    noffH->code.size = WordToHost(noffH->code.size);
    noffH->code.virtualAddr = WordToHost(noffH->code.virtualAddr);
    noffH->code.inFileAddr = WordToHost(noffH->code.inFileAddr);
    noffH->initData.size = WordToHost(noffH->initData.size);
    noffH->initData.virtualAddr = WordToHost(noffH->initData.virtualAddr);
    noffH->initData.inFileAddr = WordToHost(noffH->initData.inFileAddr);
    noffH->uninitData.size = WordToHost(noffH->uninitData.size);
    noffH->uninitData.virtualAddr = WordToHost(noffH->uninitData.virtualAddr);
    noffH->uninitData.inFileAddr = WordToHost(noffH->uninitData.inFileAddr);
}

//----------------------------------------------------------------------
// AddrSpace::AddrSpace 地址空间
// 	Create an address space to run a user program.
//	Load the program from a file "executable", and set everything
//	up so that we can start executing user instructions.
//
//	Assumes that the object code file is in NOFF format.
//
//	First, set up the translation from program memory to physical
//	memory.  For now, this is really simple (1:1), since we are
//	only uniprogramming, and we have a single unsegmented page table
//
//	"executable" is the file containing the object code to load into memory
//----------------------------------------------------------------------

AddrSpace::AddrSpace(OpenFile *executable)
{
    NoffHeader noffH;
    unsigned int i, size;

    executable->ReadAt((char *)&noffH, sizeof(noffH), 0);
    if ((noffH.noffMagic != NOFFMAGIC) && //将小端切换大端
        (WordToHost(noffH.noffMagic) == NOFFMAGIC))
        SwapHeader(&noffH);
    ASSERT(noffH.noffMagic == NOFFMAGIC);

    // how big is address space?
    size = noffH.code.size + noffH.initData.size + noffH.uninitData.size + UserStackSize; // we need to increase the size
                                                                                          // to leave room for the stack
    numPages = divRoundUp(size, PageSize);
    size = numPages * PageSize;

    ASSERT(numPages <= NumPhysPages); // check we're not trying
                                      // to run anything too big --
                                      // at least until we have
                                      // virtual memory

    DEBUG('a', "Initializing address space, num pages %d, size %d\n",
          numPages, size);
    // first, set up the translation
    pageTable = new TranslationEntry[numPages]; //新建页表
    for (i = 0; i < numPages; i++)
    {
        pageTable[i].virtualPage = i; // for now, virtual page # = phys page #
        pageTable[i].physicalPage = machine->freeFrame->Find();
        ASSERT(pageTable[i].physicalPage != -1);
        pageTable[i].valid = TRUE;
        pageTable[i].use = FALSE;
        pageTable[i].dirty = FALSE;
        pageTable[i].readOnly = FALSE; // if the code segment was entirely on
                                       // a separate page, we could set its
                                       // pages to be read-only
    }
    pid = machine->threadMap->Find();
    // zero out the entire address space, to zero the unitialized data segment
    // and the stack segment
    bzero(machine->mainMemory + pageTable[0].physicalPage * PageSize, size);

    // then, copy in the code and data segments into memory读入代码段，数据段
    //以下代码假设页表在物理上是连续的
    if (noffH.code.size > 0)
    {
        DEBUG('a', "Initializing code segment, at 0x%x, size %d\n",
              noffH.code.virtualAddr, noffH.code.size);
        if (noffH.code.size <= PageSize)
        { //仅需一页
            executable->ReadAt(&(machine->mainMemory[noffH.code.virtualAddr % PageSize + pageTable[noffH.code.virtualAddr / PageSize].physicalPage * PageSize]),
                               noffH.code.size, noffH.code.inFileAddr);
        }
        else
        {                                                          //需要多页
            int codePages = divRoundUp(noffH.code.size, PageSize); //code段需要的页数
            //第一页
            int hasRead = 0; //已经读入的字节数
            int firstPage = noffH.code.virtualAddr / PageSize;
            executable->ReadAt(&(machine->mainMemory[noffH.code.virtualAddr % PageSize + pageTable[firstPage].physicalPage * PageSize]),
                               PageSize - noffH.code.virtualAddr % PageSize, noffH.code.inFileAddr);
            hasRead += PageSize - noffH.code.virtualAddr % PageSize;
            //中间页
            for (int i = firstPage + 1; i < firstPage + codePages - 1; i++)
            {
                executable->ReadAt(&(machine->mainMemory[pageTable[i].physicalPage * PageSize]),
                                   PageSize, noffH.code.inFileAddr + hasRead);
                hasRead += PageSize;
            }
            //最后一页
            executable->ReadAt(&(machine->mainMemory[pageTable[firstPage + codePages - 1].physicalPage * PageSize]),
                               noffH.code.size - hasRead, noffH.code.inFileAddr + hasRead);
        }
        // executable->ReadAt(&(machine->mainMemory[noffH.code.virtualAddr + pageTable[0].physicalPage * PageSize]),
        //                    noffH.code.size, noffH.code.inFileAddr);
    }
    if (noffH.initData.size > 0)
    {
        DEBUG('a', "Initializing data segment, at 0x%x, size %d\n",
              noffH.initData.virtualAddr, noffH.initData.size);
        if (noffH.initData.size <= PageSize)
        { //仅需一页
            executable->ReadAt(&(machine->mainMemory[noffH.initData.virtualAddr % PageSize + pageTable[noffH.initData.virtualAddr / PageSize].physicalPage * PageSize]),
                               noffH.initData.size, noffH.initData.inFileAddr);
        }
        else
        {                                                          //需要多页
            int dataPages = divRoundUp(noffH.initData.size, PageSize); //initData段需要的页数
            //第一页
            int hasRead = 0; //已经读入的字节数
            int firstPage = noffH.initData.virtualAddr / PageSize;
            executable->ReadAt(&(machine->mainMemory[noffH.initData.virtualAddr % PageSize + pageTable[firstPage].physicalPage * PageSize]),
                               PageSize - noffH.initData.virtualAddr % PageSize, noffH.code.inFileAddr);
            hasRead += PageSize - noffH.initData.virtualAddr % PageSize;
            //中间页
            for (int i = firstPage + 1; i < firstPage + dataPages - 1; i++)
            {
                executable->ReadAt(&(machine->mainMemory[pageTable[i].physicalPage * PageSize]),
                                   PageSize, noffH.initData.inFileAddr + hasRead);
                hasRead += PageSize;
            }
            //最后一页
            executable->ReadAt(&(machine->mainMemory[pageTable[firstPage + dataPages - 1].physicalPage * PageSize]),
                               noffH.initData.size - hasRead, noffH.initData.inFileAddr + hasRead);
        }
        // executable->ReadAt(&(machine->mainMemory[noffH.initData.virtualAddr + pageTable[0].physicalPage * PageSize]),
        //                    noffH.initData.size, noffH.initData.inFileAddr);
    }
}

//----------------------------------------------------------------------
// AddrSpace::~AddrSpace
// 	Dealloate an address space.  Nothing for now!
//----------------------------------------------------------------------

AddrSpace::~AddrSpace()
{
    machine->threadMap->Clear(pid);
    for (int i = 0; i < numPages; i++)
        machine->freeFrame->Clear(pageTable[i].physicalPage);
    delete[] pageTable;
}

//----------------------------------------------------------------------
// AddrSpace::InitRegisters
// 	Set the initial values for the user-level register set.
//
// 	We write these directly into the "machine" registers, so
//	that we can immediately jump to user code.  Note that these
//	will be saved/restored into the currentThread->userRegisters
//	when this thread is context switched out.
//----------------------------------------------------------------------

void AddrSpace::InitRegisters()
{
    int i;

    for (i = 0; i < NumTotalRegs; i++)
        machine->WriteRegister(i, 0);

    // Initial program counter -- must be location of "Start"
    machine->WriteRegister(PCReg, 0);

    // Need to also tell MIPS where next instruction is, because
    // of branch delay possibility
    machine->WriteRegister(NextPCReg, 4);

    // Set the stack register to the end of the address space, where we
    // allocated the stack; but subtract off a bit, to make sure we don't
    // accidentally reference off the end!
    machine->WriteRegister(StackReg, numPages * PageSize - 16);
    DEBUG('a', "Initializing stack register to %d\n", numPages * PageSize - 16);
}

//----------------------------------------------------------------------
// AddrSpace::SaveState
// 	On a context switch, save any machine state, specific
//	to this address space, that needs saving.
//
//	For now, nothing!
//----------------------------------------------------------------------

void AddrSpace::SaveState()
{
    //保存寄存器状态
    for (int i = 0; i < NumTotalRegs; i++)
        regState[i] = machine->ReadRegister(i);
}

//----------------------------------------------------------------------
// AddrSpace::RestoreState
// 	On a context switch, restore the machine state so that
//	this address space can run.
//
//      For now, tell the machine where to find the page table.
//----------------------------------------------------------------------

void AddrSpace::RestoreState()
{
    for (int i = 0; i < NumTotalRegs; i++)
        machine->WriteRegister(i, regState[i]);
    machine->pageTable = pageTable;
    machine->pageTableSize = numPages;
}

void AddrSpace::Print()
{
    printf("page table dump: %d pages in total\n", numPages);
    printf("===========================================\n");
    printf("\tVirtPage, \tPhysPage\n");

    for (int i = 0; i < numPages; i++)
    {
        printf("\t%d, \t\t%d\n", pageTable[i].virtualPage, pageTable[i].physicalPage);
    }
    printf("===========================================\n\n");
}