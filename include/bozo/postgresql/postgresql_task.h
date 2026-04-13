/**
 * @file postgresql_task.h
 * @brief Thread-safe PostgreSQL task facade built on top of OZO.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-09
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BOZO_POSTGRESQL_POSTGRESQL_TASK_H_
#define BOZO_POSTGRESQL_POSTGRESQL_TASK_H_

#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/system/error_code.hpp>

#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "bsrvcore/core/trait.h"
#include "bsrvcore/core/types.h"
#include "bozo/postgresql/postgresql_query.h"
#include "ozo/asio.h"
#include "ozo/connection.h"
#include "ozo/connection_info.h"
#include "ozo/connection_pool.h"
#include "ozo/core/recursive.h"
#include "ozo/error.h"
#include "ozo/execute.h"
#include "ozo/io/binary_query.h"
#include "ozo/request.h"
#include "ozo/result.h"
#include "ozo/shortcuts.h"
#include "ozo/transaction.h"
#include "ozo/transaction_status.h"

namespace bozo::postgresql {

/**
 * @brief Stable task phase visible to bozo callers.
 */
enum class PostgreSqlTaskPhase {
  kCreated,
  kConnection,
  kTransaction,
  kClosed,
  kFailed,
};

/**
 * @brief Local bozo task error codes.
 */
enum class PostgreSqlTaskErrc {
  kOk = 0,
  kInvalidState,
  kClosed,
  kFailed,
  kCancelled,
};

/**
 * @brief Returns the local bozo task error category.
 */
const std::error_category &PostgreSqlTaskCategory() noexcept;

/**
 * @brief Converts `PostgreSqlTaskErrc` into `std::error_code`.
 */
std::error_code make_error_code(PostgreSqlTaskErrc ec) noexcept;

/**
 * @brief Immutable snapshot of task state.
 */
class PostgreSqlTaskState
    : public bsrvcore::CopyableMovable<PostgreSqlTaskState> {
public:
  [[nodiscard]] PostgreSqlTaskPhase GetPhase() const noexcept;
  [[nodiscard]] bool HasConnection() const noexcept;
  [[nodiscard]] bool InTransaction() const noexcept;
  [[nodiscard]] bool IsClosed() const noexcept;
  [[nodiscard]] bool IsFailed() const noexcept;
  [[nodiscard]] const std::error_code &GetLastTaskError() const noexcept;
  [[nodiscard]] const boost::system::error_code &
  GetLastOzoError() const noexcept;
  [[nodiscard]] const std::string &GetLastErrorMessage() const noexcept;
  [[nodiscard]] const std::string &GetLastErrorContext() const noexcept;
  [[nodiscard]] std::size_t GetQueueDepth() const noexcept;

private:
  friend class PostgreSqlTask;
  friend class PostgreSqlTaskResult;

  PostgreSqlTaskState(PostgreSqlTaskPhase phase, std::error_code task_error,
                      boost::system::error_code ozo_error,
                      std::string error_message, std::string error_context,
                      std::size_t queue_depth);

  PostgreSqlTaskPhase phase_{PostgreSqlTaskPhase::kCreated};
  std::error_code task_error_;
  boost::system::error_code ozo_error_;
  std::string error_message_;
  std::string error_context_;
  std::size_t queue_depth_{0};
};

/**
 * @brief Callback payload for one queued task operation.
 */
class PostgreSqlTaskResult
    : public bsrvcore::CopyableMovable<PostgreSqlTaskResult> {
public:
  [[nodiscard]] bool Ok() const noexcept;
  [[nodiscard]] bool IsCancelled() const noexcept;
  [[nodiscard]] const std::error_code &GetTaskError() const noexcept;
  [[nodiscard]] const boost::system::error_code &GetOzoError() const noexcept;
  [[nodiscard]] const PostgreSqlTaskState &GetState() const noexcept;

private:
  friend class PostgreSqlTask;

  PostgreSqlTaskResult(std::error_code task_error,
                       boost::system::error_code ozo_error, bool cancelled,
                       PostgreSqlTaskState state);

  std::error_code task_error_;
  boost::system::error_code ozo_error_;
  bool cancelled_{false};
  PostgreSqlTaskState state_{PostgreSqlTaskPhase::kCreated, {}, {}, {}, {}, 0};
};

/**
 * @brief Runtime options shared by task instances created from a direct
 * factory.
 */
struct PostgreSqlTaskOptions
    : public bsrvcore::CopyableMovable<PostgreSqlTaskOptions> {
  std::chrono::milliseconds operation_timeout{10000};
};

/**
 * @brief Runtime options shared by task instances created from a pool factory.
 */
struct PostgreSqlTaskPoolOptions
    : public bsrvcore::CopyableMovable<PostgreSqlTaskPoolOptions> {
  std::chrono::milliseconds operation_timeout{10000};
  ozo::connection_pool_config pool_config{};
};

template <typename T>
inline constexpr bool IsBinaryQueryObject =
    !std::is_base_of_v<ozo::detail::no_binary_query_conversion,
                       ozo::to_binary_query_impl<std::decay_t<T>>>;

/**
 * @brief Thread-safe serialized PostgreSQL task.
 */
class PostgreSqlTask : public std::enable_shared_from_this<PostgreSqlTask>,
                       public bsrvcore::NonCopyableNonMovable<PostgreSqlTask> {
public:
  using Executor = bsrvcore::IoContextExecutor;
  using Callback = std::function<void(const PostgreSqlTaskResult &)>;

  [[nodiscard]] PostgreSqlTaskState GetState() const;

  [[nodiscard]] std::error_code StartTransaction(Callback cb);
  [[nodiscard]] std::error_code CommitTransaction(Callback cb);
  [[nodiscard]] std::error_code RollbackTransaction(Callback cb);
  [[nodiscard]] std::error_code Close(Callback cb = {});

  template <typename Query, typename CallbackLike>
    requires IsBinaryQueryObject<Query>
  [[nodiscard]] std::error_code Execute(Query &&query, CallbackLike &&cb);

  template <typename Text, typename... Args>
    requires(ozo::QueryText<std::decay_t<Text>> &&
             !IsBinaryQueryObject<Text> &&
             sizeof...(Args) > 0)
  [[nodiscard]] std::error_code Execute(Text &&text, Args &&...args);

  /**
   * @brief Executes a typed query and writes decoded rows into `out`.
   *
   * `bozo` does not parse row data itself. It forwards `out` to
   * `ozo::request()`, which performs row decoding and stores the result.
   * The callback only reports task status and connection/transaction state.
   */
  template <typename Query, typename Out, typename CallbackLike>
    requires IsBinaryQueryObject<Query>
  [[nodiscard]] std::error_code Request(Query &&query, Out out,
                                        CallbackLike &&cb);

  template <typename Text, typename Out, typename... Args>
    requires(ozo::QueryText<std::decay_t<Text>> &&
             !IsBinaryQueryObject<Text> &&
             sizeof...(Args) > 0)
  [[nodiscard]] std::error_code Request(Text &&text, Out out, Args &&...args);

  /**
   * @brief Executes a typed query and delivers the decoded output to the
   * callback.
   *
   * `bozo` allocates the output object, passes it to `ozo::request()`, and
   * invokes the callback only after OZO has finished decoding rows into it.
   * The callback can accept `Output&`, `const Output&`, or `Output` by value.
   * `Output` must be default-constructible.
   */
  template <typename Output, typename Query, typename CallbackLike>
    requires IsBinaryQueryObject<Query>
  [[nodiscard]] std::error_code RequestValue(Query &&query, CallbackLike &&cb);

  template <typename Output, typename Text, typename... Args>
    requires(ozo::QueryText<std::decay_t<Text>> &&
             !IsBinaryQueryObject<Text> &&
             sizeof...(Args) > 0)
  [[nodiscard]] std::error_code RequestValue(Text &&text, Args &&...args);

  /**
   * @brief Executes a query into raw `ozo::result`.
   *
   * The raw result object is filled by OZO. `bozo` only serializes execution
   * and reports task status through the callback.
   */
  template <typename Query, typename CallbackLike>
    requires IsBinaryQueryObject<Query>
  [[nodiscard]] std::error_code RequestRaw(Query &&query, ozo::result &out,
                                           CallbackLike &&cb);

  template <typename Text, typename... Args>
    requires(ozo::QueryText<std::decay_t<Text>> &&
             !IsBinaryQueryObject<Text> &&
             sizeof...(Args) > 0)
  [[nodiscard]] std::error_code RequestRaw(Text &&text, ozo::result &out,
                                           Args &&...args);

  /**
   * @brief Executes a query into an owned raw `ozo::result` and passes it to
   * the callback.
   */
  template <typename Query, typename CallbackLike>
    requires IsBinaryQueryObject<Query>
  [[nodiscard]] std::error_code RequestRawValue(Query &&query,
                                                CallbackLike &&cb);

  template <typename Text, typename... Args>
    requires(ozo::QueryText<std::decay_t<Text>> &&
             !IsBinaryQueryObject<Text> &&
             sizeof...(Args) > 0)
  [[nodiscard]] std::error_code RequestRawValue(Text &&text, Args &&...args);

  /**
   * @brief Binds shared output storage to a status callback.
   *
   * This keeps `output` alive until the callback runs and adapts a callback
   * expecting `(const PostgreSqlTaskResult&, Output&)` into the status-only
   * callback used by `Request()`. `output` must not be null.
   */
  template <typename Output, typename CallbackLike>
  [[nodiscard]] static auto BindOutput(std::shared_ptr<Output> output,
                                       CallbackLike &&cb);

  ~PostgreSqlTask() = default;

private:
  friend class PostgreSqlTaskFactory;
  friend class PostgreSqlTaskPoolFactory;

  using DirectSource = ozo::connection_info<>;
  using PoolSource = ozo::connection_pool<DirectSource>;
  using DirectConnection = DirectSource::connection_type;
  using PooledConnection = typename PoolSource::connection_type;
  using DefaultTransactionOptions = decltype(ozo::make_options());
  using DirectTransaction =
      ozo::transaction<DirectConnection, DefaultTransactionOptions>;
  using PooledTransaction =
      ozo::transaction<PooledConnection, DefaultTransactionOptions>;
  using SourceVariant = std::variant<DirectSource, std::shared_ptr<PoolSource>>;
  using HandleVariant =
      std::variant<std::monostate, DirectConnection, DirectTransaction,
                   PooledConnection, PooledTransaction>;
  enum class QueuedOperationKind {
    kExecuteLike,
    kStartTransaction,
    kCommitTransaction,
    kRollbackTransaction,
    kClose,
  };

  struct OperationStart {
    struct Interface {
      virtual ~Interface() = default;
      virtual void Run() = 0;
    };

    template <typename Fn>
    struct Model final : Interface {
      explicit Model(Fn fn) : fn_(std::move(fn)) {}

      void Run() override { fn_(); }

      Fn fn_;
    };

    OperationStart() = default;
    OperationStart(OperationStart &&) noexcept = default;
    OperationStart &operator=(OperationStart &&) noexcept = default;
    OperationStart(const OperationStart &) = delete;
    OperationStart &operator=(const OperationStart &) = delete;

    template <typename Fn>
    explicit OperationStart(Fn &&fn)
        : impl_(std::make_unique<Model<std::decay_t<Fn>>>(
              std::forward<Fn>(fn))) {}

    void operator()() {
      if (impl_) {
        impl_->Run();
      }
    }

  private:
    std::unique_ptr<Interface> impl_;
  };

  struct QueuedOperation {
    OperationStart start;
    Callback callback;
  };

  PostgreSqlTask(Executor io_executor, Executor callback_executor,
                 DirectSource source, PostgreSqlTaskOptions options);
  PostgreSqlTask(Executor io_executor, Executor callback_executor,
                 std::shared_ptr<PoolSource> source,
                 PostgreSqlTaskPoolOptions options);

  [[nodiscard]] std::error_code
  EnqueueOperation(QueuedOperation operation, QueuedOperationKind kind,
                   PostgreSqlTaskPhase next_phase);
  void Drain();
  void BeginClose();
  void FinalizeClose();
  void FinalizeOperation(HandleVariant next_handle,
                         boost::system::error_code ozo_error, bool cancelled,
                         Callback callback);
  void FinalizeOperationOnClose(boost::system::error_code ozo_error,
                                bool cancelled, Callback callback);
  void DeliverCallback(Callback callback, const PostgreSqlTaskResult &result);
  void DeliverPendingOperations(std::vector<QueuedOperation> operations,
                                std::error_code task_error,
                                boost::system::error_code ozo_error,
                                bool cancelled,
                                const PostgreSqlTaskState &state_snapshot);
  void
  DeliverCancelledPendingOperations(const PostgreSqlTaskState &state_snapshot);
  void DeliverCloseCallbacks(const PostgreSqlTaskState &state_snapshot);

  void StartTransactionImpl(Callback cb);
  void CommitOrRollbackImpl(bool commit, Callback cb);

  template <typename Query, typename CallbackLike>
    requires IsBinaryQueryObject<Query>
  [[nodiscard]] std::error_code EnqueueExecuteOperation(Query &&query,
                                                        CallbackLike &&cb);

  template <typename Query, typename Out, typename CallbackLike>
    requires IsBinaryQueryObject<Query>
  [[nodiscard]] std::error_code EnqueueRequestOperation(Query &&query, Out out,
                                                        CallbackLike &&cb);

  template <typename Query> void StartExecuteImpl(Query query, Callback cb);

  template <typename Query, typename Out>
  void StartRequestImpl(Query query, Out out, Callback cb);

  template <typename Query>
  void StartExecuteFromSource(Query query, Callback cb);

  template <typename Query>
  void StartExecuteFromHandle(Query query, Callback cb);

  template <typename Query, typename Out>
  void StartRequestFromSource(Query query, Out out, Callback cb);

  template <typename Query, typename Out>
  void StartRequestFromHandle(Query query, Out out, Callback cb);

  template <typename CallbackLike>
  [[nodiscard]] static Callback NormalizeCallback(CallbackLike &&cb);

  template <typename Handle>
  [[nodiscard]] static HandleVariant MakeHandleVariant(Handle handle);

  template <typename Text, typename Tuple, std::size_t... Indexes>
  [[nodiscard]] static auto MakeQueryFromStoredArgumentsImpl(
      Text &&text, Tuple &stored_arguments,
      std::index_sequence<Indexes...> indexes);

  template <typename Text, typename Tuple>
  [[nodiscard]] static auto MakeQueryFromStoredArguments(
      Text &&text, Tuple &stored_arguments);

  [[nodiscard]] PostgreSqlTaskPhase GetActualPhase() const;
  [[nodiscard]] PostgreSqlTaskState MakeStateSnapshotLocked() const;
  [[nodiscard]] PostgreSqlTaskResult
  MakeResultLocked(std::error_code task_error,
                   boost::system::error_code ozo_error, bool cancelled) const;
  [[nodiscard]] std::error_code
  ValidateTransitionLocked(QueuedOperationKind kind) const;
  [[nodiscard]] PostgreSqlTaskPhase
  DeterminePhaseFromHandle(const HandleVariant &handle) const;
  void UpdateSnapshotLocked(PostgreSqlTaskPhase phase,
                            std::error_code task_error,
                            boost::system::error_code ozo_error,
                            std::string error_message,
                            std::string error_context);
  void ClearErrorsLocked(PostgreSqlTaskPhase phase);
  void CancelActiveHandle();
  void CloseActiveHandle();
  [[nodiscard]] ozo::io_context &GetIoContext() const;
  [[nodiscard]] std::size_t CurrentQueueDepthLocked() const;

  Executor io_executor_;
  Executor callback_executor_;
  boost::asio::strand<Executor> operation_strand_;
  boost::asio::strand<Executor> callback_strand_;
  SourceVariant source_;
  HandleVariant handle_;
  std::chrono::milliseconds operation_timeout_{10000};

  mutable std::mutex mutex_;
  std::deque<QueuedOperation> queue_;
  std::vector<QueuedOperation> cancelled_pending_operations_;
  std::vector<Callback> close_callbacks_;
  PostgreSqlTaskPhase actual_phase_{PostgreSqlTaskPhase::kCreated};
  PostgreSqlTaskPhase scheduled_phase_{PostgreSqlTaskPhase::kCreated};
  std::error_code last_task_error_;
  boost::system::error_code last_ozo_error_;
  std::string last_error_message_;
  std::string last_error_context_;
  bool operation_in_flight_{false};
  bool close_requested_{false};
};

/**
 * @brief Factory for direct-connection PostgreSQL tasks.
 */
class PostgreSqlTaskFactory
    : public bsrvcore::CopyableMovable<PostgreSqlTaskFactory> {
public:
  using Executor = PostgreSqlTask::Executor;
  using DirectSource = PostgreSqlTask::DirectSource;

  PostgreSqlTaskFactory(Executor io_executor, std::string connection_string,
                        PostgreSqlTaskOptions options = {});
  PostgreSqlTaskFactory(Executor io_executor, Executor callback_executor,
                        std::string connection_string,
                        PostgreSqlTaskOptions options = {});

  [[nodiscard]] std::shared_ptr<PostgreSqlTask> Create() const;

  [[nodiscard]] static std::shared_ptr<PostgreSqlTask>
  CreateTask(Executor io_executor, std::string connection_string,
             PostgreSqlTaskOptions options = {});
  [[nodiscard]] static std::shared_ptr<PostgreSqlTask>
  CreateTask(Executor io_executor, Executor callback_executor,
             std::string connection_string, PostgreSqlTaskOptions options = {});

private:
  Executor io_executor_;
  Executor callback_executor_;
  DirectSource source_;
  PostgreSqlTaskOptions options_;
};

/**
 * @brief Factory for pooled-connection PostgreSQL tasks.
 */
class PostgreSqlTaskPoolFactory
    : public bsrvcore::CopyableMovable<PostgreSqlTaskPoolFactory> {
public:
  using Executor = PostgreSqlTask::Executor;
  using DirectSource = PostgreSqlTask::DirectSource;
  using PoolSource = PostgreSqlTask::PoolSource;

  PostgreSqlTaskPoolFactory(Executor io_executor, std::string connection_string,
                            PostgreSqlTaskPoolOptions options = {});
  PostgreSqlTaskPoolFactory(Executor io_executor, Executor callback_executor,
                            std::string connection_string,
                            PostgreSqlTaskPoolOptions options = {});

  [[nodiscard]] std::shared_ptr<PostgreSqlTask> Create() const;

  [[nodiscard]] static std::shared_ptr<PostgreSqlTask>
  CreateTask(Executor io_executor, std::string connection_string,
             PostgreSqlTaskPoolOptions options = {});
  [[nodiscard]] static std::shared_ptr<PostgreSqlTask>
  CreateTask(Executor io_executor, Executor callback_executor,
             std::string connection_string,
             PostgreSqlTaskPoolOptions options = {});

private:
  Executor io_executor_;
  Executor callback_executor_;
  std::shared_ptr<PoolSource> source_;
  PostgreSqlTaskPoolOptions options_;
};

} // namespace bozo::postgresql

namespace std {

template <>
struct is_error_code_enum<bozo::postgresql::PostgreSqlTaskErrc> : true_type {};

} // namespace std

#include "bozo/postgresql/postgresql_task-inl.h"

#endif // BOZO_POSTGRESQL_POSTGRESQL_TASK_H_
