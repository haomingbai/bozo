/**
 * @file postgresql_task_lifecycle.cc
 * @brief Queue, lifecycle, and transaction-control implementation.
 */

#include "bozo/postgresql/postgresql_task.h"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>

#include <type_traits>
#include <utility>

namespace bozo::postgresql {

namespace {

template <typename Handle>
std::string CopyErrorMessageIfAny(const Handle& handle) {
  if (ozo::is_null_recursive(handle)) {
    return {};
  }
  return std::string(ozo::error_message(handle));
}

template <typename Handle>
std::string CopyErrorContextIfAny(const Handle& handle) {
  if (ozo::is_null_recursive(handle)) {
    return {};
  }
  return std::string(ozo::get_error_context(handle));
}

}  // namespace

std::error_code PostgreSqlTask::StartTransaction(Callback cb) {
  QueuedOperation operation;
  operation.callback = cb;
  operation.start = OperationStart([this, cb = std::move(cb)]() mutable {
    StartTransactionImpl(std::move(cb));
  });
  return EnqueueOperation(std::move(operation),
                          QueuedOperationKind::kStartTransaction,
                          PostgreSqlTaskPhase::kTransaction);
}

std::error_code PostgreSqlTask::CommitTransaction(Callback cb) {
  QueuedOperation operation;
  operation.callback = cb;
  operation.start = OperationStart([this, cb = std::move(cb)]() mutable {
    CommitOrRollbackImpl(true, std::move(cb));
  });
  return EnqueueOperation(std::move(operation),
                          QueuedOperationKind::kCommitTransaction,
                          PostgreSqlTaskPhase::kConnection);
}

std::error_code PostgreSqlTask::RollbackTransaction(Callback cb) {
  QueuedOperation operation;
  operation.callback = cb;
  operation.start = OperationStart([this, cb = std::move(cb)]() mutable {
    CommitOrRollbackImpl(false, std::move(cb));
  });
  return EnqueueOperation(std::move(operation),
                          QueuedOperationKind::kRollbackTransaction,
                          PostgreSqlTaskPhase::kConnection);
}

std::error_code PostgreSqlTask::Close(Callback cb) {
  auto ec = EnqueueOperation({}, QueuedOperationKind::kClose,
                             PostgreSqlTaskPhase::kClosed);
  if (ec) {
    return ec;
  }

  if (cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    close_callbacks_.push_back(std::move(cb));
  }

  auto self = shared_from_this();
  boost::asio::post(operation_strand_, [self]() { self->BeginClose(); });
  return {};
}

std::error_code PostgreSqlTask::EnqueueOperation(QueuedOperation operation,
                                                 QueuedOperationKind kind,
                                                 PostgreSqlTaskPhase next_phase) {
  bool should_post = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (scheduled_phase_ == PostgreSqlTaskPhase::kClosed || close_requested_) {
      return make_error_code(PostgreSqlTaskErrc::kClosed);
    }
    if (scheduled_phase_ == PostgreSqlTaskPhase::kFailed &&
        kind != QueuedOperationKind::kClose) {
      return make_error_code(PostgreSqlTaskErrc::kFailed);
    }

    auto validation_ec = ValidateTransitionLocked(kind);
    if (validation_ec) {
      return validation_ec;
    }

    if (kind != QueuedOperationKind::kClose) {
      queue_.push_back(std::move(operation));
      scheduled_phase_ = next_phase;
      should_post = !operation_in_flight_;
    } else {
      close_requested_ = true;
      scheduled_phase_ = PostgreSqlTaskPhase::kClosed;
      while (!queue_.empty()) {
        cancelled_pending_operations_.push_back(std::move(queue_.front()));
        queue_.pop_front();
      }
      should_post = true;
    }
  }

  if (should_post && kind != QueuedOperationKind::kClose) {
    auto self = shared_from_this();
    boost::asio::post(operation_strand_, [self]() { self->Drain(); });
  }
  return {};
}

void PostgreSqlTask::Drain() {
  QueuedOperation operation;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (operation_in_flight_ || close_requested_ || queue_.empty()) {
      return;
    }
    operation_in_flight_ = true;
    operation = std::move(queue_.front());
    queue_.pop_front();
  }
  operation.start();
}

void PostgreSqlTask::BeginClose() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!close_requested_) {
      return;
    }
  }
  CancelActiveHandle();

  bool in_flight = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    in_flight = operation_in_flight_;
  }
  if (!in_flight) {
    FinalizeClose();
  }
}

void PostgreSqlTask::FinalizeClose() {
  CloseActiveHandle();

  PostgreSqlTaskState state_snapshot(PostgreSqlTaskPhase::kClosed,
                                     make_error_code(PostgreSqlTaskErrc::kOk),
                                     {}, {}, {}, 0);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ClearErrorsLocked(PostgreSqlTaskPhase::kClosed);
    close_requested_ = false;
    scheduled_phase_ = PostgreSqlTaskPhase::kClosed;
    state_snapshot = MakeStateSnapshotLocked();
  }

  DeliverCancelledPendingOperations(state_snapshot);
  DeliverCloseCallbacks(state_snapshot);
}

void PostgreSqlTask::FinalizeOperation(HandleVariant next_handle,
                                       boost::system::error_code ozo_error,
                                       bool cancelled, Callback callback) {
  if (std::holds_alternative<std::monostate>(next_handle)) {
    FinalizeOperationOnClose(ozo_error, cancelled, std::move(callback));
    return;
  }

  handle_ = std::move(next_handle);

  std::string error_message;
  std::string error_context;
  std::error_code task_error;
  const auto next_phase = DeterminePhaseFromHandle(handle_);

  std::visit(
      [&](const auto& handle) {
        using Handle = std::decay_t<decltype(handle)>;
        if constexpr (!std::is_same_v<Handle, std::monostate>) {
          error_message = CopyErrorMessageIfAny(handle);
          error_context = CopyErrorContextIfAny(handle);
        }
      },
      handle_);

  if (cancelled) {
    task_error = make_error_code(PostgreSqlTaskErrc::kCancelled);
  } else if (next_phase == PostgreSqlTaskPhase::kFailed) {
    task_error = make_error_code(PostgreSqlTaskErrc::kFailed);
  }

  PostgreSqlTaskResult result(
      {}, ozo_error, cancelled,
      PostgreSqlTaskState(PostgreSqlTaskPhase::kCreated, {}, {}, {}, {}, 0));
  bool should_finalize_close = false;
  std::vector<QueuedOperation> terminal_operations;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    operation_in_flight_ = false;
    if (close_requested_) {
      UpdateSnapshotLocked(PostgreSqlTaskPhase::kClosed, task_error, ozo_error,
                           std::move(error_message), std::move(error_context));
      actual_phase_ = PostgreSqlTaskPhase::kClosed;
      scheduled_phase_ = PostgreSqlTaskPhase::kClosed;
      result = MakeResultLocked(task_error, ozo_error, cancelled);
      should_finalize_close = true;
    } else {
      if (ozo_error) {
        UpdateSnapshotLocked(next_phase, task_error, ozo_error,
                             std::move(error_message),
                             std::move(error_context));
      } else if (task_error) {
        UpdateSnapshotLocked(next_phase, task_error, ozo_error, {}, {});
      } else {
        ClearErrorsLocked(next_phase);
      }
      actual_phase_ = next_phase;
      if (next_phase == PostgreSqlTaskPhase::kFailed) {
        scheduled_phase_ = PostgreSqlTaskPhase::kFailed;
        while (!queue_.empty()) {
          terminal_operations.push_back(std::move(queue_.front()));
          queue_.pop_front();
        }
      } else if (queue_.empty()) {
        scheduled_phase_ = next_phase;
      }
      result = MakeResultLocked(task_error, ozo_error, cancelled);
    }
  }

  if (callback) {
    DeliverCallback(std::move(callback), result);
  }

  if (should_finalize_close) {
    FinalizeClose();
    return;
  }

  if (!terminal_operations.empty()) {
    DeliverPendingOperations(std::move(terminal_operations),
                             make_error_code(PostgreSqlTaskErrc::kFailed),
                             ozo_error, false, result.GetState());
  }

  Drain();
}

void PostgreSqlTask::FinalizeOperationOnClose(
    boost::system::error_code ozo_error, bool cancelled, Callback callback) {
  const std::error_code task_error =
      cancelled ? make_error_code(PostgreSqlTaskErrc::kCancelled)
                : make_error_code(PostgreSqlTaskErrc::kFailed);

  PostgreSqlTaskResult result(
      {}, ozo_error, cancelled,
      PostgreSqlTaskState(PostgreSqlTaskPhase::kCreated, {}, {}, {}, {}, 0));
  bool should_finalize_close = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    operation_in_flight_ = false;
    if (close_requested_) {
      UpdateSnapshotLocked(PostgreSqlTaskPhase::kClosed, task_error, ozo_error,
                           {}, {});
      actual_phase_ = PostgreSqlTaskPhase::kClosed;
      scheduled_phase_ = PostgreSqlTaskPhase::kClosed;
      result = MakeResultLocked(task_error, ozo_error, cancelled);
      should_finalize_close = true;
    } else {
      UpdateSnapshotLocked(PostgreSqlTaskPhase::kFailed, task_error, ozo_error,
                           {}, {});
      actual_phase_ = PostgreSqlTaskPhase::kFailed;
      scheduled_phase_ = PostgreSqlTaskPhase::kFailed;
      result = MakeResultLocked(task_error, ozo_error, cancelled);
    }
  }

  if (callback) {
    DeliverCallback(std::move(callback), result);
  }

  if (should_finalize_close) {
    FinalizeClose();
    return;
  }

  Drain();
}

void PostgreSqlTask::DeliverCallback(Callback callback,
                                     const PostgreSqlTaskResult& result) {
  if (!callback) {
    return;
  }
  boost::asio::post(callback_strand_,
                    [callback = std::move(callback), result]() mutable {
                      callback(result);
                    });
}

void PostgreSqlTask::DeliverPendingOperations(
    std::vector<QueuedOperation> operations, std::error_code task_error,
    boost::system::error_code ozo_error, bool cancelled,
    const PostgreSqlTaskState& state_snapshot) {
  for (auto& operation : operations) {
    if (!operation.callback) {
      continue;
    }
    DeliverCallback(std::move(operation.callback),
                    PostgreSqlTaskResult(task_error, ozo_error, cancelled,
                                         state_snapshot));
  }
}

void PostgreSqlTask::DeliverCancelledPendingOperations(
    const PostgreSqlTaskState& state_snapshot) {
  std::vector<QueuedOperation> operations;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    operations.swap(cancelled_pending_operations_);
  }

  DeliverPendingOperations(std::move(operations),
                           make_error_code(PostgreSqlTaskErrc::kCancelled),
                           boost::asio::error::operation_aborted, true,
                           state_snapshot);
}

void PostgreSqlTask::DeliverCloseCallbacks(
    const PostgreSqlTaskState& state_snapshot) {
  std::vector<Callback> callbacks;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks.swap(close_callbacks_);
  }
  for (auto& callback : callbacks) {
    DeliverCallback(std::move(callback),
                    PostgreSqlTaskResult({}, {}, false, state_snapshot));
  }
}

void PostgreSqlTask::StartTransactionImpl(Callback cb) {
  auto handler =
      [self = shared_from_this(), callback = std::move(cb)](
          ozo::error_code ec, auto transaction) mutable {
        const auto cancelled = ec == boost::asio::error::operation_aborted;
        auto handle_variant = MakeHandleVariant(std::move(transaction));
        boost::asio::dispatch(
            self->operation_strand_,
            [self, handle_variant = std::move(handle_variant), ec, cancelled,
             callback = std::move(callback)]() mutable {
              self->FinalizeOperation(std::move(handle_variant), ec, cancelled,
                                      std::move(callback));
            });
      };

  if (GetActualPhase() == PostgreSqlTaskPhase::kCreated) {
    std::visit(
        [this, handler = std::move(handler)](auto& source) mutable {
          using Source = std::decay_t<decltype(source)>;
          if constexpr (std::is_same_v<Source, DirectSource>) {
            ozo::begin(source[GetIoContext()], operation_timeout_,
                       std::move(handler));
          } else {
            ozo::begin((*source)[GetIoContext()], operation_timeout_,
                       std::move(handler));
          }
        },
        source_);
    return;
  }

  std::visit(
      [this, handler = std::move(handler)](auto& handle) mutable {
        using Handle = std::decay_t<decltype(handle)>;
        if constexpr (std::is_same_v<Handle, DirectConnection> ||
                      std::is_same_v<Handle, PooledConnection>) {
          auto handle_copy = handle;
          ozo::begin(std::move(handle_copy), operation_timeout_,
                     std::move(handler));
        } else {
          boost::asio::dispatch(
              operation_strand_, [handler = std::move(handler)]() mutable {
                handler(boost::asio::error::not_connected, std::monostate{});
              });
        }
      },
      handle_);
}

void PostgreSqlTask::CommitOrRollbackImpl(bool commit, Callback cb) {
  auto handler =
      [self = shared_from_this(), callback = std::move(cb)](
          ozo::error_code ec, auto connection) mutable {
        const auto cancelled = ec == boost::asio::error::operation_aborted;
        auto handle_variant = MakeHandleVariant(std::move(connection));
        boost::asio::dispatch(
            self->operation_strand_,
            [self, handle_variant = std::move(handle_variant), ec, cancelled,
             callback = std::move(callback)]() mutable {
              self->FinalizeOperation(std::move(handle_variant), ec, cancelled,
                                      std::move(callback));
            });
      };

  std::visit(
      [this, commit, handler = std::move(handler)](auto& handle) mutable {
        using Handle = std::decay_t<decltype(handle)>;
        if constexpr (std::is_same_v<Handle, DirectTransaction> ||
                      std::is_same_v<Handle, PooledTransaction>) {
          auto handle_copy = handle;
          if (commit) {
            ozo::commit(std::move(handle_copy), operation_timeout_,
                        std::move(handler));
          } else {
            ozo::rollback(std::move(handle_copy), operation_timeout_,
                          std::move(handler));
          }
        } else {
          boost::asio::dispatch(
              operation_strand_, [handler = std::move(handler)]() mutable {
                handler(boost::asio::error::not_connected, std::monostate{});
              });
        }
      },
      handle_);
}

void PostgreSqlTask::CancelActiveHandle() {
  std::visit(
      [](auto& handle) {
        using Handle = std::decay_t<decltype(handle)>;
        if constexpr (std::is_same_v<Handle, DirectConnection> ||
                      std::is_same_v<Handle, PooledConnection>) {
          if (handle) {
            handle->cancel();
          }
        } else if constexpr (std::is_same_v<Handle, DirectTransaction> ||
                             std::is_same_v<Handle, PooledTransaction>) {
          handle.cancel();
        }
      },
      handle_);
}

void PostgreSqlTask::CloseActiveHandle() {
  std::visit(
      [](auto& handle) {
        using Handle = std::decay_t<decltype(handle)>;
        if constexpr (std::is_same_v<Handle, DirectConnection> ||
                      std::is_same_v<Handle, PooledConnection>) {
          if (handle) {
            (void)handle->close();
          }
        } else if constexpr (std::is_same_v<Handle, DirectTransaction> ||
                             std::is_same_v<Handle, PooledTransaction>) {
          (void)handle.close();
        }
      },
      handle_);
  handle_ = std::monostate{};
}

}  // namespace bozo::postgresql
