#include "rbfm.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string.h>
#include <iomanip>
// everthing is an int32_t
// setting
uint32_t bitmask = 0x80000000;
// retreiving
uint32_t retreivemask = 0x7fffffff;

RecordBasedFileManager *RecordBasedFileManager::_rbf_manager = NULL;
PagedFileManager *RecordBasedFileManager::_pf_manager = NULL;
RecordBasedFileManager *RecordBasedFileManager::instance()
{
    if (!_rbf_manager)
        _rbf_manager = new RecordBasedFileManager();
    return _rbf_manager;
}
RecordBasedFileManager::RecordBasedFileManager()
{
}
RecordBasedFileManager::~RecordBasedFileManager()
{
}
RC RecordBasedFileManager::createFile(const string &fileName)
{
    // Creating a new paged file.
    if (_pf_manager->createFile(fileName))
        return RBFM_CREATE_FAILED;
    // Setting up the first page.
    void *firstPageData = calloc(PAGE_SIZE, 1);
    if (firstPageData == NULL)
        return RBFM_MALLOC_FAILED;
    newRecordBasedPage(firstPageData);
    // Adds the first record based page.
    FileHandle handle;
    if (_pf_manager->openFile(fileName.c_str(), handle))
        return RBFM_OPEN_FAILED;
    if (handle.appendPage(firstPageData))
        return RBFM_APPEND_FAILED;
    _pf_manager->closeFile(handle);
    free(firstPageData);
    return SUCCESS;
}
RC RecordBasedFileManager::destroyFile(const string &fileName)
{
    return _pf_manager->destroyFile(fileName);
}
RC RecordBasedFileManager::openFile(const string &fileName, FileHandle &fileHandle)
{
    return _pf_manager->openFile(fileName.c_str(), fileHandle);
}
RC RecordBasedFileManager::closeFile(FileHandle &fileHandle)
{
    return _pf_manager->closeFile(fileHandle);
}
RC RecordBasedFileManager::insertRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, RID &rid)
{
    // Gets the size of the record.
    // cerr << "insertRecord: Received the following data." << endl;
    // printRecord(recordDescriptor, data);
    unsigned recordSize = getRecordSize(recordDescriptor, data);
    // Cycles through pages looking for enough free space for the new entry.
    void *pageData = malloc(PAGE_SIZE);
    if (pageData == NULL)
        return RBFM_MALLOC_FAILED;
    bool pageFound = false;
    unsigned i;
    unsigned numPages = fileHandle.getNumberOfPages();
    for (i = 0; i < numPages; i++)
    {
        if (fileHandle.readPage(i, pageData))
            return RBFM_READ_FAILED;
        // When we find a page with enough space (accounting also for the size that will be added to the slot directory), we stop the loop.
        if (getPageFreeSpaceSize(pageData) >= sizeof(SlotDirectoryRecordEntry) + recordSize)
        {
            pageFound = true;
            break;
        }
    }
    // If we can't find a page with enough space, we create a new one
    if (!pageFound)
    {
        newRecordBasedPage(pageData);
    }
    SlotDirectoryHeader slotHeader = getSlotDirectoryHeader(pageData);
    // Setting up the return RID.
    rid.pageNum = i;
    bool foundSlot = false;
    SlotDirectoryRecordEntry newRecordEntry;
    for (uint32_t slot = 0; slot < slotHeader.recordEntriesNumber; slot += 1)
    {
        newRecordEntry = getSlotDirectoryRecordEntry(pageData, slot);
        if (newRecordEntry.length < 0)
        {
            foundSlot = true;
            rid.slotNum = slot;
            break;
        }
    }
    if (!foundSlot)
        rid.slotNum = slotHeader.recordEntriesNumber;
    // Adding the new record reference in the slot directory.
    newRecordEntry.length = recordSize;
    newRecordEntry.offset = slotHeader.freeSpaceOffset - recordSize;
    setSlotDirectoryRecordEntry(pageData, rid.slotNum, newRecordEntry);
    // Updating the slot directory header.
    slotHeader.freeSpaceOffset = newRecordEntry.offset;
    if (!foundSlot)
        slotHeader.recordEntriesNumber += 1;

    setSlotDirectoryHeader(pageData, slotHeader);
    // Adding the record data.
    setRecordAtOffset(pageData, newRecordEntry.offset, recordDescriptor, data);
    // Writing the page to disk.
    if (pageFound)
    {
        if (fileHandle.writePage(i, pageData))
            return RBFM_WRITE_FAILED;
    }
    else
    {
        if (fileHandle.appendPage(pageData))
            return RBFM_APPEND_FAILED;
    }

    // cerr << "insertRecord: returned rid, pageNum: " << rid.pageNum << " slotNum: " << rid.slotNum << endl;

    // void* temp = malloc(PAGE_SIZE);
    // readRecord(fileHandle, recordDescriptor, rid, temp);
    // printRecord(recordDescriptor, temp);
    // free(temp);
    free(pageData);
    return SUCCESS;
}
RC RecordBasedFileManager::readRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, void *data)
{
    // Retrieve the specified page
    void *pageData = malloc(PAGE_SIZE);
    if (pageData == NULL)
        return RBFM_MALLOC_FAILED;

    if (fileHandle.readPage(rid.pageNum, pageData))
        return RBFM_READ_FAILED;

    // Checks if the specific slot id exists in the page.
    SlotDirectoryHeader slotHeader = getSlotDirectoryHeader(pageData);
    if (slotHeader.recordEntriesNumber <= rid.slotNum)
        return RBFM_SLOT_DN_EXIST;
    // Gets the slot directory record entry data.
    SlotDirectoryRecordEntry recordEntry = getSlotDirectoryRecordEntry(pageData, rid.slotNum);
    if (recordEntry.length < 0)
    {
        free(pageData);
        return RBFM_SLOT_ALR_DELETED;
    }
    if (recordEntry.offset < 0)
    {
        // This is a forwarding address.
        RID newRID;
        newRID.pageNum = (recordEntry.offset * -1) - 1;
        /* newRID.pageNum = (recordEntry.offset * -1); */
        newRID.slotNum = recordEntry.length;
        free(pageData);
        return readRecord(fileHandle, recordDescriptor, newRID, data);
    }
    // Retrieve the actual entry data
    getRecordAtOffset(pageData, recordEntry.offset, recordDescriptor, data);
    free(pageData);
    return SUCCESS;
}
RC RecordBasedFileManager::printRecord(const vector<Attribute> &recordDescriptor, const void *data)
{
    // Parse the null indicator and save it into an array.
    int nullIndicatorSize = getNullIndicatorSize(recordDescriptor.size());
    char nullIndicator[nullIndicatorSize];
    memset(nullIndicator, 0, nullIndicatorSize);
    memcpy(nullIndicator, data, nullIndicatorSize);
    // We've read in the null indicator, so we can skip past it now
    unsigned offset = nullIndicatorSize;
    cout << "----" << endl;
    for (unsigned i = 0; i < (unsigned)recordDescriptor.size(); i++)
    {
        cout << setw(10) << left << recordDescriptor[i].name << ": ";
        // If the field is null, don't print it
        bool isNull = fieldIsNull(nullIndicator, i);
        if (isNull)
        {
            cout << "NULL" << endl;
            continue;
        }
        switch (recordDescriptor[i].type)
        {
        case TypeInt:
            uint32_t data_integer;
            memcpy(&data_integer, ((char *)data + offset), INT_SIZE);
            offset += INT_SIZE;
            cout << "" << data_integer << endl;
            break;
        case TypeReal:
            float data_real;
            memcpy(&data_real, ((char *)data + offset), REAL_SIZE);
            offset += REAL_SIZE;
            cout << "" << data_real << endl;
            break;
        case TypeVarChar:
            // First VARCHAR_LENGTH_SIZE bytes describe the varchar length
            uint32_t varcharSize;
            memcpy(&varcharSize, ((char *)data + offset), VARCHAR_LENGTH_SIZE);
            offset += VARCHAR_LENGTH_SIZE;
            /* if (varcharSize > 10) */
            /*     cerr << "printRecord: found a varchar of length: " << varcharSize << endl; */
            // Gets the actual string.
            char *data_string = (char *)malloc(varcharSize + 1);
            if (data_string == NULL)
                return RBFM_MALLOC_FAILED;
            memcpy(data_string, ((char *)data + offset), varcharSize);
            // Adds the string terminator.
            data_string[varcharSize] = '\0';
            offset += varcharSize;
            cout << data_string << endl;
            free(data_string);
            break;
        }
    }
    cout << "----" << endl;
    return SUCCESS;
}

RC RecordBasedFileManager::deleteRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid)
{
    // Retrieve the specified page
    void *pageData = malloc(PAGE_SIZE);
    if (pageData == NULL)
        return RBFM_MALLOC_FAILED;
    if (fileHandle.readPage(rid.pageNum, pageData))
        return RBFM_READ_FAILED;
    // Checks if the specific slot id exists in the page
    SlotDirectoryHeader slotHeader = getSlotDirectoryHeader(pageData);
    if (slotHeader.recordEntriesNumber <= rid.slotNum)
    {
        free(pageData);
        return RBFM_SLOT_DN_EXIST;
    }
    // Gets the slot directory record entry data
    SlotDirectoryRecordEntry recordEntry = getSlotDirectoryRecordEntry(pageData, rid.slotNum);
    if (recordEntry.length < 0)
    {
        // This means record has been deleted.
        free(pageData);
        return RBFM_SLOT_ALR_DELETED;
    }
    if (recordEntry.offset < 0)
    {
        // This is a forwarding address.
        RID newRID;
        newRID.pageNum = (recordEntry.offset * -1) - 1;
        /* newRID.pageNum = (recordEntry.offset * -1); // we need to be very careful about how we are defining the record offset. */
                                                    // If it is negative, we can either just set the final bit to 1, or we can multiply by -1.
                                                    // these are likely not equivalent b/c Two's complement. So like... pick one
        newRID.slotNum = recordEntry.length;        // we shouldnt need to screw with the length, we just need to make sure that the MSB is only used
                                                    // to indicate whether something is deleted, and nothing more at all. If its forwarding address,
                                                    // the value can be positive (MSB is 0)
        deleteRecord(fileHandle, recordDescriptor, newRID);
        recordEntry.length = -1;
        recordEntry.offset = 0;
        setSlotDirectoryRecordEntry(pageData, rid.slotNum, recordEntry);
        if (fileHandle.writePage(rid.pageNum, pageData))
            return RBFM_WRITE_FAILED;
        free(pageData);
        return SUCCESS;
    }
    // The entry is neither a forwarding address nor already deleted, meaning we have to delete it.
    // Delete the record at the entry.
    // to make this simple, we can set everything at that memory to 0, though it should be completely unnecessary (just doing it for safety)
    memset((char *)pageData + recordEntry.offset, 0, recordEntry.length); // The way the offset is currently handled will almost certainly need to be changed
    // Shift over the record data by the length of the old record.
    // Loop over every record entry in the slot directory.
    // If the record offset was shifted (the starting offset is less than the one we deleted):
    // Then, add the length of the deleted record.
    unsigned shiftBeginning = sizeof(SlotDirectoryHeader) + slotHeader.recordEntriesNumber * sizeof(SlotDirectoryRecordEntry);
    unsigned shiftSize = recordEntry.offset - shiftBeginning;

    memmove((char *)pageData + shiftBeginning + recordEntry.length, (char *)pageData + shiftBeginning, shiftSize);
    // Zero out the the where the data used to be.
    memset((char *)pageData + shiftBeginning, 0, recordEntry.length);
    for (uint32_t i = 0; i < slotHeader.recordEntriesNumber; i += 1)
    {
        SlotDirectoryRecordEntry next = getSlotDirectoryRecordEntry(pageData, i); // get the next record in order that are after the deleted record
        if (next.offset < recordEntry.offset)
        {
            next.offset += recordEntry.length;
            setSlotDirectoryRecordEntry(pageData, i, next);
        }
    }
    // modify directory to reflect that the entry has been deleted
    recordEntry.length = -1;
    recordEntry.offset = 0;
    // write it back
    setSlotDirectoryRecordEntry(pageData, rid.slotNum, recordEntry);
    // Write back the page data
    if (fileHandle.writePage(rid.pageNum, pageData))
        return RBFM_WRITE_FAILED;
    free(pageData);
    return SUCCESS;
}
RC RecordBasedFileManager::updateRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, const RID &rid)
{
    // cerr << "updateRecord: starting rid.pageNum: " << rid.pageNum << " rid.slotNum: " << rid.slotNum << endl;
    /* cerr << "updateRecord: We are trying to write in:" << endl; */
    /* printRecord(recordDescriptor, data); */
    // Retrieve the specified page
    void *pageData = malloc(PAGE_SIZE);
    if (pageData == NULL)
        return RBFM_MALLOC_FAILED;
    if (fileHandle.readPage(rid.pageNum, pageData))
    {
        free(pageData);
        return RBFM_READ_FAILED;
    }
    SlotDirectoryHeader slotHeader = getSlotDirectoryHeader(pageData);
    // Checks if the specific slot id exists in the page
    if (slotHeader.recordEntriesNumber <= rid.slotNum)
    {
        free(pageData);
        return RBFM_SLOT_DN_EXIST;
    }
    SlotDirectoryRecordEntry oldEntry = getSlotDirectoryRecordEntry(pageData, rid.slotNum);
    // If forwarded: call delete address on forwarded address.
    // Then, call insert record to place it in a new slot / page.
    // Update the forwarding address to reflect the change.
    // Note: If we always delete and insert to a single new forward address:
    // Then, we should never create chains of forwarding addresses.
    // Update the offset and length in the corresponding slot directory.
    // DO NOT change the number of entries in the SlotDirectoryHeader.
    // Check if record has been deleted
    if (oldEntry.length < 0)
    {
        free(pageData);
        return RBFM_SLOT_ALR_DELETED;
    }

    // Check if oldEntry is a forwarding address
    if (oldEntry.offset < 0)
    {
        // Creates RID to be passed in deleteRecord
        RID forwardingRid;
        forwardingRid.pageNum = (oldEntry.offset * -1) - 1;
        forwardingRid.slotNum = oldEntry.length;
        // retreiving
        /* void *temp = malloc(PAGE_SIZE); */
        /* readRecord(fileHandle, recordDescriptor, forwardingRid, temp); */
        /* cerr << "updateRecord: record data before update from forwarded address." << endl; */
        /* printRecord(recordDescriptor, temp); */
        /* free(temp); */
        if (deleteRecord(fileHandle, recordDescriptor, forwardingRid))
        {
            free(pageData);
            return RBFM_DELETE_FAILED;
        }
        if (fileHandle.readPage(rid.pageNum, pageData))
        {
            free(pageData);
            return RBFM_READ_FAILED;
        }
        setSlotDirectoryRecordEntry(pageData, rid.slotNum, oldEntry);
        // cerr << "updateRecord: after delete: rid.pageNum: " << rid.pageNum << " rid.slotNum: " << rid.slotNum << endl;
        if (fileHandle.writePage(rid.pageNum, pageData))
        {
            free(pageData);
            return RBFM_WRITE_FAILED;
        }
        if (insertRecord(fileHandle, recordDescriptor, data, forwardingRid)) // After inserting, forwardingRid is updated with new rid
        {
            free(pageData);
            return RBFM_INSERT_FAILED;
        }
        if (fileHandle.readPage(rid.pageNum, pageData))
        {
            free(pageData);
            return RBFM_READ_FAILED;
        }
        // Preps RecordEntry to be inserted in directory
        SlotDirectoryRecordEntry newEntry;
        // cerr << "bitmask: " << bitmask << endl;
        // cerr << "updateRecord: after insert: forwardingRid.pageNum: " << forwardingRid.pageNum << endl;
        newEntry.offset = (forwardingRid.pageNum + 1) * -1;
        newEntry.length = forwardingRid.slotNum;      // Sets slot num of forwarded address
        // cerr << "updateRecord: after insert: newEntry.offset: " << newEntry.offset << endl;
        /* newEntry.offset = forwardingRid.pageNum * -1; // Multiply by -1 to set forwarding flag */
        /* newEntry.length = forwardingRid.slotNum;      // Sets slot num of forwarded address */
        // Update Slot Directory
        setSlotDirectoryRecordEntry(pageData, rid.slotNum, newEntry); // Accesses old rid for slot number
        // cerr << "updateRecord: after insert: rid.pageNum: " << rid.pageNum << " rid.slotNum: " << rid.slotNum << endl;
        // cerr << "updateRecord: after insert: forwardingRid.pageNum: " << forwardingRid.pageNum << " forwardingRid.slotNum: " << forwardingRid.slotNum << endl;
        // Write back the page data
        if (fileHandle.writePage(rid.pageNum, pageData))
        {
            free(pageData);
            return RBFM_WRITE_FAILED;
        }
        free(pageData);
        return SUCCESS;
    }
    /* cerr << "updateRecord: record data before the update:" << endl; */
    /* void *temp = malloc(PAGE_SIZE); */
    /* readRecord(fileHandle, recordDescriptor, rid, temp); */
    /* printRecord(recordDescriptor, temp); */
    /* free(temp); */
    // Case when record is present on the page
    if (deleteRecord(fileHandle, recordDescriptor, rid))
    {
        free(pageData);
        return RBFM_DELETE_FAILED;
    }
    
    if (fileHandle.readPage(rid.pageNum, pageData))
    {
        free(pageData);
        return RBFM_READ_FAILED;
    }
  
    /* cerr << "updateRecord: after delete: rid.pageNum: " << rid.pageNum << " rid.slotNum: " << rid.slotNum << endl; */
    setSlotDirectoryRecordEntry(pageData, rid.slotNum, oldEntry);
    if (fileHandle.writePage(rid.pageNum, pageData))
    {
        free(pageData);
        return RBFM_WRITE_FAILED;
    }
    // Creates RID to be passed in insertRecord
    RID forwardingRid;
    if (insertRecord(fileHandle, recordDescriptor, data, forwardingRid)) // After inserting, new Rid is stored in forwardingRid
    {
        free(pageData);
        return RBFM_INSERT_FAILED;
    }
    if (fileHandle.readPage(rid.pageNum, pageData))
    {
        free(pageData);
        return RBFM_READ_FAILED;
    }
    // Preps RecordEntry to be inserted in directory
    SlotDirectoryRecordEntry newEntry;
    /* cerr << "updateRecord: after insert: forwardingRid.pageNum: " << forwardingRid.pageNum << endl; */
    newEntry.offset = (forwardingRid.pageNum + 1) * -1;
    newEntry.length = forwardingRid.slotNum;      // Sets slot num of forwarded address
    /* cerr << "updateRecord: after insert: newEntry.offset: " << newEntry.offset << endl; */
    // Update RecordEntry to have forwarding address
    setSlotDirectoryRecordEntry(pageData, rid.slotNum, newEntry); // Accesses old rid for slot number
    if (fileHandle.writePage(rid.pageNum, pageData))
    {
        free(pageData);
        return RBFM_WRITE_FAILED;
    }

    /* cerr << "updateRecord: record data after the update:" << endl; */
    /* temp = malloc(PAGE_SIZE); */
    /* readRecord(fileHandle, recordDescriptor, forwardingRid, temp); */
    /* printRecord(recordDescriptor, temp); */
    /* free(temp); */
    free(pageData);
    return SUCCESS;
}

RC RecordBasedFileManager::readAttribute(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, const string &attributeName, void *data)
{
    // cerr << "they gave an rid, pagnum: " << rid.pageNum << " slotnum: " << rid.slotNum << endl;
    // void* my_temp = malloc(PAGE_SIZE);
    // readRecord(fileHandle, recordDescriptor, rid, my_temp);
    // cerr << "printing record at start of read attr" << endl;
    // printRecord(recordDescriptor, my_temp);
    // Retrieve the specified page
    void *pageData = malloc(PAGE_SIZE);
    if (pageData == NULL)
        return RBFM_MALLOC_FAILED;
    if (fileHandle.readPage(rid.pageNum, pageData))
        return RBFM_READ_FAILED;
    SlotDirectoryHeader slotHeader = getSlotDirectoryHeader(pageData);
    if (slotHeader.recordEntriesNumber <= rid.slotNum)
        return RBFM_SLOT_DN_EXIST;
    SlotDirectoryRecordEntry recordEntry = getSlotDirectoryRecordEntry(pageData, rid.slotNum);
    if (recordEntry.length < 0)
    {
        free(pageData);
        return RBFM_SLOT_ALR_DELETED;
    }
    if (recordEntry.offset < 0)
    {
        RID forwardingRid;
        /* forwardingRid.pageNum = recordEntry.offset * -1; */
        forwardingRid.pageNum = (recordEntry.offset * -1) - 1;
        forwardingRid.slotNum = recordEntry.length;
        free(pageData);
        return readAttribute(fileHandle, recordDescriptor, forwardingRid, attributeName, data);
    }
    // Points to start of record
    char *start = (char *)pageData + recordEntry.offset;
    // Read in the null indicator
    int nullIndicatorSize = getNullIndicatorSize(recordDescriptor.size());
    char nullIndicator[nullIndicatorSize];
    memset(nullIndicator, 0, nullIndicatorSize);
    // Get number of columns and size of the null indicator for this record
    RecordLength len = 0;
    memcpy(&len, start, sizeof(RecordLength));
    int recordNullIndicatorSize = getNullIndicatorSize(len);
    // Read in the existing null indicator
    memcpy(nullIndicator, start + sizeof(RecordLength), nullIndicatorSize);
    // If this new recordDescriptor has had fields added to it, we set all of the new fields to null
    for (unsigned i = len; i < recordDescriptor.size(); i++)
    {
        int indicatorIndex = (i + 1) / CHAR_BIT;
        int indicatorMask = 1 << (CHAR_BIT - 1 - (i % CHAR_BIT));
        nullIndicator[indicatorIndex] |= indicatorMask;
    }
    // Write out null indicator
    // TODO: We only need to write out one null indicator bit for the one attribute!
    /* memcpy(data, nullIndicator, nullIndicatorSize); */
    // Initialize some offsets
    // rec_offset: points to data in the record. We move this forward as we read data from our record
    unsigned rec_offset = sizeof(RecordLength) + recordNullIndicatorSize + len * sizeof(ColumnOffset);
    // data_offset: points to our current place in the output data. We move this forward as we write to data.
    unsigned data_offset = sizeof(char);
    // directory_base: points to the start of our directory of indices
    char *directory_base = start + sizeof(RecordLength) + recordNullIndicatorSize;
    // void *temp = malloc(PAGE_SIZE);
    // cerr << "rid we give to readRecord: pagnum: " << rid.pageNum << " slotnum: " << rid.slotNum << endl;
    // if (readRecord(fileHandle, recordDescriptor, rid, temp))
    //     cerr << "read failed." << endl;
    // printRecord(recordDescriptor, temp);
    unsigned i = 0;
    for (i = 0; i < recordDescriptor.size(); i++)
    {
        if (recordDescriptor[i].name == attributeName)
        {
            if (fieldIsNull(nullIndicator, i)) {
                char val = 0x80;
                memcpy(data, &val, 1);
                free(pageData);
                return SUCCESS;
            }
            ColumnOffset endPointer;
            memcpy(&endPointer, directory_base + i * sizeof(ColumnOffset), sizeof(ColumnOffset));
            // If we skipped to a column, the previous column offset has the beginning of our record.
            if (i > 0)
                memcpy(&rec_offset, directory_base + (i - 1) * sizeof(ColumnOffset), sizeof(ColumnOffset));
            // rec_offset keeps track of start of column, so end-start = total size
            uint32_t fieldSize = endPointer - rec_offset;
            // Special case for varchar, we must give data the size of varchar first
            /* if (recordDescriptor[i].type == TypeVarChar) */
            /* { */
            /*     memcpy((char *)data + data_offset, start + rec_offset, VARCHAR_LENGTH_SIZE); */
            /*     data_offset += VARCHAR_LENGTH_SIZE; */
            /*     rec_offset += VARCHAR_LENGTH_SIZE; */
            /* } */
            // Next we copy bytes equal to the size of the field and increase our offsets
            memset((char *)data, 0, 1);
            memcpy((char *)data + data_offset, start + rec_offset, fieldSize);
            break;
        }
    }
    free(pageData);
    return SUCCESS;
}
// Scan returns an iterator to allow the caller to go through the results one by one.
RC RecordBasedFileManager::scan(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const string &conditionAttribute, const CompOp compOp,
                                const void *value, const vector<string> &attributeNames, RBFM_ScanIterator &rbfm_ScanIterator)
{
    rbfm_ScanIterator.Open(fileHandle, recordDescriptor, conditionAttribute, compOp, value, attributeNames);
    return SUCCESS;
}
SlotDirectoryHeader RecordBasedFileManager::getSlotDirectoryHeader(void *page)
{
    // Getting the slot directory header.
    SlotDirectoryHeader slotHeader;
    memcpy(&slotHeader, page, sizeof(SlotDirectoryHeader));
    return slotHeader;
}
void RecordBasedFileManager::setSlotDirectoryHeader(void *page, SlotDirectoryHeader slotHeader)
{
    // Setting the slot directory header.
    memcpy(page, &slotHeader, sizeof(SlotDirectoryHeader));
}
SlotDirectoryRecordEntry RecordBasedFileManager::getSlotDirectoryRecordEntry(void *page, unsigned recordEntryNumber)
{
    // Getting the slot directory entry data.
    SlotDirectoryRecordEntry recordEntry;
    memcpy(
        &recordEntry,
        ((char *)page + sizeof(SlotDirectoryHeader) + recordEntryNumber * sizeof(SlotDirectoryRecordEntry)),
        sizeof(SlotDirectoryRecordEntry));
    return recordEntry;
}
void RecordBasedFileManager::setSlotDirectoryRecordEntry(void *page, unsigned recordEntryNumber, SlotDirectoryRecordEntry recordEntry)
{
    // Setting the slot directory entry data.
    memcpy(
        ((char *)page + sizeof(SlotDirectoryHeader) + recordEntryNumber * sizeof(SlotDirectoryRecordEntry)),
        &recordEntry,
        sizeof(SlotDirectoryRecordEntry));
}
// Configures a new record based page, and puts it in "page".
void RecordBasedFileManager::newRecordBasedPage(void *page)
{
    memset(page, 0, PAGE_SIZE);
    // Writes the slot directory header.
    SlotDirectoryHeader slotHeader;
    slotHeader.freeSpaceOffset = PAGE_SIZE;
    slotHeader.recordEntriesNumber = 0;
    memcpy(page, &slotHeader, sizeof(SlotDirectoryHeader));
}
unsigned RecordBasedFileManager::getRecordSize(const vector<Attribute> &recordDescriptor, const void *data)
{
    // Read in the null indicator
    int nullIndicatorSize = getNullIndicatorSize(recordDescriptor.size());
    char nullIndicator[nullIndicatorSize];
    memset(nullIndicator, 0, nullIndicatorSize);
    memcpy(nullIndicator, (char *)data, nullIndicatorSize);
    // Offset into *data. Start just after the null indicator
    unsigned offset = nullIndicatorSize;
    // Running count of size. Initialize it to the size of the header
    unsigned size = sizeof(RecordLength) + (recordDescriptor.size()) * sizeof(ColumnOffset) + nullIndicatorSize;
    for (unsigned i = 0; i < (unsigned)recordDescriptor.size(); i++)
    {
        // Skip null fields
        if (fieldIsNull(nullIndicator, i))
            continue;
        switch (recordDescriptor[i].type)
        {
        case TypeInt:
            size += INT_SIZE;
            offset += INT_SIZE;
            break;
        case TypeReal:
            size += REAL_SIZE;
            offset += REAL_SIZE;
            break;
        case TypeVarChar:
            uint32_t varcharSize;
            // We have to get the size of the VarChar field by reading the integer that precedes the string value itself
            memcpy(&varcharSize, (char *)data + offset, VARCHAR_LENGTH_SIZE);
            size += varcharSize;
            offset += varcharSize + VARCHAR_LENGTH_SIZE;
            break;
        }
    }
    return size;
}
// Calculate actual bytes for null-indicator for the given field counts
int RecordBasedFileManager::getNullIndicatorSize(int fieldCount)
{
    return int(ceil((double)fieldCount / CHAR_BIT));
}
bool RecordBasedFileManager::fieldIsNull(char *nullIndicator, int i)
{
    int indicatorIndex = i / CHAR_BIT;
    int indicatorMask = 1 << (CHAR_BIT - 1 - (i % CHAR_BIT));
    return (nullIndicator[indicatorIndex] & indicatorMask) != 0;
}
// Computes the free space of a page (function of the free space pointer and the slot directory size).
unsigned RecordBasedFileManager::getPageFreeSpaceSize(void *page)
{
    SlotDirectoryHeader slotHeader = getSlotDirectoryHeader(page);
    return slotHeader.freeSpaceOffset - slotHeader.recordEntriesNumber * sizeof(SlotDirectoryRecordEntry) - sizeof(SlotDirectoryHeader);
}
// Support header size and null indicator. If size is less than recordDescriptor size, then trailing records are null
void RecordBasedFileManager::getRecordAtOffset(void *page, unsigned offset, const vector<Attribute> &recordDescriptor, void *data)
{
    // Pointer to start of record
    char *start = (char *)page + offset;
    // Allocate space for null indicator.
    int nullIndicatorSize = getNullIndicatorSize(recordDescriptor.size());
    char nullIndicator[nullIndicatorSize];
    memset(nullIndicator, 0, nullIndicatorSize);
    // Get number of columns and size of the null indicator for this record
    RecordLength len = 0;
    memcpy(&len, start, sizeof(RecordLength));
    int recordNullIndicatorSize = getNullIndicatorSize(len);
    // Read in the existing null indicator
    memcpy(nullIndicator, start + sizeof(RecordLength), recordNullIndicatorSize);
    // If this new recordDescriptor has had fields added to it, we set all of the new fields to null
    for (unsigned i = len; i < recordDescriptor.size(); i++)
    {
        int indicatorIndex = (i + 1) / CHAR_BIT;
        int indicatorMask = 1 << (CHAR_BIT - 1 - (i % CHAR_BIT));
        nullIndicator[indicatorIndex] |= indicatorMask;
    }
    // Write out null indicator
    memcpy(data, nullIndicator, nullIndicatorSize);
    // Initialize some offsets
    // rec_offset: points to data in the record. We move this forward as we read data from our record
    unsigned rec_offset = sizeof(RecordLength) + recordNullIndicatorSize + len * sizeof(ColumnOffset);
    // data_offset: points to our current place in the output data. We move this forward as we write to data.
    unsigned data_offset = nullIndicatorSize;
    // directory_base: points to the start of our directory of indices
    char *directory_base = start + sizeof(RecordLength) + recordNullIndicatorSize;
    for (unsigned i = 0; i < recordDescriptor.size(); i++)
    {
        if (fieldIsNull(nullIndicator, i))
            continue;
        // Grab pointer to end of this column
        ColumnOffset endPointer;
        memcpy(&endPointer, directory_base + i * sizeof(ColumnOffset), sizeof(ColumnOffset));
        // rec_offset keeps track of start of column, so end-start = total size
        uint32_t fieldSize = endPointer - rec_offset;
        // Special case for varchar, we must give data the size of varchar first
        if (recordDescriptor[i].type == TypeVarChar)
        {
            memcpy((char *)data + data_offset, &fieldSize, VARCHAR_LENGTH_SIZE);
            data_offset += VARCHAR_LENGTH_SIZE;
        }
        // Next we copy bytes equal to the size of the field and increase our offsets
        memcpy((char *)data + data_offset, start + rec_offset, fieldSize);
        rec_offset += fieldSize;
        data_offset += fieldSize;
    }
}
void RecordBasedFileManager::setRecordAtOffset(void *page, unsigned offset, const vector<Attribute> &recordDescriptor, const void *data)
{
    // Read in the null indicator
    int nullIndicatorSize = getNullIndicatorSize(recordDescriptor.size());
    char nullIndicator[nullIndicatorSize];
    memset(nullIndicator, 0, nullIndicatorSize);
    memcpy(nullIndicator, (char *)data, nullIndicatorSize);
    // Points to start of record
    char *start = (char *)page + offset;
    // Offset into *data
    unsigned data_offset = nullIndicatorSize;
    // Offset into page header
    unsigned header_offset = 0;
    RecordLength len = recordDescriptor.size();
    memcpy(start + header_offset, &len, sizeof(len));
    header_offset += sizeof(len);
    memcpy(start + header_offset, nullIndicator, nullIndicatorSize);
    header_offset += nullIndicatorSize;
    // Keeps track of the offset of each record
    // Offset is relative to the start of the record and points to the END of a field
    ColumnOffset rec_offset = header_offset + (recordDescriptor.size()) * sizeof(ColumnOffset);
    unsigned i = 0;
    for (i = 0; i < recordDescriptor.size(); i++)
    {
        if (!fieldIsNull(nullIndicator, i))
        {
            // Points to current position in *data
            char *data_start = (char *)data + data_offset;
            // Read in the data for the next column, point rec_offset to end of newly inserted data
            switch (recordDescriptor[i].type)
            {
            case TypeInt:
                memcpy(start + rec_offset, data_start, INT_SIZE);
                rec_offset += INT_SIZE;
                data_offset += INT_SIZE;
                break;
            case TypeReal:
                memcpy(start + rec_offset, data_start, REAL_SIZE);
                rec_offset += REAL_SIZE;
                data_offset += REAL_SIZE;
                break;
            case TypeVarChar:
                unsigned varcharSize;
                // We have to get the size of the VarChar field by reading the integer that precedes the string value itself
                memcpy(&varcharSize, data_start, VARCHAR_LENGTH_SIZE);
                memcpy(start + rec_offset, data_start + VARCHAR_LENGTH_SIZE, varcharSize);
                // We also have to account for the overhead given by that integer.
                rec_offset += varcharSize;
                data_offset += VARCHAR_LENGTH_SIZE + varcharSize;
                break;
            }
        }

        // Copy offset into record header
        // Offset is relative to the start of the record and points to END of field
        memcpy(start + header_offset, &rec_offset, sizeof(ColumnOffset));
        header_offset += sizeof(ColumnOffset);
    }
}
// RBFM_ScanIterator is an iterator to go through records
// The way to use it is like the following:
//  RBFM_ScanIterator rbfmScanIterator;
//  rbfm.scan(..., rbfmScanIterator);
//  while (rbfmScanIterator(rid, data) != RBFM_EOF) {
//    process the data;
//  }
//  rbfmScanIterator.close();
RBFM_ScanIterator::RBFM_ScanIterator()
{
}
RBFM_ScanIterator::~RBFM_ScanIterator()
{
}
void RBFM_ScanIterator::Open(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const string &conditionAttribute, const CompOp compOp,
                             const void *value, const vector<string> &attributeNames)
{
    this->pageNum = 0;
    this->fileHandle = &fileHandle;
    if (this->fileHandle != NULL) {
        this->totalPages = fileHandle.getNumberOfPages();
    } else {
        this->totalPages = 0;
    }
    
    this->recordDescriptor = &recordDescriptor;
    this->conditionAttribute = &conditionAttribute;
    this->compOp = compOp;
    this->value = value;
    this->attributeNames = &attributeNames;
    this->pageData = malloc(PAGE_SIZE);
    if (pageData == NULL)
        cerr << "Open: unable to malloc pageData." << endl;
}
// Never keep the results in the memory. When getNextRecord() is called,
// a satisfying record needs to be fetched from the file.
// "data" follows the same format as RecordBasedFileManager::insertRecord().
RC RBFM_ScanIterator::getNextRecord(RID &rid, void *data)
{
    /* cerr << "getNextRecord: We believe there are: " << totalPages << " totalPages." << endl; */
    // while (this->pageNum < this->totalPages)
    // {
    //     /* cerr << "Trying to read page num: " << pageNum << endl; */
    //     if (this->recordNum == 0)
    //     {
    //         if (this->fileHandle->readPage(this->pageNum, this->pageData))
    //             return RBFM_READ_FAILED;
    //         SlotDirectoryHeader header = rbfm->getSlotDirectoryHeader(this->pageData);
    //         this->totalRecordEntries = header.recordEntriesNumber;
    //         /* this->recordNum = 0; */
    //     }
    //     SlotDirectoryRecordEntry recordEntry;
    //     while (this->recordNum < this->totalRecordEntries)
    //     {
    //         recordEntry = rbfm->getSlotDirectoryRecordEntry(this->pageData, this->recordNum);
    //         if (recordEntry.length < 0)
    //         {
    //             this->recordNum++;
    //             continue;
    //         }
    //         if (recordEntry.offset < 0)
    //         {
    //             this->recordNum++;
    //             continue;
    //         }
    //         if (!acceptRecord(recordEntry.offset))
    //         {
    //             this->recordNum++;
    //             continue;
    //         }
    //         memcpy(data, (char *)pageData + recordEntry.offset, recordEntry.length);
    //         if (formatRecord(data, *recordDescriptor, *attributeNames)) {
    //             cerr << "getNextRecord: Error formatting record. Skipping RID with pageNum: " << pageNum << ", recordNum: " << recordNum << endl;
    //             this->recordNum++;
    //             continue;
    //         }
    //         // We wrote in the data!
    //         rid.pageNum = pageNum;
    //         rid.slotNum = recordNum;
    //         cerr << "We returned an RID with pageNum: " << pageNum << " and recordNum: " << recordNum << endl;
    //         // Prep starting record num for next call.
    //         this->recordNum++;
    //         return SUCCESS;
    //     }
    //     // Increment the page number and reset the record num.
    //     this->pageNum++;
    //     this->recordNum = 0;
    // }
    // return RBFM_EOF;
    // IMPLEMENTATION FOR TESTING PURPSOSES. 
    /* cerr << "getNextRecord: We believe there are: " << totalPages << " totalPages." << endl; */
    while (this->pageNum < this->totalPages)
    {
        /* cerr << "Trying to read page num: " << pageNum << endl; */
        // if (this->recordNum == 0)
        // {
        if (this->fileHandle->readPage(this->pageNum, this->pageData) != SUCCESS)
            return RBFM_READ_FAILED;
        SlotDirectoryHeader header = rbfm->getSlotDirectoryHeader(this->pageData);
        this->totalRecordEntries = header.recordEntriesNumber;
        /* this->recordNum = 0; */
        // }
        SlotDirectoryRecordEntry recordEntry;
        while (this->recordNum < this->totalRecordEntries)
        {
            recordEntry = rbfm->getSlotDirectoryRecordEntry(this->pageData, this->recordNum);
            if (recordEntry.length < 0)
            {
                this->recordNum++;
                continue;
            }
            if (recordEntry.offset < 0)
            {
                this->recordNum++;
                continue;
            }
            // if (!acceptRecord(recordEntry.offset))
            // {
            //     this->recordNum++;
            //     continue;
            // }
            char* record_itself = (char*)malloc(PAGE_SIZE);
            RID cur;
            cur.pageNum = this->pageNum;
            cur.slotNum = this->recordNum;
            rbfm->readRecord(*(this->fileHandle), *(this->recordDescriptor), cur, record_itself);
            
            unsigned length_of_record_to_return = 0;
            char* record_data = (char*)malloc(PAGE_SIZE);
            memset(record_data, 0, PAGE_SIZE);
            RC rc = my_format_record(*(this->recordDescriptor), record_itself, *attributeNames, record_data, *conditionAttribute, this->compOp, &length_of_record_to_return);
            if (rc == DO_NOT_INCLUDE) {
                free(record_data);
                free(record_itself);
                this->recordNum += 1;
                continue;
            }
            memcpy(data, record_data, length_of_record_to_return);
            // if (formatRecord(data, *recordDescriptor, *attributeNames)) {
            //     cerr << "getNextRecord: Error formatting record. Skipping RID with pageNum: " << pageNum << ", recordNum: " << recordNum << endl;
            //     this->recordNum++;
            //     continue;
            // }
            // We wrote in the data!
            rid.pageNum = pageNum;
            rid.slotNum = recordNum;
            // cerr << "We returned an RID with pageNum: " << pageNum << " and recordNum: " << recordNum << endl;
            // Prep starting record num for next call.
            this->recordNum += 1;
            free(record_data);
            free(record_itself);
            return SUCCESS;
        }
        // Increment the page number and reset the record num.
        this->pageNum++;
        this->recordNum = 0;
    }
    return RBFM_EOF;
}
RC RBFM_ScanIterator::my_format_record(const vector<Attribute> &recordDescriptor, const void *data, const vector<string> &attributeNames, const void *return_data, const string &conditionAttribute, const CompOp compOp, uint32_t* length_of_record_to_return)
{
    /* rbfm->printRecord(recordDescriptor, data); */
    // Parse the null indicator and save it into an array.
    int nullIndicatorSize = rbfm->getNullIndicatorSize(recordDescriptor.size());
    char nullIndicator[nullIndicatorSize];
    memset(nullIndicator, 0, nullIndicatorSize);
    memcpy(nullIndicator, data, nullIndicatorSize);
    //our null indicators to add into return data
    int returnNullIndicatorSize = rbfm->getNullIndicatorSize(attributeNames.size());
    char returnNullIndicator[returnNullIndicatorSize];
    memset(returnNullIndicator, 0, returnNullIndicatorSize);
    int null_indictor_index = 0;
    // We've read in the null indicator, so we can skip past it now
    unsigned offset = nullIndicatorSize;
    unsigned ret_offset = returnNullIndicatorSize;
    // cout << "----" << endl;
    bool flagFound = false;
    bool include = false;
    vector<Attribute> myAttrs;
    for (unsigned i = 0; i < (unsigned)recordDescriptor.size(); i++)
    {
        // cout << setw(10) << left << recordDescriptor[i].name << ": ";
        // If the field is null, don't print it
        bool in_set = false;
        bool con_attr = false;
        for (int j = 0; j < attributeNames.size(); j += 1){
            if(recordDescriptor[i].name == attributeNames[j])
                in_set = true;
            if(recordDescriptor[i].name == conditionAttribute) {
                con_attr = true;
                flagFound = true;
                // cerr << "we found the condition attribute. It is " << recordDescriptor[i].name << endl;
            }
            else {
                con_attr = false;
            }
        }
        if(in_set) {
            myAttrs.push_back(recordDescriptor[i]);
            bool isNull = rbfm->fieldIsNull(nullIndicator, i);
            // if the current attribute is NULL, just push the null bit and then continue to next attribute
            if (isNull) {
                uint8_t their_bitmask = 1 << (7 - (null_indictor_index % 8));
                returnNullIndicator[null_indictor_index / 8] = returnNullIndicator[null_indictor_index / 8] | their_bitmask;
                continue;
            }
          
            switch (recordDescriptor[i].type)
            {
            case TypeInt:
                if (con_attr) {
                    // cerr << "we made it into the con attr part of case TypeInt" << endl;
                    int data_integer;
                    memcpy(&data_integer, ((char *)data + offset), INT_SIZE);
                    include = intCompare(&data_integer);
                }
                // uint32_t data_integer;
                memcpy(((char *)return_data + ret_offset), ((char *)data + offset), INT_SIZE);
                offset += INT_SIZE;
                ret_offset += INT_SIZE;
                // cout << "" << data_integer << endl;
                break;
            case TypeReal:
                if (con_attr) {
                    float data_real;
                    memcpy(&data_real, ((char *)data + offset), REAL_SIZE);
                    include = floatCompare(&data_real);
                }
                // float data_real;
                memcpy(((char *)return_data + ret_offset), ((char *)data + offset), REAL_SIZE);
                offset += REAL_SIZE;
                ret_offset += REAL_SIZE;
                // cout << "" << data_real << endl;
                break;
            case TypeVarChar:
                if (con_attr) {
                    unsigned varcharSize;
                    memcpy(&varcharSize, ((char *)data + offset), VARCHAR_LENGTH_SIZE);
                    char *data_string = (char *)malloc(varcharSize + 1);
                    if (data_string == NULL)
                        return RBFM_MALLOC_FAILED;
                    memcpy(data_string, ((char *)data + offset + VARCHAR_LENGTH_SIZE), varcharSize);
                    data_string[varcharSize] = '\0';
                    include = stringCompare(data_string, varcharSize);
                }
  
                // First VARCHAR_LENGTH_SIZE bytes describe the varchar length
                unsigned varcharSize;
                memcpy(&varcharSize, ((char *)data + offset), VARCHAR_LENGTH_SIZE);
                memcpy(((char *)return_data + ret_offset), ((char *)data + offset), VARCHAR_LENGTH_SIZE);
                offset += VARCHAR_LENGTH_SIZE;
                ret_offset += VARCHAR_LENGTH_SIZE;
                
                memcpy(((char *)return_data + ret_offset), ((char *)data + offset), varcharSize);
                offset += varcharSize;
                ret_offset += varcharSize;
                break;
            }
            null_indictor_index += 1;
        }
        else {
            switch (recordDescriptor[i].type) {
                case TypeInt:
                    if (con_attr) {
                        // cerr << "we made it into the con attr part of case TypeInt" << endl;
                        int data_integer;
                        memcpy(&data_integer, ((char *)data + offset), INT_SIZE);
                        include = intCompare(&data_integer);
                    }
                    offset += INT_SIZE;
                    break;
                case TypeReal:
                    if (con_attr) {
                        float data_real;
                        memcpy(&data_real, ((char *)data + offset), REAL_SIZE);
                        include = floatCompare(&data_real);
                    }
                    offset += REAL_SIZE;
                    break;
                case TypeVarChar:
                    if (con_attr) {
                        unsigned varcharSize;
                        memcpy(&varcharSize, ((char *)data + offset), VARCHAR_LENGTH_SIZE);
                        char *data_string = (char *)malloc(varcharSize + 1);
                        if (data_string == NULL)
                            return RBFM_MALLOC_FAILED;
                        memcpy(data_string, ((char *)data + offset + VARCHAR_LENGTH_SIZE), varcharSize);
                        data_string[varcharSize] = '\0';
                        include = stringCompare(data_string, varcharSize);
                    }
                    unsigned varcharSize;
                    memcpy(&varcharSize, ((char *)data + offset), VARCHAR_LENGTH_SIZE);
                    offset += (varcharSize + VARCHAR_LENGTH_SIZE);
                    break;
            }
            continue;
        }
    }
    /* rbfm->printRecord(myAttrs, return_data); */
    // cerr << "include flag: " << include << endl;
    *length_of_record_to_return = ret_offset;
    // cout << "----" << endl;
    if (!flagFound) {
        // condition attribute does not exist in this record descriptor
        return -50;
    }
    if (!include && !(compOp == NO_OP || value == NULL)) {
        return DO_NOT_INCLUDE;
    }
    return SUCCESS;
}
RC RBFM_ScanIterator::close()
{
    /* cerr << this->pageData <<endl; */
    if (this->pageData != NULL)
        free(this->pageData);
    return SUCCESS;
}
bool RBFM_ScanIterator::acceptRecord(unsigned offset)
{
    // NoOp.
    if ((this->compOp == 6) || (this->value == NULL))
        return true;
    // Pointer to start of record
    char *start = (char *)this->pageData + offset;
    // Allocate space for null indicator.
    int nullIndicatorSize = rbfm->getNullIndicatorSize(this->recordDescriptor->size());
    char nullIndicator[nullIndicatorSize];
    memset(nullIndicator, 0, nullIndicatorSize);
    // Get number of columns and size of the null indicator for this record
    RecordLength len = 0;
    memcpy(&len, start, sizeof(RecordLength));
    int recordNullIndicatorSize = rbfm->getNullIndicatorSize(len);
    // Read in the existing null indicator
    memcpy(nullIndicator, start + sizeof(RecordLength), recordNullIndicatorSize);
    // Initialize some offsets
    // rec_offset: points to data in the record. We move this forward as we read data from our record
    unsigned rec_offset = sizeof(RecordLength) + recordNullIndicatorSize + len * sizeof(ColumnOffset);
    // data_offset: points to our current place in the output data. We move this forward as we write to data.
    unsigned data_offset = nullIndicatorSize;
    // directory_base: points to the start of our directory of indices
    char *directory_base = start + sizeof(RecordLength) + recordNullIndicatorSize;
    unsigned i;
    for (i = 0; i < recordDescriptor->size(); i++)
    {
        if ((*recordDescriptor)[i].name == *conditionAttribute)
        {
            if (rbfm->fieldIsNull(nullIndicator, i))
                return false;
            ColumnOffset endPointer;
            memcpy(&endPointer, directory_base + i * sizeof(ColumnOffset), sizeof(ColumnOffset));
            // If we skipped to a column, the previous column offset has the beginning of our record.
            if (i > 0)
                memcpy(&rec_offset, directory_base + (i - 1) * sizeof(ColumnOffset), sizeof(ColumnOffset));
            // rec_offset keeps track of start of column, so end-start = total size
            uint32_t fieldSize = endPointer - rec_offset;
            // Special case for varchar, we must give data the size of varchar first
            switch ((*recordDescriptor)[i].type)
            {
            case TypeInt:
                int data_integer;
                memcpy(&data_integer, ((char *)pageData + offset), INT_SIZE);
                offset += INT_SIZE;
                return intCompare(&data_integer);
            case TypeReal:
                float data_real;
                memcpy(&data_real, ((char *)pageData + offset), REAL_SIZE);
                offset += REAL_SIZE;
                return floatCompare(&data_real);
            case TypeVarChar:
                int varcharSize;
                memcpy(&varcharSize, ((char *)pageData + data_offset), VARCHAR_LENGTH_SIZE);
                data_offset += VARCHAR_LENGTH_SIZE;
                // Gets the actual string.
                /* cerr << "acceptRecord: found a varchar of size: " << varcharSize << endl; */
                char *data_string = (char *)malloc(varcharSize + 1);
                if (data_string == NULL) {
                    cerr << "acceptRecord: Unable to malloc data_string." << endl;
                    return false;
                }
                memcpy(data_string, ((char *)pageData + data_offset), varcharSize);
                // Adds the string terminator.
                data_string[varcharSize] = '\0';
                data_offset += varcharSize;
                bool ret_value = stringCompare(data_string, (*recordDescriptor)[i].length);
                free(data_string);
                return ret_value;
            }
        }
    }
    return false;
}
bool RBFM_ScanIterator::intCompare(int *compare)
{
    if (value == NULL)
        return true;

    if  (compOp == NO_OP)
        return true;

    int val;
    memcpy(&val, value, INT_SIZE);
    // cerr << "compare value: " << *compare << endl;
    // cerr << "value to compare against: " << val << endl;
    switch (compOp)
    {
    case EQ_OP:
        return *compare == val;
    case LT_OP:
        return *compare < val;
    case LE_OP:
        return *compare <= val;
    case GT_OP:
        return *compare < val;
    case GE_OP:
        return *compare >= val;
    case NE_OP:
        return *compare != val;
    case NO_OP:
        return true;
    default:
        return false;
    }
}
bool RBFM_ScanIterator::floatCompare(float *compare)
{
    if (value == NULL)
        return true;

    if  (compOp == NO_OP)
        return true;

    float val;
    memcpy(&val, value, sizeof(float));
    switch (compOp)
    {
    case EQ_OP:
        return *compare == val;
    case LT_OP:
        return *compare < val;
    case LE_OP:
        return *compare <= val;
    case GT_OP:
        return *compare < val;
    case GE_OP:
        return *compare >= val;
    case NE_OP:
        return *compare != val;
    case NO_OP:
        return true;
    default:
        return false;
    }
}
bool RBFM_ScanIterator::stringCompare(char *compare, uint32_t length)
{
    if (value == NULL)
        return true;

    if  (compOp == NO_OP)
        return true;

    char str[length + 1];
    char *charValue = (char *)value;
    strcpy(str, charValue);
    int val = strcmp(compare, str);
    switch (compOp)
    {
    case EQ_OP:
        return val == 0;
    case LT_OP:
        return val < 0;
    case LE_OP:
        return val <= 0;
    case GT_OP:
        return val < 0;
    case GE_OP:
        return val >= 0;
    case NE_OP:
        return val != 0;
    case NO_OP:
        return true;
    default:
        return false;
    }
}
RC RBFM_ScanIterator::formatRecord(void *data, const vector<Attribute> &recordDescriptor, const vector<string> &attributeNames)
// Record to be formatted is expected to be in *data
{
    // Size of nullindicators and starting offset of field data
    unsigned outputNullIndicatorSize = rbfm->getNullIndicatorSize(attributeNames.size());
    unsigned inputNullIndicatorSize = rbfm->getNullIndicatorSize(recordDescriptor.size());
    char *tempBuffer = (char *)malloc(PAGE_SIZE); // Temp buffer to store formatted record
    memset(tempBuffer, 0, PAGE_SIZE);
    char *newNullIndicator = (char *)malloc(outputNullIndicatorSize); // NullIndicator for formatted record
    memset(newNullIndicator, 0, outputNullIndicatorSize);             // default 0 for nullIndicator
    char *dataPtr = (char *)data; // Char pointer to data
    unsigned formattedRecordOffset = outputNullIndicatorSize;
    unsigned recordOffset = inputNullIndicatorSize;
    // Process each attribute in attributeNames
    for (unsigned i = 0; i < attributeNames.size(); ++i)
    {
        bool found = false;
        unsigned recordOffset = inputNullIndicatorSize;        // Resets the offset to the beginning of the record
        for (unsigned j = 0; j < recordDescriptor.size(); ++j) // Loop for each field in record
        {
            unsigned attributeSize = getAttributeSize(dataPtr + recordOffset, recordDescriptor[j]); // Returns the size of the attribute type
            if (recordDescriptor[j].name == attributeNames[i]) // Has to check if field name match since we don't know the ordering in attributeNames
            {
                if (rbfm->fieldIsNull(dataPtr, j)) // If null flag is found, set null in new nullIndicator
                {
                    setFieldNull(newNullIndicator, i);
                }
                else
                {
                    memcpy(tempBuffer + formattedRecordOffset, dataPtr + recordOffset, attributeSize); // Copys attribute data into formatted record
                    formattedRecordOffset += attributeSize; 
                }
                found = true;
            }
            recordOffset += attributeSize; // Record offset is increased to stay consistent with loop
            if (found)                     // If attribute is found, we break to start looking for next attribute
                break;
        }
    }
    // Copy the new null indicator and formatted data back to the original data buffer
    memcpy(dataPtr, newNullIndicator, outputNullIndicatorSize);
    memcpy(dataPtr + outputNullIndicatorSize, tempBuffer + outputNullIndicatorSize, formattedRecordOffset - outputNullIndicatorSize);
    free(tempBuffer);
    free(newNullIndicator);
    return SUCCESS;
}
bool RBFM_ScanIterator::setFieldNull(char *nullIndicator, int fieldNum)
{
    int byteIndex = fieldNum / 8;                         // Finds the byte of the n'th field
    int bitPosition = fieldNum % 8;                       // Find the bit position within that byte
    nullIndicator[byteIndex] |= (1 << (7 - bitPosition)); // Set the bit to 1
    return true;
}
unsigned RBFM_ScanIterator::getAttributeSize(const void *attributePtr, const Attribute &attribute)
{
    switch (attribute.type)
    {
    case TypeInt:
        return INT_SIZE;
    case TypeReal:
        return REAL_SIZE;
    case TypeVarChar:
        // For VarChar, read the length from the data.
        uint32_t length;
        memcpy(&length, attributePtr, VARCHAR_LENGTH_SIZE);
        return VARCHAR_LENGTH_SIZE + length; // Length of the string plus size of the length field itself
    default:
        return 0;
    }
}
