/**
 * @file postgresql_task_state.cc
 * @brief Error-category, state snapshot, and core state helpers.
 */

#include "bozo/postgresql/postgresql_task.h"

#include <utility>

namespace bozo::postgresql {

namespace {

class PostgreSqlTaskCategoryImpl final : public std::error_category {
 public:
  [[nodiscard]] const char* name() const noexcept override {
    return "bozo.postgresql.task";
  }

  [[nodiscard]] std::string message(int condition) const override {
    switch (static_cast<PostgreSqlTaskErrc>(condition)) {
      case PostgreSqlTaskErrc::kOk:
        return "ok";
      case PostgreSqlTaskErrc::kInvalidState:
        return "invalid state transition";
      case PostgreSqlTaskErrc::kClosed:
        return "task already closed";
      case PostgreSqlTaskErrc::kFailed:
        return "task is in failed state";
      case PostgreSqlTaskErrc::kCancelled:
        return "operation cancelled";
    }
    return "unknown bozo postgresql task error";
  }
};

const PostgreSqlTaskCategoryImpl kTaskCategory{};

}  // namespace

const std::error_category& PostgreSqlTaskCategory() noexcept {
  return kTaskCategory;
}

std::error_code make_error_code(PostgreSqlTaskErrc ec) noexcept {
  return {static_cast<int>(ec), PostgreSqlTaskCategory()};
}

PostgreSqlTaskState::PostgreSqlTaskState(PostgreSqlTaskPhase phase,
                                         std::error_code task_error,
                                         boost::system::error_code ozo_error,
                                         std::string error_message,
                                         std::string error_context,
                                         std::size_t queue_depth)
    : phase_(phase),
      task_error_(std::move(task_error)),
      ozo_error_(std::move(ozo_error)),
      error_message_(std::move(error_message)),
      error_context_(std::move(error_context)),
      queue_depth_(queue_depth) {}

PostgreSqlTaskPhase PostgreSqlTaskState::GetPhase() const noexcept {
  return phase_;
}

bool PostgreSqlTaskState::HasConnection() const noexcept {
  return phase_ == PostgreSqlTaskPhase::kConnection ||
         phase_ == PostgreSqlTaskPhase::kTransaction;
}

bool PostgreSqlTaskState::InTransaction() const noexcept {
  return phase_ == PostgreSqlTaskPhase::kTransaction;
}

bool PostgreSqlTaskState::IsClosed() const noexcept {
  return phase_ == PostgreSqlTaskPhase::kClosed;
}

bool PostgreSqlTaskState::IsFailed() const noexcept {
  return phase_ == PostgreSqlTaskPhase::kFailed;
}

const std::error_code& PostgreSqlTaskState::GetLastTaskError() const noexcept {
  return task_error_;
}

const boost::system::error_code&
PostgreSqlTaskState::GetLastOzoError() const noexcept {
  return ozo_error_;
}

const std::string& PostgreSqlTaskState::GetLastErrorMessage() const noexcept {
  return error_message_;
}

const std::string& PostgreSqlTaskState::GetLastErrorContext() const noexcept {
  return error_context_;
}

std::size_t PostgreSqlTaskState::GetQueueDepth() const noexcept {
  return queue_depth_;
}

PostgreSqlTaskResult::PostgreSqlTaskResult(std::error_code task_error,
                                           boost::system::error_code ozo_error,
                                           bool cancelled,
                                           PostgreSqlTaskState state)
    : task_error_(std::move(task_error)),
      ozo_error_(std::move(ozo_error)),
      cancelled_(cancelled),
      state_(std::move(state)) {}

bool PostgreSqlTaskResult::Ok() const noexcept {
  return !task_error_ && !ozo_error_;
}

bool PostgreSqlTaskResult::IsCancelled() const noexcept {
  return cancelled_;
}

const std::error_code& PostgreSqlTaskResult::GetTaskError() const noexcept {
  return task_error_;
}

const boost::system::error_code& PostgreSqlTaskResult::GetOzoError() const
    noexcept {
  return ozo_error_;
}

const PostgreSqlTaskState& PostgreSqlTaskResult::GetState() const noexcept {
  return state_;
}

PostgreSqlTask::PostgreSqlTask(Executor io_executor, Executor callback_executor,
                               DirectSource source,
                               PostgreSqlTaskOptions options)
    : io_executor_(io_executor),
      callback_executor_(callback_executor),
      operation_strand_(io_executor),
      callback_strand_(callback_executor),
      source_(std::move(source)),
      operation_timeout_(options.operation_timeout) {}

PostgreSqlTask::PostgreSqlTask(Executor io_executor, Executor callback_executor,
                               std::shared_ptr<PoolSource> source,
                               PostgreSqlTaskPoolOptions options)
    : io_executor_(io_executor),
      callback_executor_(callback_executor),
      operation_strand_(io_executor),
      callback_strand_(callback_executor),
      source_(std::move(source)),
      operation_timeout_(options.operation_timeout) {}

PostgreSqlTaskState PostgreSqlTask::GetState() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return MakeStateSnapshotLocked();
}

PostgreSqlTaskPhase PostgreSqlTask::GetActualPhase() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return actual_phase_;
}

PostgreSqlTaskState PostgreSqlTask::MakeStateSnapshotLocked() const {
  return PostgreSqlTaskState(actual_phase_, last_task_error_, last_ozo_error_,
                             last_error_message_, last_error_context_,
                             CurrentQueueDepthLocked());
}

PostgreSqlTaskResult PostgreSqlTask::MakeResultLocked(
    std::error_code task_error, boost::system::error_code ozo_error,
    bool cancelled) const {
  return PostgreSqlTaskResult(std::move(task_error), std::move(ozo_error),
                              cancelled, MakeStateSnapshotLocked());
}

std::error_code PostgreSqlTask::ValidateTransitionLocked(
    QueuedOperationKind kind) const {
  switch (kind) {
    case QueuedOperationKind::kExecuteLike:
      if (scheduled_phase_ == PostgreSqlTaskPhase::kCreated ||
          scheduled_phase_ == PostgreSqlTaskPhase::kConnection ||
          scheduled_phase_ == PostgreSqlTaskPhase::kTransaction) {
        return {};
      }
      break;
    case QueuedOperationKind::kStartTransaction:
      if (scheduled_phase_ == PostgreSqlTaskPhase::kCreated ||
          scheduled_phase_ == PostgreSqlTaskPhase::kConnection) {
        return {};
      }
      break;
    case QueuedOperationKind::kCommitTransaction:
    case QueuedOperationKind::kRollbackTransaction:
      if (scheduled_phase_ == PostgreSqlTaskPhase::kTransaction) {
        return {};
      }
      break;
    case QueuedOperationKind::kClose:
      return {};
  }
  return make_error_code(PostgreSqlTaskErrc::kInvalidState);
}

PostgreSqlTaskPhase PostgreSqlTask::DeterminePhaseFromHandle(
    const HandleVariant& handle) const {
  return std::visit(
      [](const auto& value) -> PostgreSqlTaskPhase {
        using Handle = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Handle, std::monostate>) {
          return PostgreSqlTaskPhase::kFailed;
        } else {
          if (ozo::is_null_recursive(value) || ozo::connection_bad(value)) {
            return PostgreSqlTaskPhase::kFailed;
          }
          switch (ozo::get_transaction_status(value)) {
            case ozo::transaction_status::idle:
              return PostgreSqlTaskPhase::kConnection;
            case ozo::transaction_status::transaction:
            case ozo::transaction_status::error:
            case ozo::transaction_status::active:
              return PostgreSqlTaskPhase::kTransaction;
            case ozo::transaction_status::unknown:
              return PostgreSqlTaskPhase::kFailed;
          }
        }
        return PostgreSqlTaskPhase::kFailed;
      },
      handle);
}

void PostgreSqlTask::UpdateSnapshotLocked(PostgreSqlTaskPhase phase,
                                          std::error_code task_error,
                                          boost::system::error_code ozo_error,
                                          std::string error_message,
                                          std::string error_context) {
  actual_phase_ = phase;
  last_task_error_ = std::move(task_error);
  last_ozo_error_ = std::move(ozo_error);
  last_error_message_ = std::move(error_message);
  last_error_context_ = std::move(error_context);
}

void PostgreSqlTask::ClearErrorsLocked(PostgreSqlTaskPhase phase) {
  UpdateSnapshotLocked(phase, {}, {}, {}, {});
}

ozo::io_context& PostgreSqlTask::GetIoContext() const {
  return static_cast<ozo::io_context&>(io_executor_.context());
}

std::size_t PostgreSqlTask::CurrentQueueDepthLocked() const {
  return queue_.size() + (operation_in_flight_ ? 1U : 0U);
}

}  // namespace bozo::postgresql
