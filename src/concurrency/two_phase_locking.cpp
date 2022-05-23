/**
 * @file two_phase_locking.cpp
 * @author sheep
 * @brief transaction manager for two phase locking
 * @version 0.1
 * @date 2022-05-23
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "concurrency/two_phase_locking.h"
#include "concurrency/transaction_map.h"

namespace TinyDB {


Result<> TwoPLManager::Read(TransactionContext *txn_context, Tuple *tuple, RID rid, TableInfo *table_info) {
    TINYDB_ASSERT(txn_context->IsAborted() == false, "Trying to executing aborted transaction");
    auto context = txn_context->Cast<TwoPLContext>();

    // if we are not read_uncommitted(don't need lock)
    // and we are not holding the lock
    // then we trying to acquire the lock
    if (context->isolation_level_ != IsolationLevel::READ_UNCOMMITTED && 
        !context->IsSharedLocked(rid) &&
        !context->IsExclusiveLocked(rid)) {
        lock_manager_->LockShared(context, rid);
    }

    auto res = Result();

    // there are many reasons that might lead to reading failure
    // TODO: check the detailed reason then decide whether we need to abort txn
    if (!table_info->table_->GetTuple(rid, tuple)) {
        // skip this tuple
        res = Result(ErrorCode::SKIP);
    }

    // after reading, if we are read committed, then we need to release the lock
    // note that we only need to release shared lock
    if (context->isolation_level_ == IsolationLevel::READ_COMMITTED &&
        context->IsSharedLocked(rid)) {
        lock_manager_->Unlock(context, rid);
    }

    return res;
}

void TwoPLManager::Insert(TransactionContext *txn_context, const Tuple &tuple, RID *rid, TableInfo *table_info) {
    TINYDB_ASSERT(txn_context->IsAborted() == false, "Trying to executing aborted transaction");
    auto context = txn_context->Cast<TwoPLContext>();

}

void TwoPLManager::Delete(TransactionContext *txn_context, RID rid, TableInfo *table_info) {
    TINYDB_ASSERT(txn_context->IsAborted() == false, "Trying to executing aborted transaction");
}

void TwoPLManager::Update(TransactionContext *txn_context, const Tuple &tuple, RID rid, TableInfo *table_info) {
    TINYDB_ASSERT(txn_context->IsAborted() == false, "Trying to executing aborted transaction");
}

TransactionContext *TwoPLManager::Begin(IsolationLevel isolation_level) {
    auto txn_id = next_txn_id_.fetch_add(1);
    TransactionContext *context = new TwoPLContext(txn_id, isolation_level);
    txn_map_->AddTransactionContext(context);

    return context;
}

void TwoPLManager::Commit(TransactionContext *txn_context) {
    auto context = txn_context->Cast<TwoPLContext>();
    context->SetCommitted();
    
    // perform commit action
    for (auto &action : context->commit_action_) {
        action();
    }

    // release all locks
    ReleaseAllLocks(txn_context);

    // free the txn context
    txn_map_->RemoveTransactionContext(txn_context->GetTxnId());
    delete txn_context;
}

void TwoPLManager::Abort(TransactionContext *txn_context) {
    auto context = txn_context->Cast<TwoPLContext>();
    context->SetAborted();

    // rollback before releasing the lock
    // should we abort in reverse order?
    for (auto &action : context->abort_action_) {
        action();
    }

    // release all locks
    ReleaseAllLocks(txn_context);

    // free the txn context
    txn_map_->RemoveTransactionContext(txn_context->GetTxnId());
    delete txn_context;
}

void TwoPLManager::ReleaseAllLocks(TransactionContext *txn_context) {
    auto context = txn_context->Cast<TwoPLContext>();
    std::unordered_set<RID> lock_set;
    for (auto rid : *context->GetExclusiveLockSet()) {
        lock_set.emplace(rid);
    }
    for (auto rid : *context->GetSharedLockSet()) {
        lock_set.emplace(rid);
    }
    for (auto rid : lock_set) {
        lock_manager_->Unlock(context, rid);
    }
}

}