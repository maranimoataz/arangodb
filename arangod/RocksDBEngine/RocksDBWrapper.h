////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2019 ArangoDB GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// based upon stackable_db.h and transaction_db.h from Facebook rocksdb
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
//
////////////////////////////////////////////////////////////////////////////////

#pragma once
#include "rocksdb/utilities/checkpoint.h"
#include "rocksdb/utilities/convenience.h"
#include "rocksdb/utilities/transaction_db.h"

#include "Basics/Mutex.h"
#include "Basics/ReadWriteLock.h"
#include "Basics/ReadLocker.h"

namespace arangodb {

class RocksDBWrapperIterator;
class RocksDBWrapperSnapshot;
class RocksDBWrapperCFHandle;

////////////////////////////////////////////////////////////////////////////////
///
/// @brief This is the primary wrapper for the database and its supporting objects.
///  The primary goal is force use of read side of ReadWriteLock on every call.
///
////////////////////////////////////////////////////////////////////////////////

                      // YES! "protected" to prevent accidental API usage leakage
class RocksDBWrapper : protected rocksdb::DB {
 public:
  RocksDBWrapper() = delete;
  RocksDBWrapper(const RocksDBWrapper &) = delete;
//  RocksDBWrapper & operator(const RocksDBWrapper &) = delete;

  RocksDBWrapper(const rocksdb::DBOptions& db_options,
                 const rocksdb::TransactionDBOptions& txn_db_options,
                 const std::string& dbname,
                 const std::vector<rocksdb::ColumnFamilyDescriptor>& column_families,
                 std::vector<rocksdb::ColumnFamilyHandle*>* handles,
                 rocksdb::TransactionDB * trans);

  virtual ~RocksDBWrapper() {}

  static rocksdb::Status Open(const rocksdb::Options& options,
                              const rocksdb::TransactionDBOptions& txn_db_options,
                     const std::string& dbname, RocksDBWrapper** dbptr);

  static rocksdb::Status Open(const rocksdb::DBOptions& db_options,
                              const rocksdb::TransactionDBOptions& txn_db_options,
                              const std::string& dbname,
                              const std::vector<rocksdb::ColumnFamilyDescriptor>& column_families,
                              std::vector<rocksdb::ColumnFamilyHandle*>* handles,
                              RocksDBWrapper** dbptr);

  virtual rocksdb::Status ReOpen();

  virtual rocksdb::Status Close() { return _db->Close(); }

  // do NOT support GetRootDB() or GetBaseDB() ... allows routines to bypass
  //  need rwlock.
private:
  virtual rocksdb::DB* GetRootDB() { return _db->GetRootDB(); }

  virtual rocksdb::DB* GetBaseDB() { return _db->GetBaseDB(); }

  // friend class gets access
  rocksdb::DB * GetDB() {return _db;};

  friend class RocksDBWrapperDBLock;

public:
  virtual rocksdb::Transaction* BeginTransaction(
    const rocksdb::WriteOptions& write_options,
    const rocksdb::TransactionOptions& txn_options = rocksdb::TransactionOptions(),
    rocksdb::Transaction* old_txn = nullptr) {
    READ_LOCKER(lock, _rwlock);
    return _db->BeginTransaction(write_options, txn_options, old_txn);
  }

  virtual rocksdb::Transaction* GetTransactionByName(const rocksdb::TransactionName& name) {
    READ_LOCKER(lock, _rwlock);
    return _db->GetTransactionByName(name);
  }

  virtual void GetAllPreparedTransactions(std::vector<rocksdb::Transaction*>* trans) {
    READ_LOCKER(lock, _rwlock);
    return _db->GetAllPreparedTransactions(trans);
  }

  // Returns set of all locks held.
  //
  // The mapping is column family id -> KeyLockInfo
  virtual std::unordered_multimap<uint32_t, rocksdb::KeyLockInfo> GetLockStatusData() {
    READ_LOCKER(lock, _rwlock);
    return _db->GetLockStatusData();
  }

  virtual std::vector<rocksdb::DeadlockPath> GetDeadlockInfoBuffer() {
    READ_LOCKER(lock, _rwlock);
    return _db->GetDeadlockInfoBuffer();
  }

  virtual void SetDeadlockInfoBufferSize(uint32_t target_size) {
    READ_LOCKER(lock, _rwlock);
    return _db->SetDeadlockInfoBufferSize(target_size);
  }

  // this is static.  lock not used.  here to force use of RocksDBWrapper namespace
  static rocksdb::Status ListColumnFamilies(const rocksdb::DBOptions& db_options,
                                            const std::string& name,
                                            std::vector<std::string>* column_families) {
    return rocksdb::DB::ListColumnFamilies(db_options, name, column_families);
  }

  virtual rocksdb::Status CreateColumnFamily(const rocksdb::ColumnFamilyOptions& options,
                                    const std::string& column_family_name,
                                    rocksdb::ColumnFamilyHandle** handle) override {
    READ_LOCKER(lock, _rwlock);
    return _db->CreateColumnFamily(options, column_family_name, handle);
  }

  virtual rocksdb::Status CreateColumnFamilies(
      const rocksdb::ColumnFamilyOptions& options,
      const std::vector<std::string>& column_family_names,
      std::vector<rocksdb::ColumnFamilyHandle*>* handles) override {
    READ_LOCKER(lock, _rwlock);
    return _db->CreateColumnFamilies(options, column_family_names, handles);
  }

  virtual rocksdb::Status CreateColumnFamilies(
      const std::vector<rocksdb::ColumnFamilyDescriptor>& column_families,
      std::vector<rocksdb::ColumnFamilyHandle*>* handles) override {
    READ_LOCKER(lock, _rwlock);
    return _db->CreateColumnFamilies(column_families, handles);
  }

  virtual rocksdb::Status DropColumnFamily(rocksdb::ColumnFamilyHandle* column_family) override {
    READ_LOCKER(lock, _rwlock);
    return _db->DropColumnFamily(unwrapCF(column_family));
  }

  virtual rocksdb::Status DropColumnFamilies(
      const std::vector<rocksdb::ColumnFamilyHandle*>& column_families) override {
    READ_LOCKER(lock, _rwlock);
    return _db->DropColumnFamilies(column_families);
  }

  virtual rocksdb::Status DestroyColumnFamilyHandle(
    rocksdb::ColumnFamilyHandle* column_family) override {
//    READ_LOCKER(lock, _rwlock);  exempt because used in shutdownRocksDBInstance
    rocksdb::Status status;
    status = _db->DestroyColumnFamilyHandle(unwrapCF(column_family));
    delete column_family;
    return status;
  }

  virtual rocksdb::Status Put(const rocksdb::WriteOptions& options,
                     rocksdb::ColumnFamilyHandle* column_family, const rocksdb::Slice& key,
                     const rocksdb::Slice& val) override {
    READ_LOCKER(lock, _rwlock);
    return _db->Put(options, unwrapCF(column_family), key, val);
  }

  virtual rocksdb::Status Get(const rocksdb::ReadOptions& options,
                     rocksdb::ColumnFamilyHandle* column_family, const rocksdb::Slice& key,
                     rocksdb::PinnableSlice* value) override {
    READ_LOCKER(lock, _rwlock);
    rocksdb::ReadOptions local_options(options);
    local_options.snapshot = rewriteSnapshot(options.snapshot);
    return _db->Get(local_options, unwrapCF(column_family), key, value);
  }

  virtual rocksdb::Status Get(const rocksdb::ReadOptions& options,
                            rocksdb::ColumnFamilyHandle* column_family, const rocksdb::Slice& key,
                            std::string* value) override {
    READ_LOCKER(lock, _rwlock);
    rocksdb::ReadOptions local_options(options);
    local_options.snapshot = rewriteSnapshot(options.snapshot);
    return _db->Get(local_options, unwrapCF(column_family), key, value);
  }

  virtual std::vector<rocksdb::Status> MultiGet(
      const rocksdb::ReadOptions& options,
      const std::vector<rocksdb::ColumnFamilyHandle*>& column_family,
      const std::vector<rocksdb::Slice>& keys,
      std::vector<std::string>* values) override {
    READ_LOCKER(lock, _rwlock);
    rocksdb::ReadOptions local_options(options);
    local_options.snapshot = rewriteSnapshot(options.snapshot);
    return _db->MultiGet(local_options, column_family, keys, values);
  }

  virtual rocksdb::Status IngestExternalFile(
      rocksdb::ColumnFamilyHandle* column_family,
      const std::vector<std::string>& external_files,
      const rocksdb::IngestExternalFileOptions& options) override {
    READ_LOCKER(lock, _rwlock);
    return _db->IngestExternalFile(unwrapCF(column_family), external_files, options);
  }

  virtual rocksdb::Status VerifyChecksum() override {
    READ_LOCKER(lock, _rwlock);
    return _db->VerifyChecksum();
  }

  virtual bool KeyMayExist(const rocksdb::ReadOptions& options,
                           rocksdb::ColumnFamilyHandle* column_family, const rocksdb::Slice& key,
                           std::string* value,
                           bool* value_found = nullptr) override {
    READ_LOCKER(lock, _rwlock);
    rocksdb::ReadOptions local_options(options);
    local_options.snapshot = rewriteSnapshot(options.snapshot);
    return _db->KeyMayExist(local_options, unwrapCF(column_family), key, value, value_found);
  }

  virtual rocksdb::Status Delete(const rocksdb::WriteOptions& wopts,
                        rocksdb::ColumnFamilyHandle* column_family,
                        const rocksdb::Slice& key) override {
    READ_LOCKER(lock, _rwlock);
    return _db->Delete(wopts, unwrapCF(column_family), key);
  }

  virtual rocksdb::Status SingleDelete(const rocksdb::WriteOptions& wopts,
                              rocksdb::ColumnFamilyHandle* column_family,
                              const rocksdb::Slice& key) override {
    READ_LOCKER(lock, _rwlock);
    return _db->SingleDelete(wopts, unwrapCF(column_family), key);
  }

  virtual rocksdb::Status DeleteRange(const rocksdb::WriteOptions& options,
                                      rocksdb::ColumnFamilyHandle* column_family,
                                      const rocksdb::Slice& begin_key,
                                      const rocksdb::Slice& end_key) override {
    READ_LOCKER(lock, _rwlock);
    return _db->DeleteRange(options, unwrapCF(column_family), begin_key, end_key);
  }



  virtual rocksdb::Status Merge(const rocksdb::WriteOptions& options,
                       rocksdb::ColumnFamilyHandle* column_family, const rocksdb::Slice& key,
                       const rocksdb::Slice& value) override {
    READ_LOCKER(lock, _rwlock);
    return _db->Merge(options, unwrapCF(column_family), key, value);
  }


  virtual rocksdb::Status Write(const rocksdb::WriteOptions& opts, rocksdb::WriteBatch* updates) override
    {
    READ_LOCKER(lock, _rwlock);
    return _db->Write(opts, updates);
  }

  virtual rocksdb::Iterator* NewIterator(const rocksdb::ReadOptions& opts,
                                         rocksdb::ColumnFamilyHandle* column_family) override;

  virtual rocksdb::Status NewIterators(
      const rocksdb::ReadOptions& options,
      const std::vector<rocksdb::ColumnFamilyHandle*>& column_families,
      std::vector<rocksdb::Iterator*>* iterators) override {
    TRI_ASSERT(false);
    READ_LOCKER(lock, _rwlock);
    rocksdb::ReadOptions local_options(options);
    local_options.snapshot = rewriteSnapshot(options.snapshot);
    // lacks code to translate to RocksDBWrapperIterators.
    return _db->NewIterators(local_options, column_families, iterators);
  }

  virtual const rocksdb::Snapshot* GetSnapshot() override;

  virtual void ReleaseSnapshot(const rocksdb::Snapshot* snapshot) override;

  virtual bool GetProperty(rocksdb::ColumnFamilyHandle* column_family,
                           const rocksdb::Slice& property, std::string* value) override {
    READ_LOCKER(lock, _rwlock);
    return _db->GetProperty(unwrapCF(column_family), property, value);
  }
  virtual bool GetProperty(const rocksdb::Slice& property, std::string* value) override {
    READ_LOCKER(lock, _rwlock);
    return _db->GetProperty(property, value);
  }

  virtual bool GetMapProperty(
      rocksdb::ColumnFamilyHandle* column_family, const rocksdb::Slice& property,
      std::map<std::string, std::string>* value) override {
    READ_LOCKER(lock, _rwlock);
    return _db->GetMapProperty(unwrapCF(column_family), property, value);
  }

  virtual bool GetIntProperty(rocksdb::ColumnFamilyHandle* column_family,
                              const rocksdb::Slice& property, uint64_t* value) override {
    READ_LOCKER(lock, _rwlock);
    return _db->GetIntProperty(unwrapCF(column_family), property, value);
  }

  virtual bool GetAggregatedIntProperty(const rocksdb::Slice& property,
                                        uint64_t* value) override {
    READ_LOCKER(lock, _rwlock);
    return _db->GetAggregatedIntProperty(property, value);
  }

  virtual void GetApproximateSizes(rocksdb::ColumnFamilyHandle* column_family,
                                   const rocksdb::Range* r, int n, uint64_t* sizes,
                                   uint8_t include_flags
                                   = rocksdb::DB::INCLUDE_FILES) override {
    READ_LOCKER(lock, _rwlock);
    return _db->GetApproximateSizes(unwrapCF(column_family), r, n, sizes,
                                    include_flags);
  }

  virtual void GetApproximateMemTableStats(rocksdb::ColumnFamilyHandle* column_family,
                                           const rocksdb::Range& range,
                                           uint64_t* const count,
                                           uint64_t* const size) override {
    READ_LOCKER(lock, _rwlock);
    return _db->GetApproximateMemTableStats(unwrapCF(column_family), range, count, size);
  }

  virtual rocksdb::Status CompactRange(const rocksdb::CompactRangeOptions& options,
                              rocksdb::ColumnFamilyHandle* column_family,
                              const rocksdb::Slice* begin, const rocksdb::Slice* end) override {
    READ_LOCKER(lock, _rwlock);
    return _db->CompactRange(options, unwrapCF(column_family), begin, end);
  }

  virtual rocksdb::Status CompactFiles(
      const rocksdb::CompactionOptions& compact_options,
      rocksdb::ColumnFamilyHandle* column_family,
      const std::vector<std::string>& input_file_names,
      const int output_level, const int output_path_id = -1,
      std::vector<std::string>* const output_file_names = nullptr) override {
    READ_LOCKER(lock, _rwlock);
    return _db->CompactFiles(
        compact_options, unwrapCF(column_family), input_file_names,
        output_level, output_path_id, output_file_names);
  }

  virtual rocksdb::Status PauseBackgroundWork() override {
    READ_LOCKER(lock, _rwlock);
    return _db->PauseBackgroundWork();
  }
  virtual rocksdb::Status ContinueBackgroundWork() override {
    READ_LOCKER(lock, _rwlock);
    return _db->ContinueBackgroundWork();
  }

  virtual rocksdb::Status EnableAutoCompaction(
      const std::vector<rocksdb::ColumnFamilyHandle*>& column_family_handles) override {
    READ_LOCKER(lock, _rwlock);
    return _db->EnableAutoCompaction(column_family_handles);
  }

  virtual int NumberLevels(rocksdb::ColumnFamilyHandle* column_family) override {
    READ_LOCKER(lock, _rwlock);
    return _db->NumberLevels(unwrapCF(column_family));
  }

  virtual int MaxMemCompactionLevel(rocksdb::ColumnFamilyHandle* column_family) override
  {
    READ_LOCKER(lock, _rwlock);
    return _db->MaxMemCompactionLevel(unwrapCF(column_family));
  }

  virtual int Level0StopWriteTrigger(rocksdb::ColumnFamilyHandle* column_family) override
  {
    READ_LOCKER(lock, _rwlock);
    return _db->Level0StopWriteTrigger(unwrapCF(column_family));
  }

  virtual const std::string& GetName() const override {
    READ_LOCKER(lock, _rwlock);
    return _db->GetName();
  }

  virtual rocksdb::Env* GetEnv() const override {
    READ_LOCKER(lock, _rwlock);
    return _db->GetEnv();
  }

  virtual rocksdb::Options GetOptions(rocksdb::ColumnFamilyHandle* column_family) const override {
    READ_LOCKER(lock, _rwlock);
    return _db->GetOptions(unwrapCF(column_family));
  }

  virtual rocksdb::DBOptions GetDBOptions() const override {
    READ_LOCKER(lock, _rwlock);
    return _db->GetDBOptions();
  }

  virtual rocksdb::Status Flush(const rocksdb::FlushOptions& fopts,
                       rocksdb::ColumnFamilyHandle* column_family) override {
    READ_LOCKER(lock, _rwlock);
    return _db->Flush(fopts, unwrapCF(column_family));
  }

  virtual rocksdb::Status SyncWAL() override {
//    READ_LOCKER(lock, _rwlock);  exempt: used in shutdownRocksDBInstance()
    return _db->SyncWAL();
  }

  virtual rocksdb::Status FlushWAL(bool sync) override {
    READ_LOCKER(lock, _rwlock);
    return _db->FlushWAL(sync);
  }

#ifndef ROCKSDB_LITE

  virtual rocksdb::Status DisableFileDeletions() override {
    READ_LOCKER(lock, _rwlock);
    return _db->DisableFileDeletions();
  }

  virtual rocksdb::Status EnableFileDeletions(bool force) override {
    READ_LOCKER(lock, _rwlock);
    return _db->EnableFileDeletions(force);
  }

  virtual void GetLiveFilesMetaData(
      std::vector<rocksdb::LiveFileMetaData>* metadata) override {
    READ_LOCKER(lock, _rwlock);
    _db->GetLiveFilesMetaData(metadata);
  }

  virtual void GetColumnFamilyMetaData(
      rocksdb::ColumnFamilyHandle *column_family,
      rocksdb::ColumnFamilyMetaData* cf_meta) override {
    READ_LOCKER(lock, _rwlock);
    _db->GetColumnFamilyMetaData(unwrapCF(column_family), cf_meta);
  }

#endif  // ROCKSDB_LITE

  virtual rocksdb::Status GetLiveFiles(std::vector<std::string>& vec, uint64_t* mfs,
                              bool flush_memtable = true) override {
    READ_LOCKER(lock, _rwlock);
    return _db->GetLiveFiles(vec, mfs, flush_memtable);
  }

  virtual rocksdb::SequenceNumber GetLatestSequenceNumber() const override {
    READ_LOCKER(lock, _rwlock);
    return _db->GetLatestSequenceNumber();
  }

  virtual bool SetPreserveDeletesSequenceNumber(rocksdb::SequenceNumber seqnum) override {
    READ_LOCKER(lock, _rwlock);
    return _db->SetPreserveDeletesSequenceNumber(seqnum);
  }

  virtual rocksdb::Status GetSortedWalFiles(rocksdb::VectorLogPtr& files) override {
//    READ_LOCKER(lock, _rwlock);  exempt: used in determinPrunableWalFiles
    return _db->GetSortedWalFiles(files);
  }

  virtual rocksdb::Status DeleteFile(std::string name) override {
//    READ_LOCKER(lock, _rwlock);  exempt: used in pruneWalFiles
    return _db->DeleteFile(name);
  }

  virtual rocksdb::Status GetDbIdentity(std::string& identity) const override {
    READ_LOCKER(lock, _rwlock);
    return _db->GetDbIdentity(identity);
  }

  virtual rocksdb::Status SetOptions(rocksdb::ColumnFamilyHandle* column_family_handle,
                            const std::unordered_map<std::string, std::string>&
                                new_options) override {
    READ_LOCKER(lock, _rwlock);
    return _db->SetOptions(unwrapCF(column_family_handle), new_options);
  }

  virtual rocksdb::Status SetDBOptions(
      const std::unordered_map<std::string, std::string>& new_options)
      override {
    READ_LOCKER(lock, _rwlock);
    return _db->SetDBOptions(new_options);
  }

  virtual rocksdb::Status ResetStats() override {
    READ_LOCKER(lock, _rwlock);
    return _db->ResetStats();
  }

  virtual rocksdb::Status GetPropertiesOfAllTables(
      rocksdb::ColumnFamilyHandle* column_family,
      rocksdb::TablePropertiesCollection* props) override {
    READ_LOCKER(lock, _rwlock);
    return _db->GetPropertiesOfAllTables(unwrapCF(column_family), props);
  }

  virtual rocksdb::Status GetPropertiesOfTablesInRange(
      rocksdb::ColumnFamilyHandle* column_family, const rocksdb::Range* range, std::size_t n,
      rocksdb::TablePropertiesCollection* props) override {
    READ_LOCKER(lock, _rwlock);
    return _db->GetPropertiesOfTablesInRange(unwrapCF(column_family), range, n, props);
  }

  virtual rocksdb::Status GetUpdatesSince(
      rocksdb::SequenceNumber seq_number, std::unique_ptr<rocksdb::TransactionLogIterator>* iter,
      const rocksdb::TransactionLogIterator::ReadOptions& read_options) override {
    READ_LOCKER(lock, _rwlock);
    return _db->GetUpdatesSince(seq_number, iter, read_options);
  }

  virtual rocksdb::Status SuggestCompactRange(rocksdb::ColumnFamilyHandle* column_family,
                                     const rocksdb::Slice* begin,
                                     const rocksdb::Slice* end) override {
    READ_LOCKER(lock, _rwlock);
    return _db->SuggestCompactRange(unwrapCF(column_family), begin, end);
  }

  virtual rocksdb::Status PromoteL0(rocksdb::ColumnFamilyHandle* column_family,
                           int target_level) override {
    READ_LOCKER(lock, _rwlock);
    return _db->PromoteL0(unwrapCF(column_family), target_level);
  }

  virtual rocksdb::ColumnFamilyHandle* DefaultColumnFamily() const override {
    READ_LOCKER(lock, _rwlock);
    return _db->DefaultColumnFamily();
  }

  ///
  /// static routines from include/rocksdb/convenience.h
  ///

  // Delete files which are entirely in the given range
  // Could leave some keys in the range which are in files which are not
  // entirely in the range. Also leaves L0 files regardless of whether they're
  // in the range.
  // Snapshots before the delete might not see the data in the given range.
  rocksdb::Status DeleteFilesInRange(rocksdb::ColumnFamilyHandle* column_family,
                                              const rocksdb::Slice* begin, const rocksdb::Slice* end,
                                              bool include_end = true) {
    READ_LOCKER(lock, _rwlock);
    return rocksdb::DeleteFilesInRange(_db, unwrapCF(column_family), begin, end, include_end);
  }

  // Delete files in multiple ranges at once
  // Delete files in a lot of ranges one at a time can be slow, use this API for
  // better performance in that case.
  rocksdb::Status DeleteFilesInRanges(rocksdb::ColumnFamilyHandle* column_family,
                                               const rocksdb::RangePtr* ranges, size_t n,
                                               bool include_end = true) {
    READ_LOCKER(lock, _rwlock);
    return rocksdb::DeleteFilesInRanges(_db, unwrapCF(column_family), ranges, n, include_end);
  }

  ///
  /// static routines from include/rocksdb/utilities/checkpoint.h
  ///

  rocksdb::Status CreateCheckpointObject(rocksdb::Checkpoint** checkpoint_ptr) {
    READ_LOCKER(lock, _rwlock);
    return rocksdb::Checkpoint::Create(_db, checkpoint_ptr);
  }



  ///
  bool pauseRocksDB(std::chrono::milliseconds timeout);
  bool restartRocksDB(bool isRetry = false);

  /// give out readwrite lock so iterators and snapshots can protect their API too
  basics::ReadWriteLock & rwlock() {return _rwlock;}

  /// management routines for iterators and snapshots
  void registerIterator(RocksDBWrapperIterator *);
  void releaseIterator(RocksDBWrapperIterator *);

  void registerSnapshot(RocksDBWrapperSnapshot *);
  void releaseSnapshot(RocksDBWrapperSnapshot *);

  void buildCFWrappers(std::vector<rocksdb::ColumnFamilyHandle *> & newHandles);

 protected:
  rocksdb::ColumnFamilyHandle * unwrapCF(rocksdb::ColumnFamilyHandle * wrapper);
  rocksdb::ColumnFamilyHandle * unwrapCF(const rocksdb::ColumnFamilyHandle * wrapper) const;
  void updateCFWrappers(std::vector<rocksdb::ColumnFamilyHandle *> & newHandles);

  void deactivateAllIterators();
  void deactivateAllSnapshots();
  const rocksdb::Snapshot * rewriteSnapshot(const rocksdb::Snapshot * snap);

  /// copies of the Open parameters
  const rocksdb::DBOptions _db_options;
  const rocksdb::TransactionDBOptions _txn_db_options;
  const std::string _dbname;
  const std::vector<rocksdb::ColumnFamilyDescriptor> _column_families;
  std::vector<rocksdb::ColumnFamilyHandle*>* _handlesPtr; // special case, this is an output pointer
  std::vector<std::shared_ptr<RocksDBWrapperCFHandle>> _cfWrappers;

  mutable basics::ReadWriteLock _rwlock; /// used in several "const" functions, must be mutable

  /// iterator management
  Mutex _iterMutex;
  std::set<RocksDBWrapperIterator *> _iterSet;

  /// snapshot management
  Mutex _snapMutex;
  std::set<RocksDBWrapperSnapshot *> _snapSet;

  /// original stuff
  rocksdb::TransactionDB * _db;
//  std::shared_ptr<DB> shared_db_ptr_;
};


////////////////////////////////////////////////////////////////////////////////
///
/// @brief This version of a rocksdb iterator holds the live iterator internally
///  until a hotbackup restore.  At that point, its Valid() function begins to
///  return false to break loops.  And it releases the real rocksdb iterator
///  immediately.
///
////////////////////////////////////////////////////////////////////////////////

class RocksDBWrapperIterator : public rocksdb::Iterator {
 public:

  RocksDBWrapperIterator(rocksdb::Iterator * rocks_it, RocksDBWrapper & dbWrap)
    : _isValid(true), _it(rocks_it), _dbWrap(dbWrap)
  {
    _dbWrap.registerIterator(this);
  }

/// race condition between iterator delete and dbWrapper making all purge?  manual rwlock usage? atomic flag?
  virtual ~RocksDBWrapperIterator()
  {
    READ_LOCKER(lock, _dbWrap.rwlock());
    if (_isValid) {
      _isValid = false;
      delete _it;
      _it = nullptr;
      _dbWrap.releaseIterator(this);
    } // if
  }

  virtual bool Valid() const override {
    return _isValid ? _it->Valid() : false;
  }

  virtual void SeekToFirst() override {
    READ_LOCKER(lock, _dbWrap.rwlock());
    if (_isValid) _it->SeekToFirst();
  }

  virtual void SeekToLast() override {
    READ_LOCKER(lock, _dbWrap.rwlock());
    if (_isValid) _it->SeekToLast();
  }

  virtual void Seek(const rocksdb::Slice& target) override {
    READ_LOCKER(lock, _dbWrap.rwlock());
    if (_isValid) _it->Seek(target);
  }

  virtual void SeekForPrev(const rocksdb::Slice& target) override {
    READ_LOCKER(lock, _dbWrap.rwlock());
    if (_isValid) _it->SeekForPrev(target);
  }

  virtual void Next() override {
    READ_LOCKER(lock, _dbWrap.rwlock());
    if (_isValid) _it->Next();
  }

  virtual void Prev() override {
    READ_LOCKER(lock, _dbWrap.rwlock());
    if (_isValid) _it->Prev();
  }

  virtual rocksdb::Slice key() const override {
    READ_LOCKER(lock, _dbWrap.rwlock());
    return _isValid ? _it->key() : rocksdb::Slice();
  };

  virtual rocksdb::Slice value() const override {
    READ_LOCKER(lock, _dbWrap.rwlock());
    return _isValid ? _it->value() : rocksdb::Slice();
  };

  virtual rocksdb::Status status() const override {
    READ_LOCKER(lock, _dbWrap.rwlock());
    return _isValid ? _it->status() : rocksdb::Status::Aborted();
  };

  virtual rocksdb::Status Refresh() override {
    READ_LOCKER(lock, _dbWrap.rwlock());
    return _isValid ? _it->Refresh() : rocksdb::Status::Aborted();
  };

  virtual rocksdb::Status GetProperty(std::string prop_name, std::string* prop) override
  {
    READ_LOCKER(lock, _dbWrap.rwlock());
    return _isValid ? _it->GetProperty(prop_name, prop) : rocksdb::Status::Aborted();
  };

  /// this is called with write lock already held
  void arangoRelease() {_isValid = false; delete _it; _it = nullptr;};

protected:
  std::atomic<bool> _isValid;
  rocksdb::Iterator * _it;
  RocksDBWrapper & _dbWrap;

 private:
  RocksDBWrapperIterator();
  // No copying allowed
  RocksDBWrapperIterator(const RocksDBWrapperIterator&);
  void operator=(const RocksDBWrapperIterator&);

};// class RocksDBWrapperIterator


////////////////////////////////////////////////////////////////////////////////
///
/// @brief This version of a rocksdb snapshot holds the live snapshot internally
///  until a hotbackup restore.  At that point, its GetSequenceNumber() returns zero.
///  It releases the rocksdb snapshot immediately.
///
////////////////////////////////////////////////////////////////////////////////

class RocksDBWrapperSnapshot : public rocksdb::Snapshot {
 public:

  RocksDBWrapperSnapshot(const rocksdb::Snapshot * rocksSnap, RocksDBWrapper & dbWrap)
    : _isValid(true), _snap(rocksSnap), _dbWrap(dbWrap)
  {
    _dbWrap.registerSnapshot(this);
  }

  void deleteSnapshot(rocksdb::TransactionDB * internalDB)
  {
    READ_LOCKER(lock, _dbWrap.rwlock());
    if (_isValid) {
      arangoRelease(internalDB);
      _dbWrap.releaseSnapshot(this);
    } // if
    delete this;
  }

  virtual rocksdb::SequenceNumber GetSequenceNumber() const override {
    READ_LOCKER(lock, _dbWrap.rwlock());
    return _isValid ? _snap->GetSequenceNumber() : (rocksdb::SequenceNumber) 0 ;
  }

  /// this is called with write lock already held
  void arangoRelease(rocksdb::TransactionDB * internalDB) {
    if (_isValid) {
      _isValid = false;
      if (nullptr != _snap) {
        internalDB->ReleaseSnapshot(_snap);
        _snap = nullptr;
      } // if
    } // if
  }

  const rocksdb::Snapshot * getOriginalSnapshot() const {return _snap;}

protected:
  virtual ~RocksDBWrapperSnapshot() {};  // because it is protected in base class

  std::atomic<bool> _isValid;
  const rocksdb::Snapshot * _snap;
  RocksDBWrapper & _dbWrap;

 private:
  RocksDBWrapperSnapshot();
  // No copying allowed
  RocksDBWrapperSnapshot(const RocksDBWrapperSnapshot&);
  void operator=(const RocksDBWrapperSnapshot&);
};


////////////////////////////////////////////////////////////////////////////////
///
/// @brief The restore portion of RocksDBHotBackup needs the ability to pause
///  API calls to rocksdb while closing, replacing, and reopening rocksdb.  These
///  classes create that capability by wrapping the rocksdbTransactionDB classes.
///
////////////////////////////////////////////////////////////////////////////////

class RocksDBWrapperCFHandle : public rocksdb::ColumnFamilyHandle {
 public:
  RocksDBWrapperCFHandle(RocksDBWrapper * dbWrap, rocksdb::ColumnFamilyHandle * handle)
    : _db(dbWrap), _cfHandle(handle) {};

  RocksDBWrapperCFHandle() = delete;

  virtual ~RocksDBWrapperCFHandle() {}
  // Returns the name of the column family associated with the current handle.
  virtual const std::string& GetName() const override {
    READ_LOCKER(lock, _db->rwlock());
    return _cfHandle->GetName();
  }
  // Returns the ID of the column family associated with the current handle.
  virtual uint32_t GetID() const override {
    READ_LOCKER(lock, _db->rwlock());
    return _cfHandle->GetID();
  }
  // Fills "*desc" with the up-to-date descriptor of the column family
  // associated with this handle. Since it fills "*desc" with the up-to-date
  // information, this call might internally lock and release DB mutex to
  // access the up-to-date CF options.  In addition, all the pointer-typed
  // options cannot be referenced any longer than the original options exist.
  //
  // Note that this function is not supported in RocksDBLite.
  virtual rocksdb::Status GetDescriptor(rocksdb::ColumnFamilyDescriptor* desc) override {
    READ_LOCKER(lock, _db->rwlock());
    return _cfHandle->GetDescriptor(desc);
  }
  // Returns the comparator of the column family associated with the
  // current handle.
  virtual const rocksdb::Comparator* GetComparator() const override {
    READ_LOCKER(lock, _db->rwlock());
    return _cfHandle->GetComparator();
  }

  virtual void SetColumnFamilyHandle(rocksdb::ColumnFamilyHandle * handle) {
    _cfHandle = handle;
  };

  virtual rocksdb::ColumnFamilyHandle * GetColumnFamilyHandle() {
    return _cfHandle;
  };

  virtual void SetRocksDBWrapper(class RocksDBWrapper * wrapper) {
    _db = wrapper;
  };

protected:
  class RocksDBWrapper * _db;
  rocksdb::ColumnFamilyHandle * _cfHandle;
};// class RocksDBWrapperCFHandle


class RocksDBWrapperDBLock {
public:
  explicit RocksDBWrapperDBLock(RocksDBWrapper * wrap)
    : _db(wrap) {
    wrap->rwlock().readLock();
  }

  explicit RocksDBWrapperDBLock(RocksDBWrapper & wrap)
    : _db(&wrap) {
    wrap.rwlock().readLock();
  }

  ~RocksDBWrapperDBLock() {
    _db->rwlock().unlockRead();
  }

  RocksDBWrapperDBLock() = delete;
  RocksDBWrapperDBLock(RocksDBWrapperDBLock const &) = delete;
  RocksDBWrapperDBLock(RocksDBWrapperDBLock&&) = delete;
  RocksDBWrapperDBLock & operator=(RocksDBWrapperDBLock const &) = delete;
  RocksDBWrapperDBLock & operator=(RocksDBWrapperDBLock&&) = delete;

  operator rocksdb::DB *() {return _db->GetDB();}

protected:
  RocksDBWrapper * _db;

};// class RocksDBWrapperDBLock

} //  namespace rocksdb
