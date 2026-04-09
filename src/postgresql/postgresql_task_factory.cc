/**
 * @file postgresql_task_factory.cc
 * @brief Direct and pooled PostgreSqlTask factory implementation.
 */

#include "bozo/postgresql/postgresql_task.h"

#include <utility>

namespace bozo::postgresql {

PostgreSqlTaskFactory::PostgreSqlTaskFactory(Executor io_executor,
                                             std::string connection_string,
                                             PostgreSqlTaskOptions options)
    : io_executor_(io_executor),
      callback_executor_(io_executor),
      source_(std::move(connection_string)),
      options_(std::move(options)) {}

PostgreSqlTaskFactory::PostgreSqlTaskFactory(Executor io_executor,
                                             Executor callback_executor,
                                             std::string connection_string,
                                             PostgreSqlTaskOptions options)
    : io_executor_(io_executor),
      callback_executor_(callback_executor),
      source_(std::move(connection_string)),
      options_(std::move(options)) {}

std::shared_ptr<PostgreSqlTask> PostgreSqlTaskFactory::Create() const {
  return std::shared_ptr<PostgreSqlTask>(
      new PostgreSqlTask(io_executor_, callback_executor_, source_, options_));
}

std::shared_ptr<PostgreSqlTask> PostgreSqlTaskFactory::CreateTask(
    Executor io_executor, std::string connection_string,
    PostgreSqlTaskOptions options) {
  return PostgreSqlTaskFactory(io_executor, std::move(connection_string),
                               std::move(options))
      .Create();
}

std::shared_ptr<PostgreSqlTask> PostgreSqlTaskFactory::CreateTask(
    Executor io_executor, Executor callback_executor,
    std::string connection_string, PostgreSqlTaskOptions options) {
  return PostgreSqlTaskFactory(io_executor, callback_executor,
                               std::move(connection_string), std::move(options))
      .Create();
}

PostgreSqlTaskPoolFactory::PostgreSqlTaskPoolFactory(
    Executor io_executor, std::string connection_string,
    PostgreSqlTaskPoolOptions options)
    : io_executor_(io_executor),
      callback_executor_(io_executor),
      source_(std::make_shared<PostgreSqlTask::PoolSource>(
          PostgreSqlTask::DirectSource(std::move(connection_string)),
          options.pool_config)),
      options_(std::move(options)) {}

PostgreSqlTaskPoolFactory::PostgreSqlTaskPoolFactory(
    Executor io_executor, Executor callback_executor,
    std::string connection_string, PostgreSqlTaskPoolOptions options)
    : io_executor_(io_executor),
      callback_executor_(callback_executor),
      source_(std::make_shared<PostgreSqlTask::PoolSource>(
          PostgreSqlTask::DirectSource(std::move(connection_string)),
          options.pool_config)),
      options_(std::move(options)) {}

std::shared_ptr<PostgreSqlTask> PostgreSqlTaskPoolFactory::Create() const {
  return std::shared_ptr<PostgreSqlTask>(
      new PostgreSqlTask(io_executor_, callback_executor_, source_, options_));
}

std::shared_ptr<PostgreSqlTask> PostgreSqlTaskPoolFactory::CreateTask(
    Executor io_executor, std::string connection_string,
    PostgreSqlTaskPoolOptions options) {
  return PostgreSqlTaskPoolFactory(io_executor, std::move(connection_string),
                                   std::move(options))
      .Create();
}

std::shared_ptr<PostgreSqlTask> PostgreSqlTaskPoolFactory::CreateTask(
    Executor io_executor, Executor callback_executor,
    std::string connection_string, PostgreSqlTaskPoolOptions options) {
  return PostgreSqlTaskPoolFactory(io_executor, callback_executor,
                                   std::move(connection_string),
                                   std::move(options))
      .Create();
}

}  // namespace bozo::postgresql
