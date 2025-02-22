
#ifndef _rm_h_
#define _rm_h_

#include <string>
#include <vector>

#include "../rbf/rbfm.h"

using namespace std;

#define RM_EOF (-1) // end of a scan operator
#define CATALOG_DSN_EXIST 1
#define TABLE_FILE_ALR_EXISTS 2
#define TB_DN_EXIST 3

// RM_ScanIterator is an iteratr to go through tuples
class RM_ScanIterator
{
public:
  RM_ScanIterator(){};
  ~RM_ScanIterator(){};

  // "data" follows the same format as RelationManager::insertTuple()
  RC scan(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const string &conditionAttribute, const CompOp compOp,
                                        const void *value, const vector<string> &attributeNames);
  RC getNextTuple(RID &rid, void *data);
  RC close();

  FileHandle* rm_scan_handle;
  friend class RelationManager;
private:
  RecordBasedFileManager *catalog = RecordBasedFileManager::instance();
  RBFM_ScanIterator *rbfm_ScanIterator;
};

// Relation Manager
class RelationManager
{
public:
  static RelationManager *instance();

  RC createCatalog();

  RC deleteCatalog();

  RC createTable(const string &tableName, const vector<Attribute> &attrs);

  RC deleteTable(const string &tableName);

  RC getAttributes(const string &tableName, vector<Attribute> &attrs);

  RC insertTuple(const string &tableName, const void *data, RID &rid);

  RC deleteTuple(const string &tableName, const RID &rid);

  RC updateTuple(const string &tableName, const void *data, const RID &rid);

  RC readTuple(const string &tableName, const RID &rid, void *data);

  // Print a tuple that is passed to this utility method.
  // The format is the same as printRecord().
  RC printTuple(const vector<Attribute> &attrs, const void *data);

  RC readAttribute(const string &tableName, const RID &rid, const string &attributeName, void *data);

  // Scan returns an iterator to allow the caller to go through the results one by one.
  // Do not store entire results in the scan iterator.
  RC scan(const string &tableName,
          const string &conditionAttribute,
          const CompOp compOp,                  // comparison type such as "<" and "="
          const void *value,                    // used in the comparison
          const vector<string> &attributeNames, // a list of projected attributes
          RM_ScanIterator &rm_ScanIterator);

  void prepareTableRecord(const int fieldCount, unsigned char *nullFieldsIndicator, const int table_id, const int tableNameLength,
                          const string &table_name, const int fileNameLength, const string &file_name, void *buffer, int *recordSize);

  void prepareColumnRecord(const int fieldCount, unsigned char *nullFieldsIndicator, const int table_id, const int columnNameLength,
                           const string &column_name, const int column_type, const int column_length, const int column_position,
                           void *buffer, int *recordSize);

  void createTableRecordDescriptor(vector<Attribute> &recordDescriptor);

  void createColumnRecordDescriptor(vector<Attribute> &recordDescriptor);

  friend class RM_ScanIterator;

protected:
  RelationManager();
  ~RelationManager();

private:
  static RelationManager *_rm;
  RecordBasedFileManager *catalog = RecordBasedFileManager::instance();
  FileHandle tableHandle;
  FileHandle columnHandle;
  uint32_t table_id_count = 0;
  RC findTableFileName(const string &tableName, RecordBasedFileManager *rbfm, FileHandle &tableFileHandle,
                       const vector<Attribute> &tableDescriptor, string &fileName);
};

#endif
