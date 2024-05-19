#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>

#include "ix.h"

IndexManager* IndexManager::_index_manager = 0;
PagedFileManager *RecordBasedFileManager::_pf_manager = NULL;

IndexManager* IndexManager::instance()
{
    if(!_index_manager)
        _index_manager = new IndexManager();

    return _index_manager;
}

IndexManager::IndexManager()
{
}

IndexManager::~IndexManager()
{
}

RC IndexManager::createFile(const string &fileName)
{
    // // Creating a new paged file.
    // if (_pf_manager->createFile(fileName))
    //     return IX_CREATE_FAILED;

    if (fileExists(fileName))
        return IX_FILE_EXISTS;

    // Attempt to open the file for writing
    FILE *pFile = fopen(fileName.c_str(), "wb");
    // Return an error if we fail
    if (pFile == NULL)
        return IX_OPEN_FAILED;

    //-------------------------

    // Setting up the header page.
    void * headerPageData = calloc(PAGE_SIZE, 1);
    if (headerPageData == NULL)
        return IX_MALLOC_FAILED;

    // TODO: Implement helper function.
    newHeaderPage(headerPageData);

    // Adds the header data page.
    IXFileHandle ixfileHandle;
    if (ixfileHandle.appendPage(headerPageData))
        return IX_APPEND_FAILED;

    free(headerPageData);

    void * firstInternalPageData = calloc(PAGE_SIZE, 1);
    if (firstInternalPageData == NULL)
        return IX_MALLOC_FAILED;
    
    newInternalPage(firstInternalPageData, -1/* leftChildPageNum */);

    if (ixfileHandle.appendPage(firstInternalPageData))
        return IX_APPEND_FAILED;

    fclose (pFile);
    free(firstInternalPageData);

    return SUCCESS;
}

RC IndexManager::destroyFile(const string &fileName)
{
    // If file cannot be successfully removed, error
    if (remove(fileName.c_str()) != 0)
        return IX_REMOVE_FAILED;

    return SUCCESS;
}

RC IndexManager::openFile(const string &fileName, IXFileHandle &ixfileHandle)
{
    // If this handle already has an open file, error
    if (ixfileHandle.getfd() != NULL)
        return IX_HANDLE_IN_USE;

    // If the file doesn't exist, error
    if (!fileExists(fileName.c_str()))
        return IX_FILE_DN_EXIST;

    // Open the file for reading/writing in binary mode
    FILE *pFile;
    pFile = fopen(fileName.c_str(), "rb+");
    // If we fail, error
    if (pFile == NULL)
        return IX_OPEN_FAILED;

    ixfileHandle.setfd(pFile);

    return SUCCESS;
}

RC IndexManager::closeFile(IXFileHandle &ixfileHandle)
{
    FILE *pFile = ixfileHandle.getfd();

    // If not an open file, error
    if (pFile == NULL)
        return 1;

    // Flush and close the file
    fclose(pFile);

    ixfileHandle.setfd(NULL);

    return SUCCESS;
}

RC IndexManager::insertEntry(IXFileHandle &ixfileHandle, const Attribute &attribute, const void *key, const RID &rid)
{  
    // First create data entry to be inserted
    IndexDataEntry newDataEntry;
    unsigned rootPageNum = getRootPage(ixfileHandle); 

    return insert(attribute, key, rid, ixfileHandle, newDataEntry, rootPageNum); // Will recursively walk down tree until it finds leaf page to insert
}

RC IndexManager::deleteEntry(IXFileHandle &ixfileHandle, const Attribute &attribute, const void *key, const RID &rid)
{
    return -1;
}


RC IndexManager::scan(IXFileHandle &ixfileHandle,
        const Attribute &attribute,
        const void      *lowKey,
        const void      *highKey,
        bool			lowKeyInclusive,
        bool        	highKeyInclusive,
        IX_ScanIterator &ix_ScanIterator)
{
    return -1;
}

void IndexManager::printBtree(IXFileHandle &ixfileHandle, const Attribute &attribute) const {
}

IX_ScanIterator::IX_ScanIterator()
{
}

IX_ScanIterator::~IX_ScanIterator()
{
}

RC IX_ScanIterator::getNextEntry(RID &rid, void *key)
{
    return -1;
}

RC IX_ScanIterator::close()
{
    return -1;
}


IXFileHandle::IXFileHandle()
{
    ixReadPageCounter = 0;
    ixWritePageCounter = 0;
    ixAppendPageCounter = 0;
}

IXFileHandle::~IXFileHandle()
{
}

RC IXFileHandle::collectCounterValues(unsigned &readPageCount, unsigned &writePageCount, unsigned &appendPageCount)
{
    readPageCount = ixReadPageCounter;
    writePageCount = ixWritePageCounter;
    appendPageCount = ixAppendPageCounter;
    return SUCCESS;
}

void IndexManager::newHeaderPage(void *pageData)
{
    memset(pageData, 0, PAGE_SIZE);
    // Write the header page struct.
    MetaDataHeader metaHeader;
    metaHeader.rootPageNum = 1;
    setMetaDataHeader(pageData, metaHeader);
}

void IndexManager::setMetaDataHeader(void *pageData, MetaDataHeader metaHeader)
{
    // Setting the metadata header.
    memcpy(pageData, &metaHeader, sizeof(MetaDataHeader));
}

MetaDataHeader IndexManager::getMetaDataHeader(void *pageData)
{
    // Getting the metadata header.
    MetaDataHeader metaHeader;
    memcpy(&metaHeader, pageData, sizeof(MetaDataHeader));
    return metaHeader;
}

void IndexManager::newInternalPage(void *pageData, int leftChildPageNum)
{
    memset(pageData, 0, PAGE_SIZE);
    // Write internal page header.
    IndexHeader internalHeader;
    internalHeader.leaf = false;
    internalHeader.dataEntryNumber = 0;
    internalHeader.freeSpaceOffset = PAGE_SIZE;
    internalHeader.leftChildPageNum = leftChildPageNum;
    // Initialize values, even if they aren't needed.
    // Page num 0 will always be the meta data page, so this is an invalid internal or leaf page.
    internalHeader.nextSiblingPageNum = 0;
    internalHeader.prevSiblingPageNum = 0;
    setIndexHeader(pageData, internalHeader);
}

void IndexManager::newLeafPage(void *pageData, int nextSiblingPageNum, int prevSiblingPageNum)
{
    memset(pageData, 0, PAGE_SIZE);
    // Write Leaf Header struct.
    IndexHeader newHeader;
    newHeader.leaf = true;
    newHeader.dataEntryNumber = 0;
    newHeader.nextSiblingPageNum = nextSiblingPageNum;
    newHeader.prevSiblingPageNum = prevSiblingPageNum;
    newHeader.freeSpaceOffset = PAGE_SIZE;
    // Initialize values, even if they aren't needed.
    // Page num 0 will always be the meta data page, so this is an invalid internal or leaf page.
    newHeader.leftChildPageNum = 0;
    setIndexHeader(pageData, newHeader);
}

void IndexManager::setIndexHeader(void *pageData, IndexHeader indexHeader)
{
    // Set the index header.
    memcpy(pageData, &indexHeader, sizeof(IndexHeader));
}

IndexHeader IndexManager::getIndexHeader(void *pageData)
{
    // Get Index Header.
    IndexHeader indexHeader;
    memcpy(&indexHeader, pageData, sizeof(IndexHeader));
    return indexHeader;
}

void IndexManager::setIndexDataEntry(void *pageData, unsigned indexEntryNumber, IndexDataEntry dataEntry)
{
    // Setting the index data entry.
    memcpy(
            ((char *) pageData + sizeof(IndexHeader) + (indexEntryNumber * sizeof(IndexDataEntry))),
            &dataEntry,
            sizeof(IndexDataEntry)
          );
}

IndexDataEntry IndexManager::getIndexDataEntry(void *pageData, unsigned indexEntryNumber)
{
    IndexDataEntry dataEntry;
    memcpy(
            &dataEntry,
            ((char *) pageData + sizeof(IndexHeader) + (indexEntryNumber * sizeof(IndexDataEntry))),
            sizeof(IndexDataEntry)
          );
    return dataEntry;
}

unsigned IndexManager::getPageFreeSpaceSize(void *pageData)
{
    IndexHeader indexHeader = getIndexHeader(pageData);
    return indexHeader.freeSpaceOffset - (indexHeader.dataEntryNumber * sizeof(IndexDataEntry) - sizeof(IndexHeader));
}

RC IndexManager::insert(const Attribute &attr, const void *key, const RID &rid, IXFileHandle &fileHandle, IndexDataEntry &newIndexDataEntry, unsigned pageNum){
    void *pageData = malloc(PAGE_SIZE);

    if (fileHandle.readPage(pageNum, pageData) != SUCCESS){ // Assuming that IXFileHandle has identical methods as other FileHandle class
        free(pageData);
        return IX_READ_FAILED;
    }
    
    // Checks if current page is a leaf or internal page
    if (isNonLeaf(pageData)){ // If internal page, get child page, and recursively call insert
        unsigned childPageNum = getChildPageNum(pageData, key, attr);
        if (childPageNum == -2) return IX_EXISTING_ENTRY; 
        if (childPageNum == -3) return IX_NO_SUCH_ATTR;
        
        RC rc = insert(attr, key, rid, fileHandle, newIndexDataEntry, childPageNum); 
        if (newIndexDataEntry.key != NULL){
            rc = insertInInternal(pageData, pageNum, newIndexDataEntry); // This will be called at every backtrack level until we find space to insert 
            if (rc == IX_INTERNAL_SPLIT){
                rc = splitInternal(pageData, newIndexDataEntry);
            }
        }
    } else { // If leaf page, attempt to insert data entry in leaf
        RC rc = insertInLeaf(attr, key, rid, pageData); // TO-DO: Should return 0 if successfully inserted, return IX_LEAF_SPLIT if split is needed 
        if (rc == IX_LEAF_SPLIT) {
            rc = splitLeaf(pageData, newIndexDataEntry); // TO-DO: Should split leaf page into two, find in parent if needed.
        }
        fileHandle.writePage(pageNum, pageData);
    }

    free(pageData);
    return SUCCESS;
}

unsigned IndexManager::getRootPage(IXFileHandle &fileHandle){
    void *pageData = malloc(PAGE_SIZE);
    if (fileHandle.readPage(0, pageData) != SUCCESS){ // Assumption that the meta page is on page 0 of the file
        free(pageData);
        return IX_READ_FAILED;
    }

    MetaDataHeader metaHeader = getMetaDataHeader(pageData);
    unsigned rootPageNum = metaHeader.rootPageNum;
    free(pageData);
    return rootPageNum;
}

bool IndexManager::isNonLeaf(void *pageData){
    IndexHeader header = getIndexHeader(pageData);
    return !header.leaf; // Will return true of it is a non-leaf page
}

unsigned IndexManager::getChildPageNum(void *pageData, const void *key, const Attribute &attr){
    IndexHeader header = getIndexHeader(pageData);
    unsigned offset = sizeof(IndexHeader);
    uint32_t lastChildPage = header.leftChildPageNum;

    for (unsigned i = 0; i < header.dataEntryNumber; i++) {
        unsigned entryOffset = offset + i * sizeof(IndexDataEntry); // Calculates current iteration's entry offset

        // Accesses current entry to grab update child page later
        IndexDataEntry entry;
        memcpy(&entry, (char*)pageData + entryOffset, sizeof(IndexDataEntry));
    
        int result = compareKey(pageData, key, attr, entryOffset);
        if (result == -1) { // If the provided key is less than the current entry's key, return the last child page number encountered
            return lastChildPage;
        } else if (result == 0) {
            return -2; // Returns -2 for duplicate data entry
        } else if (result == -2){
            return -3;// Returns -3 for unhandled attribute type
        }

        lastChildPage = entry.rid.pageNum;
    }

    // If the key is greater than all the keys in the entries, return the last child page number
    return lastChildPage;
}

RC IndexManager::insertInInternal(void *pageData, unsigned pageNum, IndexDataEntry &newIndexDataEntry){
    // Should attempt to place internal "traffic cop" within page. If successful, set key within data entry to null 
    return -1;
}

RC IndexManager::splitInternal(void*pageData, IndexDataEntry &newIndexDataEntry){
    // Should split internal page into two
    // TODO: Should be able to combine split functionality into one split function.
    return -1;
}

RC IndexManager::insertInLeaf(const Attribute &attr, const void *key, const RID &rid, void *pageData){
    // Should return SUCCESS if new data entry is inserted into leaf page. If page is full, return IX_INSERT_SPLIT 
    return -1;
}

RC IndexManager::splitLeaf(void *pageData, IndexDataEntry &newIndexDataEntry){
    // TODO: Need to know the variable type to know if we have varchars.
    // Should split leaf into two, insert the newIndexDataEntry, and pass middle key value back in struct to signify split.
    IndexHeader indexHeader = getIndexHeader(pageData);
    // Get half of the entries for the new page.
    uint32_t numOldEntries = ceil(indexHeader.dataEntryNumber / 2);
    uint32_t numNewEntries = floor(indexHeader.dataEntryNumber / 2);

    void *newPageData = calloc(PAGE_SIZE, 1);
    if (newPageData == NULL)
        return IX_MALLOC_FAILED;
    
    // Prepare the new pages header.
    IndexHeader newHeader;
    newHeader.leaf = true;
    newHeader.dataEntryNumber = numOldEntries;
    // TODO: We need a way to tell split leaf the old page num, so we can update prevSiblingPageNum..
    newHeader.prevSiblingPageNum = 0;
    newHeader.nextSiblingPageNum = indexHeader.nextSiblingPageNum;
    newHeader.leftChildPageNum = 0;
    newHeader.freeSpaceOffset = PAGE_SIZE;
    setIndexHeader(newPageData, newHeader);

    // Copy over the data entries for the new page.
    memcpy(
            (char *)newPageData + sizeof(IndexHeader),
            (char *)pageData + sizeof(IndexHeader) + (sizeof(IndexDataEntry) * numOldEntries),
            (sizeof(IndexDataEntry) * numNewEntries)
          );
    // If varchar: call helper function to write in varchars at back of page.

    // Update the old index header.
    indexHeader.dataEntryNumber = numOldEntries;
    // TODO: Need a way to pass the file number to know what the page number of the new page will be.
    /* indexHeader.nextSiblingPageNum = ixfileHandle.getNumberOfPages; */
    // TODO: Create a temp page, write in the index header and the first numOldEntries data entries.
    // Then, check if we are varchar case and call helper function to put varchars at the back of the page.

    // We will append the new page and write back the old page.
    // Write to the struct the key of the first entry we split and put the page num of the new page in the RID.pageNum field.

    return -1;
}

RC IndexManager::compareKey(void *pageData, const void *key, const Attribute &attr, unsigned offset){
    switch(attr.type){
        case TypeInt: {
            int entryKey;
            memcpy(&entryKey, (char*)pageData + offset, sizeof(INT_SIZE));

            // Comparison of keys
            int searchKey;
            memcpy(&searchKey, key, sizeof(INT_SIZE));
            if (searchKey < entryKey) return -1; 
            if (searchKey > entryKey) return 1; 
            return 0; // Returns 0 when keys match, shouldn't happen
        }

        case TypeReal: {
            float entryKey;
            memcpy(&entryKey, (char*)pageData + offset, sizeof(REAL_SIZE));
        
            // Comparison of keys
            float searchKey;
            memcpy(&searchKey, key, sizeof(REAL_SIZE));
            if (searchKey < entryKey) return -1; 
            if (searchKey > entryKey) return 1; 
            return 0; // Returns 0 when keys match, shouldn't happen
        }

        case TypeVarChar: {
            // Get position of local varchar
            int localVarcharOffset;
            memcpy(&localVarcharOffset, (char*)pageData + offset, sizeof(INT_SIZE));

            // Access the varchar data using the offset
            int localVarcharLength;
            memcpy(&localVarcharLength, (char*)pageData + localVarcharOffset, sizeof(INT_SIZE));
            char *localString = (char*)malloc(localVarcharLength + 1);
            memcpy(localString, (char*)pageData + localVarcharOffset + sizeof(INT_SIZE), localVarcharLength);
            localString[localVarcharLength] = '\0'; // Null terminate the local string

            // Read the input varchar's length and data
            int inputVarcharLength;
            memcpy(&inputVarcharLength, key, sizeof(INT_SIZE));
            char *inputString = (char*)malloc(inputVarcharLength + 1);
            memcpy(inputString, (char*)key + sizeof(INT_SIZE), inputVarcharLength);
            inputString[inputVarcharLength] = '\0'; // Null terminate the input string

            // Compare the two strings
            int result = strcmp(localString, inputString);
            free(localString);
            free(inputString);
            return (result < 0) ? -1 : (result > 0) ? 1 : 0; // Limit return to -1,0,1 so we can send error code for unhandled attribute type
        }
    }

    return -2; // If we are thrown an unhandled attribute type
}

bool IndexManager::fileExists(const string &fileName)
{
    // If stat fails, we can safely assume the file doesn't exist
    struct stat sb;
    return stat(fileName.c_str(), &sb) == 0;
}

void IXFileHandle::setfd(FILE *fd)
{
    _fd = fd;
}

FILE *IXFileHandle::getfd()
{
    return _fd;
}

RC IXFileHandle::readPage(PageNum pageNum, void *data)
{
    // If pageNum doesn't exist, error
    if (getNumberOfPages() < pageNum)
        return FH_PAGE_DN_EXIST;

    // Try to seek to the specified page
    if (fseek(_fd, PAGE_SIZE * pageNum, SEEK_SET))
        return FH_SEEK_FAILED;

    // Try to read the specified page
    if (fread(data, 1, PAGE_SIZE, _fd) != PAGE_SIZE)
        return FH_READ_FAILED;

    ixReadPageCounter++;
    return SUCCESS;
}

RC IXFileHandle::writePage(PageNum pageNum, const void *data)
{
    // Check if the page exists
    if (getNumberOfPages() < pageNum)
        return FH_PAGE_DN_EXIST;

    // Seek to the start of the page
    if (fseek(_fd, PAGE_SIZE * pageNum, SEEK_SET))
        return FH_SEEK_FAILED;

    // Write the page
    if (fwrite(data, 1, PAGE_SIZE, _fd) == PAGE_SIZE)
    {
        // Immediately commit changes to disk
        fflush(_fd);
        ixWritePageCounter++;
        return SUCCESS;
    }
    
    return FH_WRITE_FAILED;
}

RC IXFileHandle::appendPage(const void *data)
{
    // Seek to the end of the file
    if (fseek(_fd, 0, SEEK_END))
        return FH_SEEK_FAILED;

    // Write the new page
    if (fwrite(data, 1, PAGE_SIZE, _fd) == PAGE_SIZE)
    {
        fflush(_fd);
        ixAppendPageCounter++;
        return SUCCESS;
    }

    return FH_WRITE_FAILED;
}

unsigned IXFileHandle::getNumberOfPages()
{
    // Use stat to get the file size
    struct stat sb;
    if (fstat(fileno(_fd), &sb) != 0)
        // On error, return 0
        return 0;
    // Filesize is always PAGE_SIZE * number of pages
    return sb.st_size / PAGE_SIZE;
}
