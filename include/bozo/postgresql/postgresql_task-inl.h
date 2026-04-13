/**
 * @file postgresql_task-inl.h
 * @brief Template implementation details for PostgreSqlTask.
 */

#pragma once

#ifndef BOZO_POSTGRESQL_POSTGRESQL_TASK_INL_H_
#define BOZO_POSTGRESQL_POSTGRESQL_TASK_INL_H_

#include <boost/asio/dispatch.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>

#include <type_traits>
#include <utility>

namespace bozo::postgresql {

template <typename CallbackLike>
inline PostgreSqlTask::Callback
PostgreSqlTask::NormalizeCallback(CallbackLike &&cb) {
  return Callback(std::forward<CallbackLike>(cb));
}

template <typename Handle>
inline PostgreSqlTask::HandleVariant
PostgreSqlTask::MakeHandleVariant(Handle handle) {
  return HandleVariant(std::move(handle));
}

template <typename Query, typename CallbackLike>
  requires IsBinaryQueryObject<Query>
inline std::error_code PostgreSqlTask::Execute(Query &&query,
                                               CallbackLike &&cb) {
  return EnqueueExecuteOperation(std::forward<Query>(query),
                                 std::forward<CallbackLike>(cb));
}

template <typename Text, typename... Args>
  requires(ozo::QueryText<std::decay_t<Text>> &&
           !IsBinaryQueryObject<Text> &&
           sizeof...(Args) > 0)
inline std::error_code PostgreSqlTask::Execute(Text &&text, Args &&...args) {
  auto stored_args = std::make_tuple(std::forward<Args>(args)...);
  constexpr std::size_t kCallbackIndex = sizeof...(Args) - 1;
  auto query = MakeQueryFromStoredArguments(std::forward<Text>(text),
                                            stored_args);
  return EnqueueExecuteOperation(std::move(query),
                                 std::move(std::get<kCallbackIndex>(
                                     stored_args)));
}

template <typename Query, typename CallbackLike>
  requires IsBinaryQueryObject<Query>
inline std::error_code PostgreSqlTask::EnqueueExecuteOperation(
    Query &&query, CallbackLike &&cb) {
  auto callback = NormalizeCallback(std::forward<CallbackLike>(cb));
  QueuedOperation operation;
  operation.callback = callback;
  operation.start =
      OperationStart([this, query = std::decay_t<Query>(std::forward<Query>(
                                query)),
                      callback = std::move(callback)]() mutable {
        StartExecuteImpl(std::move(query), std::move(callback));
      });

  PostgreSqlTaskPhase next_phase = GetActualPhase();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    next_phase = scheduled_phase_;
    if (next_phase == PostgreSqlTaskPhase::kCreated) {
      next_phase = PostgreSqlTaskPhase::kConnection;
    }
  }
  return EnqueueOperation(std::move(operation),
                          QueuedOperationKind::kExecuteLike, next_phase);
}

template <typename Query, typename Out, typename CallbackLike>
  requires IsBinaryQueryObject<Query>
inline std::error_code PostgreSqlTask::Request(Query &&query, Out out,
                                               CallbackLike &&cb) {
  return EnqueueRequestOperation(std::forward<Query>(query), std::move(out),
                                 std::forward<CallbackLike>(cb));
}

template <typename Text, typename Out, typename... Args>
  requires(ozo::QueryText<std::decay_t<Text>> &&
           !IsBinaryQueryObject<Text> &&
           sizeof...(Args) > 0)
inline std::error_code PostgreSqlTask::Request(Text &&text, Out out,
                                               Args &&...args) {
  auto stored_args = std::make_tuple(std::forward<Args>(args)...);
  constexpr std::size_t kCallbackIndex = sizeof...(Args) - 1;
  auto query = MakeQueryFromStoredArguments(std::forward<Text>(text),
                                            stored_args);
  return EnqueueRequestOperation(
      std::move(query), std::move(out),
      std::move(std::get<kCallbackIndex>(stored_args)));
}

template <typename Query, typename Out, typename CallbackLike>
  requires IsBinaryQueryObject<Query>
inline std::error_code PostgreSqlTask::EnqueueRequestOperation(
    Query &&query, Out out, CallbackLike &&cb) {
  auto callback = NormalizeCallback(std::forward<CallbackLike>(cb));
  QueuedOperation operation;
  operation.callback = callback;
  operation.start =
      OperationStart([this, query = std::decay_t<Query>(std::forward<Query>(
                                   query)),
                      out = std::move(out),
                      callback = std::move(callback)]() mutable {
        StartRequestImpl(std::move(query), std::move(out), std::move(callback));
      });

  PostgreSqlTaskPhase next_phase = GetActualPhase();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    next_phase = scheduled_phase_;
    if (next_phase == PostgreSqlTaskPhase::kCreated) {
      next_phase = PostgreSqlTaskPhase::kConnection;
    }
  }
  return EnqueueOperation(std::move(operation),
                          QueuedOperationKind::kExecuteLike, next_phase);
}

template <typename Output, typename Query, typename CallbackLike>
  requires IsBinaryQueryObject<Query>
inline std::error_code PostgreSqlTask::RequestValue(Query &&query,
                                                    CallbackLike &&cb) {
  using StoredOutput = std::remove_cvref_t<Output>;
  auto output = std::make_shared<StoredOutput>();
  auto callback = BindOutput(output, std::forward<CallbackLike>(cb));
  return Request(std::forward<Query>(query), ozo::into(*output),
                 std::move(callback));
}

template <typename Output, typename Text, typename... Args>
  requires(ozo::QueryText<std::decay_t<Text>> &&
           !IsBinaryQueryObject<Text> &&
           sizeof...(Args) > 0)
inline std::error_code PostgreSqlTask::RequestValue(Text &&text,
                                                    Args &&...args) {
  auto stored_args = std::make_tuple(std::forward<Args>(args)...);
  constexpr std::size_t kCallbackIndex = sizeof...(Args) - 1;
  auto query = MakeQueryFromStoredArguments(std::forward<Text>(text),
                                            stored_args);
  return RequestValue<Output>(std::move(query),
                              std::move(std::get<kCallbackIndex>(
                                  stored_args)));
}

template <typename Query, typename CallbackLike>
  requires IsBinaryQueryObject<Query>
inline std::error_code
PostgreSqlTask::RequestRaw(Query &&query, ozo::result &out, CallbackLike &&cb) {
  return Request(std::forward<Query>(query), ozo::into(out),
                 std::forward<CallbackLike>(cb));
}

template <typename Text, typename... Args>
  requires(ozo::QueryText<std::decay_t<Text>> &&
           !IsBinaryQueryObject<Text> &&
           sizeof...(Args) > 0)
inline std::error_code PostgreSqlTask::RequestRaw(Text &&text, ozo::result &out,
                                                  Args &&...args) {
  auto stored_args = std::make_tuple(std::forward<Args>(args)...);
  constexpr std::size_t kCallbackIndex = sizeof...(Args) - 1;
  auto query = MakeQueryFromStoredArguments(std::forward<Text>(text),
                                            stored_args);
  return RequestRaw(std::move(query), out,
                    std::move(std::get<kCallbackIndex>(stored_args)));
}

template <typename Query, typename CallbackLike>
  requires IsBinaryQueryObject<Query>
inline std::error_code PostgreSqlTask::RequestRawValue(Query &&query,
                                                       CallbackLike &&cb) {
  return RequestValue<ozo::result>(std::forward<Query>(query),
                                   std::forward<CallbackLike>(cb));
}

template <typename Text, typename... Args>
  requires(ozo::QueryText<std::decay_t<Text>> &&
           !IsBinaryQueryObject<Text> &&
           sizeof...(Args) > 0)
inline std::error_code PostgreSqlTask::RequestRawValue(Text &&text,
                                                       Args &&...args) {
  auto stored_args = std::make_tuple(std::forward<Args>(args)...);
  constexpr std::size_t kCallbackIndex = sizeof...(Args) - 1;
  auto query = MakeQueryFromStoredArguments(std::forward<Text>(text),
                                            stored_args);
  return RequestRawValue(std::move(query),
                         std::move(std::get<kCallbackIndex>(stored_args)));
}

template <typename Output, typename CallbackLike>
inline auto PostgreSqlTask::BindOutput(std::shared_ptr<Output> output,
                                       CallbackLike &&cb) {
  using StoredCallback = std::decay_t<CallbackLike>;
  return [output = std::move(output),
          callback = StoredCallback(std::forward<CallbackLike>(cb))](
             const PostgreSqlTaskResult &result) mutable {
    std::invoke(callback, result, *output);
  };
}

template <typename Text, typename Tuple, std::size_t... Indexes>
inline auto PostgreSqlTask::MakeQueryFromStoredArgumentsImpl(
    Text &&text, Tuple &stored_arguments, std::index_sequence<Indexes...>) {
  return MakeQuery(std::forward<Text>(text),
                   std::move(std::get<Indexes>(stored_arguments))...);
}

template <typename Text, typename Tuple>
inline auto PostgreSqlTask::MakeQueryFromStoredArguments(
    Text &&text, Tuple &stored_arguments) {
  using StoredTuple = std::remove_reference_t<Tuple>;
  constexpr std::size_t kTupleSize = std::tuple_size_v<StoredTuple>;
  static_assert(kTupleSize > 0,
                "parameterized query helpers expect a callback argument");
  return MakeQueryFromStoredArgumentsImpl(
      std::forward<Text>(text), stored_arguments,
      std::make_index_sequence<kTupleSize - 1>{});
}

template <typename Query>
inline void PostgreSqlTask::StartExecuteImpl(Query query, Callback cb) {
  if (GetActualPhase() == PostgreSqlTaskPhase::kCreated) {
    StartExecuteFromSource(std::move(query), std::move(cb));
    return;
  }
  StartExecuteFromHandle(std::move(query), std::move(cb));
}

template <typename Query, typename Out>
inline void PostgreSqlTask::StartRequestImpl(Query query, Out out,
                                             Callback cb) {
  if (GetActualPhase() == PostgreSqlTaskPhase::kCreated) {
    StartRequestFromSource(std::move(query), std::move(out), std::move(cb));
    return;
  }
  StartRequestFromHandle(std::move(query), std::move(out), std::move(cb));
}

template <typename Query>
inline void PostgreSqlTask::StartExecuteFromSource(Query query, Callback cb) {
  auto handler = [self = shared_from_this(), callback = std::move(cb)](
                     ozo::error_code ec, auto next_handle) mutable {
    auto cancelled = ec == boost::asio::error::operation_aborted;
    auto handle_variant = MakeHandleVariant(std::move(next_handle));
    boost::asio::dispatch(
        self->operation_strand_,
        [self, handle_variant = std::move(handle_variant), ec, cancelled,
         callback = std::move(callback)]() mutable {
          self->FinalizeOperation(std::move(handle_variant), ec, cancelled,
                                  std::move(callback));
        });
  };

  std::visit(
      [this, query = std::move(query),
       handler = std::move(handler)](auto &source) mutable {
        using Source = std::decay_t<decltype(source)>;
        if constexpr (std::is_same_v<Source, DirectSource>) {
          ozo::execute(source[GetIoContext()], std::move(query),
                       operation_timeout_, std::move(handler));
        } else {
          ozo::execute((*source)[GetIoContext()], std::move(query),
                       operation_timeout_, std::move(handler));
        }
      },
      source_);
}

template <typename Query>
inline void PostgreSqlTask::StartExecuteFromHandle(Query query, Callback cb) {
  auto handler = [self = shared_from_this(), callback = std::move(cb)](
                     ozo::error_code ec, auto next_handle) mutable {
    auto cancelled = ec == boost::asio::error::operation_aborted;
    auto handle_variant = MakeHandleVariant(std::move(next_handle));
    boost::asio::dispatch(
        self->operation_strand_,
        [self, handle_variant = std::move(handle_variant), ec, cancelled,
         callback = std::move(callback)]() mutable {
          self->FinalizeOperation(std::move(handle_variant), ec, cancelled,
                                  std::move(callback));
        });
  };

  std::visit(
      [this, query = std::move(query),
       handler = std::move(handler)](auto &handle) mutable {
        using Handle = std::decay_t<decltype(handle)>;
        if constexpr (std::is_same_v<Handle, std::monostate>) {
          boost::asio::dispatch(
              operation_strand_, [handler = std::move(handler)]() mutable {
                handler(boost::asio::error::not_connected, std::monostate{});
              });
        } else {
          auto handle_copy = handle;
          ozo::execute(std::move(handle_copy), std::move(query),
                       operation_timeout_, std::move(handler));
        }
      },
      handle_);
}

template <typename Query, typename Out>
inline void PostgreSqlTask::StartRequestFromSource(Query query, Out out,
                                                   Callback cb) {
  // OZO decodes query rows into `out`; bozo only preserves execution order and
  // updates the task handle/state seen by callbacks.
  auto handler = [self = shared_from_this(), callback = std::move(cb)](
                     ozo::error_code ec, auto next_handle) mutable {
    auto cancelled = ec == boost::asio::error::operation_aborted;
    auto handle_variant = MakeHandleVariant(std::move(next_handle));
    boost::asio::dispatch(
        self->operation_strand_,
        [self, handle_variant = std::move(handle_variant), ec, cancelled,
         callback = std::move(callback)]() mutable {
          self->FinalizeOperation(std::move(handle_variant), ec, cancelled,
                                  std::move(callback));
        });
  };

  std::visit(
      [this, query = std::move(query), out = std::move(out),
       handler = std::move(handler)](auto &source) mutable {
        using Source = std::decay_t<decltype(source)>;
        if constexpr (std::is_same_v<Source, DirectSource>) {
          ozo::request(source[GetIoContext()], std::move(query),
                       operation_timeout_, std::move(out), std::move(handler));
        } else {
          ozo::request((*source)[GetIoContext()], std::move(query),
                       operation_timeout_, std::move(out), std::move(handler));
        }
      },
      source_);
}

template <typename Query, typename Out>
inline void PostgreSqlTask::StartRequestFromHandle(Query query, Out out,
                                                   Callback cb) {
  // Result data handling stays inside OZO. bozo receives only the decoded
  // output object side effects plus the next connection/transaction handle.
  auto handler = [self = shared_from_this(), callback = std::move(cb)](
                     ozo::error_code ec, auto next_handle) mutable {
    auto cancelled = ec == boost::asio::error::operation_aborted;
    auto handle_variant = MakeHandleVariant(std::move(next_handle));
    boost::asio::dispatch(
        self->operation_strand_,
        [self, handle_variant = std::move(handle_variant), ec, cancelled,
         callback = std::move(callback)]() mutable {
          self->FinalizeOperation(std::move(handle_variant), ec, cancelled,
                                  std::move(callback));
        });
  };

  std::visit(
      [this, query = std::move(query), out = std::move(out),
       handler = std::move(handler)](auto &handle) mutable {
        using Handle = std::decay_t<decltype(handle)>;
        if constexpr (std::is_same_v<Handle, std::monostate>) {
          boost::asio::dispatch(
              operation_strand_, [handler = std::move(handler)]() mutable {
                handler(boost::asio::error::not_connected, std::monostate{});
              });
        } else {
          auto handle_copy = handle;
          ozo::request(std::move(handle_copy), std::move(query),
                       operation_timeout_, std::move(out), std::move(handler));
        }
      },
      handle_);
}

} // namespace bozo::postgresql

#endif // BOZO_POSTGRESQL_POSTGRESQL_TASK_INL_H_
