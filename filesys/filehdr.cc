// filehdr.cc
//	Routines for managing the disk file header (in UNIX, this
//	would be called the i-node).
//
//	The file header is used to locate where on disk the
//	file's data is stored.  We implement this as a fixed size
//	table of pointers -- each entry in the table points to the
//	disk sector containing that portion of the file data
//	(in other words, there are no indirect or doubly indirect
//	blocks). The table size is chosen so that the file header
//	will be just big enough to fit in one disk sector,
//
//      Unlike in a real system, we do not keep track of file permissions,
//	ownership, last modification date, etc., in the file header.
//
//	A file header can be initialized in two ways:
//	   for a new file, by modifying the in-memory data structure
//	     to point to the newly allocated data blocks
//	   for a file already on disk, by reading the file header from disk
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"

#include "system.h"
#include "filehdr.h"

//----------------------------------------------------------------------
// FileHeader::Allocate
// 	Initialize a fresh file header for a newly created file.
//	Allocate data blocks for the file out of the map of free disk blocks.
//	Return FALSE if there are not enough free blocks to accomodate
//	the new file.
//
//	"freeMap" is the bit map of free disk sectors
//	"fileSize" is the bit map of free disk sectors
//----------------------------------------------------------------------

bool FileHeader::Allocate(BitMap *freeMap, int fileSize)
{
    numBytes = fileSize;
    numSectors = divRoundUp(fileSize, SectorSize);
    if (numSectors > NumFirst) //需要二级索引
        return AllocateWithIndex(freeMap, fileSize);
    useIndex = false;
    if (freeMap->NumClear() < numSectors)
        return FALSE; // not enough space

    for (int i = 0; i < numSectors; i++)
        dataSectors[i] = freeMap->Find();
    return TRUE;
}

bool FileHeader::AllocateWithIndex(BitMap *freeMap, int fileSize)
{
    useIndex = true;
    numBytes = fileSize;
    numSectors = divRoundUp(fileSize, SectorSize);
    int numNode = divRoundUp(numSectors, NumSecond);
    if (freeMap->NumClear() < numSectors + numNode)
        return FALSE; // not enough space
    for (int i = 0; i < numNode; i++)
    {
        int sector = freeMap->Find();
        dataSectors[i] = sector;
        SecondNode *node = new SecondNode;
        if (i < numNode - 1)
            for (int j = 0; j < NumSecond; j++)
                node->dataSectors[j] = freeMap->Find();
        else
            for (int j = 0; j < numSectors - NumSecond * (numNode - 1); j++)
                node->dataSectors[j] = freeMap->Find();
        synchDisk->WriteSector(sector, (char *)node); //保存二级节点
        delete node;
    }
    return TRUE;
}

//----------------------------------------------------------------------
// FileHeader::Deallocate
// 	De-allocate all the space allocated for data blocks for this file.
//
//	"freeMap" is the bit map of free disk sectors
//----------------------------------------------------------------------

void FileHeader::Deallocate(BitMap *freeMap)
{
    if (useIndex)
    {
        DeallocateWithIndex(freeMap);
        return;
    }
    for (int i = 0; i < numSectors; i++)
    {
        ASSERT(freeMap->Test((int)dataSectors[i])); // ought to be marked!
        freeMap->Clear((int)dataSectors[i]);
    }
}

void FileHeader::DeallocateWithIndex(BitMap *freeMap)
{
    int numNode = divRoundUp(numSectors, NumSecond); //索引节点数量
    for (int i = 0; i < numNode; i++)
    {
        SecondNode *snode = new SecondNode;
        synchDisk->ReadSector(dataSectors[i], (char *)snode);
        if (i < numNode - 1)
            for (int j = 0; j < NumSecond; j++)
            {
                ASSERT(freeMap->Test((int)snode->dataSectors[j]));
                freeMap->Clear((int)snode->dataSectors[j]);
            }
        else
            for (int j = 0; j < numSectors - NumSecond * (numNode - 1); j++)
            {
                ASSERT(freeMap->Test((int)snode->dataSectors[j]));
                freeMap->Clear((int)snode->dataSectors[j]);
            }
        ASSERT(freeMap->Test((int)dataSectors[i]));
        freeMap->Clear((int)dataSectors[i]);
        delete snode;
    }
}

//----------------------------------------------------------------------
// FileHeader::FetchFrom
// 	Fetch contents of file header from disk.
//
//	"sector" is the disk sector containing the file header
//----------------------------------------------------------------------

void FileHeader::FetchFrom(int sector)
{
    synchDisk->ReadSector(sector, (char *)this);
}

//----------------------------------------------------------------------
// FileHeader::WriteBack
// 	Write the modified contents of the file header back to disk.
//
//	"sector" is the disk sector to contain the file header
//----------------------------------------------------------------------

void FileHeader::WriteBack(int sector)
{
    synchDisk->WriteSector(sector, (char *)this);
}

//----------------------------------------------------------------------
// FileHeader::ByteToSector
// 	Return which disk sector is storing a particular byte within the file.
//      This is essentially a translation from a virtual address (the
//	offset in the file) to a physical address (the sector where the
//	data at the offset is stored).
//
//	"offset" is the location within the file of the byte in question
//----------------------------------------------------------------------

int FileHeader::ByteToSector(int offset)
{
    if (useIndex)
        return ByteToSectorWithIndex(offset);
    return (dataSectors[offset / SectorSize]);
}

int FileHeader::ByteToSectorWithIndex(int offset)
{
    int whichSector = offset / SectorSize;   //要读取的是第几个扇区
    int whichNode = whichSector / NumSecond; //要读取第几个二级索引块
    SecondNode *node = new SecondNode;
    synchDisk->ReadSector(dataSectors[whichNode], (char *)node); //读取二级索引块
    int sector = node->dataSectors[whichSector - whichNode * NumSecond];
    delete node;
    return sector;
}

//----------------------------------------------------------------------
// FileHeader::FileLength
// 	Return the number of bytes in the file.
//----------------------------------------------------------------------

int FileHeader::FileLength()
{
    return numBytes;
}

//----------------------------------------------------------------------
// FileHeader::Print
// 	Print the contents of the file header, and the contents of all
//	the data blocks pointed to by the file header.
//----------------------------------------------------------------------
void FileHeader::Print()
{
    if (useIndex)
    {
        PrintWithIndex();
        return;
    }
    int i, j, k;
    char *data = new char[SectorSize];

    printf("FileHeader contents.  File size: %d.  File blocks:", numBytes);
    for (i = 0; i < numSectors; i++)
        printf("%d ", dataSectors[i]);
    printf("\nFile contents:\n");
    for (i = k = 0; i < numSectors; i++)
    {
        synchDisk->ReadSector(dataSectors[i], data);
        for (j = 0; (j < SectorSize) && (k < numBytes); j++, k++)
        {
            if ('\040' <= data[j] && data[j] <= '\176') // isprint(data[j])
                printf("%c", data[j]);
            else
                printf("\\%x", (unsigned char)data[j]);
        }
        printf("\n");
    }
    delete[] data;
}

void FileHeader::PrintWithIndex()
{
    int i, j, k;
    char *data = new char[SectorSize];

    printf("FileHeader contents.  File size: %d.\n", numBytes);
    int numNode = divRoundUp(numSectors, NumSecond); //计算二级节点数
    for (i = 0; i < numNode; i++)
    {
        printf("First file blocks:%d,second file blocks:", dataSectors[i]);
        SecondNode *node = new SecondNode;
        synchDisk->ReadSector(dataSectors[i], (char *)node);
        if (i < numNode - 1)
            for (j = 0; j < NumSecond; j++)
                printf("%d ", node->dataSectors[j]);
        else
            for (j = 0; j < numSectors - NumSecond * (numNode - 1); j++)
                printf("%d ", node->dataSectors[j]);
        delete node;
    }
    printf("\nFile contents:\n");
    for (int l = 0; l < numNode; l++)
    {
        k = 0; //记录已打印字符数
        SecondNode *node = new SecondNode;
        synchDisk->ReadSector(dataSectors[l], (char *)node);
        if (l < numNode - 1)
        { //打印前面的二级节点
            for (i = 0; i < NumSecond; i++)
            {
                synchDisk->ReadSector(node->dataSectors[i], data);
                for (j = 0; (j < SectorSize); j++, k++)
                {
                    if ('\040' <= data[j] && data[j] <= '\176') // isprint(data[j])
                        printf("%c", data[j]);
                    else
                        printf("\\%x", (unsigned char)data[j]);
                }
                printf("\n");
            }
        }
        else
        { //打印最后一个二级节点
            for (i = 0; i < numSectors - NumSecond * (numNode - 1); i++)
            {
                synchDisk->ReadSector(node->dataSectors[i], data);
                for (j = 0; (j < SectorSize) && (k < numBytes); j++, k++)
                {
                    if ('\040' <= data[j] && data[j] <= '\176') // isprint(data[j])
                        printf("%c", data[j]);
                    else
                        printf("\\%x", (unsigned char)data[j]);
                }
                printf("\n");
            }
        }
        delete node;
    }

    delete[] data;
}

bool FileHeader::addLength(int addBytes, int headSector, OpenFile *freeMapFile)
{
    //文件增加大小
    numBytes += addBytes;
    int oldNumSectors = numSectors;                //原来的扇区数
    numSectors = divRoundUp(numBytes, SectorSize); //新的扇区数
    if (numSectors - oldNumSectors == 0)
    {
        this->WriteBack(headSector); //写回文件头
        return true;                 //不需要新增扇区，只改变文件长度
    }
    //需要新增扇区
    BitMap *freeMap = new BitMap(NumSectors);
    freeMap->FetchFrom(freeMapFile);
    if (numSectors <= NumFirst)
    { //不需要二级索引
        DEBUG('f',"扩充大小:无二级索引\n");
        for (int i = oldNumSectors; i < numSectors; i++)
            dataSectors[i] = freeMap->Find();
        freeMap->WriteBack(freeMapFile); //写回空位图
        delete freeMap;
        this->WriteBack(headSector); //写回文件头
        return true;
    }
    if (numSectors > NumFirst && !useIndex)
    { //需要建立二级索引
        DEBUG('f',"扩充大小:使用二级索引\n");
        SecondNode *snode = new SecondNode;
        int sec = freeMap->Find();
        for (int i = 0; i < oldNumSectors; i++)
            snode->dataSectors[i] = dataSectors[i];
        dataSectors[0] = sec;
        synchDisk->WriteSector(sec, (char *)snode);
        useIndex = true;
        delete snode;
    }
    int oldNumNode = divRoundUp(oldNumSectors, NumSecond); //原来的索引节点数
    int numNode = divRoundUp(numSectors, NumSecond);       //新的索引节点数
    int needNode = numNode - oldNumNode;                   //需要新分配的索引节点数
    if (oldNumSectors % NumSecond != 0)
    { //如果可能，填充原来的最后一个索引节点剩余部分
        SecondNode *snode = new SecondNode;
        synchDisk->ReadSector(dataSectors[oldNumNode - 1], (char *)snode);

        for (int i = oldNumSectors - (oldNumNode - 1) * NumSecond; i < NumSecond && i < numSectors - (oldNumNode - 1) * NumSecond; i++)
        {
            DEBUG('f',"扩充大小:填充原来的最后一个索引节点剩余部分\n");
            snode->dataSectors[i] = freeMap->Find(); //填充第一个索引节点的剩余部分
        }
        synchDisk->WriteSector(dataSectors[oldNumNode - 1], (char *)snode);
        delete snode;
    }
    for (int i = oldNumNode; i < numNode; i++)
    {
        //printf("扩充大小:新建索引节点\n");
        SecondNode *snode = new SecondNode;
        int sec = freeMap->Find();
        dataSectors[i] = sec; //为索引节点寻找新扇区
        if (i < numNode - 1)
            for (int j = 0; j < NumSecond; j++)
                snode->dataSectors[j] = freeMap->Find();
        else
            for (int j = 0; j < numSectors - NumSecond * (numNode - 1); j++)
                snode->dataSectors[j] = freeMap->Find();
        synchDisk->WriteSector(sec, (char *)snode); //写回索引节点信息
    }
    this->WriteBack(headSector);     //写回文件头
    freeMap->WriteBack(freeMapFile); //写回空位图
    delete freeMap;
    return true;
}